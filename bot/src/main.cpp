#include "config.hpp"
#include "state.hpp"
#include "logger.hpp"
#include "kraken_client.hpp"
#include "strategy.hpp"
#include "util.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

// Global flag for graceful shutdown
static std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    LOG_INFO("Received signal " + std::to_string(signal) + ", initiating shutdown...");
    g_running = false;
}

bool check_kill_switch(const std::string& kill_switch_file) {
    if (util::file_exists(kill_switch_file)) {
        LOG_WARNING("Kill switch active: " + kill_switch_file);
        return true;
    }
    return false;
}

void reconcile_live_state(TradingState& state, KrakenClient& client, double current_price, const Config& config) {
    LOG_INFO("Reconciling state with live Kraken balances...");
    
    BalanceResult balance = client.get_balance();
    if (!balance.success) {
        LOG_ERROR("Failed to fetch balances for reconciliation: " + balance.error);
        LOG_WARNING("Proceeding with persisted state - manual verification recommended");
        return;
    }
    
    const double btc_threshold = 0.000001;  // Minimum BTC to consider as "holding"
    
    if (balance.btc_balance > btc_threshold) {
        // We have BTC - should be in LONG mode
        if (state.mode != TradingMode::LONG) {
            LOG_WARNING("Reconciliation: Found BTC balance but state is FLAT, setting to LONG");
            state.mode = TradingMode::LONG;
        }
        
        state.btc_amount = balance.btc_balance;
        
        // Check if entry_price is missing
        if (!state.entry_price.has_value()) {
            LOG_WARNING("!!! ENTRY PRICE MISSING WHILE HOLDING BTC !!!");
            LOG_WARNING("Setting entry_price to current price: " + std::to_string(current_price));
            LOG_WARNING("This may not reflect actual entry - verify manually if concerned");
            state.entry_price = current_price;
        }
        
        LOG_INFO("Reconciled: mode=LONG, btc_amount=" + std::to_string(state.btc_amount) + 
                 ", entry_price=" + std::to_string(state.entry_price.value_or(0)));
    } else {
        // No significant BTC - should be FLAT
        if (state.mode != TradingMode::FLAT) {
            LOG_WARNING("Reconciliation: No BTC balance but state is LONG, setting to FLAT");
            state.mode = TradingMode::FLAT;
        }
        
        state.btc_amount = 0.0;
        
        LOG_INFO("Reconciled: mode=FLAT, cad_balance=" + std::to_string(balance.cad_balance));
    }
    
    state.save(config.state_file);
}

void log_status(const TradingState& state, const TradeContext& ctx, const Config& config) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "Status | "
        << "price=" << ctx.current_price
        << " | mode=" << mode_to_string(state.mode)
        << " | entry=" << (state.entry_price.has_value() ? std::to_string(state.entry_price.value()) : "null")
        << " | exit=" << (state.exit_price.has_value() ? std::to_string(state.exit_price.value()) : "null")
        << " | tp=" << ctx.tp_price
        << " | sl=" << ctx.sl_price
        << " | cooldown=" << state.cooldown_remaining(config.cooldown_seconds) << "s"
        << " | trades=" << state.trades_today << "/" << config.max_trades_per_day
        << " | date=" << state.trades_date_yyyy_mm_dd
        << " | equity=" << ctx.sizing.equity_cad
        << " | available=" << ctx.sizing.available_cad
        << " | risk_pct=" << (config.risk_per_trade_pct * 100) << "%"
        << " | risk_cad=" << ctx.sizing.risk_cad
        << " | pos_cad=" << ctx.sizing.position_cad
        << " | max_pos=" << ctx.sizing.max_position_cad
        << " | decision=" << decision_to_string(ctx.decision)
        << " | reason=" << ctx.decision_reason;
    
    LOG_INFO(oss.str());
}

void write_ui_status(const TradingState& state, const TradeContext& ctx, const Config& config) {
    namespace fs = std::filesystem;
    fs::create_directories(config.ui_dir);

    nlohmann::json j;
    j["price"] = ctx.current_price;
    j["mode"] = mode_to_string(state.mode);
    j["entry_price"] = state.entry_price.has_value() ? state.entry_price.value() : 0.0;
    j["exit_price"] = state.exit_price.has_value() ? state.exit_price.value() : 0.0;
    j["tp_price"] = ctx.tp_price;
    j["sl_price"] = ctx.sl_price;
    j["decision"] = decision_to_string(ctx.decision);
    j["decision_reason"] = ctx.decision_reason;
    j["trades_today"] = state.trades_today;
    j["max_trades_per_day"] = config.max_trades_per_day;
    j["equity_cad"] = ctx.sizing.equity_cad;
    j["available_cad"] = ctx.sizing.available_cad;
    j["risk_cad"] = ctx.sizing.risk_cad;
    j["position_cad"] = ctx.sizing.position_cad;
    j["spread_pct"] = ctx.spread_pct;
    j["atr"] = ctx.atr;
    j["sma_short"] = ctx.sma_short;
    j["sma_long"] = ctx.sma_long;

    std::ofstream status_file(config.ui_dir + "/status.json");
    status_file << j.dump(2) << std::endl;

    fs::path index_path = fs::path(config.ui_dir) / "index.html";
    if (!fs::exists(index_path)) {
        std::ofstream index_file(index_path);
        index_file << "<!doctype html>\n"
                      "<html lang=\"en\">\n"
                      "<head>\n"
                      "  <meta charset=\"utf-8\" />\n"
                      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
                      "  <title>Trading Bot Status</title>\n"
                      "  <style>\n"
                      "    body { font-family: sans-serif; margin: 20px; }\n"
                      "    .card { border: 1px solid #ddd; padding: 16px; border-radius: 8px; max-width: 600px; }\n"
                      "    .row { margin: 6px 0; }\n"
                      "    .label { font-weight: bold; }\n"
                      "  </style>\n"
                      "</head>\n"
                      "<body>\n"
                      "  <h2>Trading Bot Status</h2>\n"
                      "  <div class=\"card\" id=\"card\">Loading...</div>\n"
                      "  <script>\n"
                      "    async function loadStatus() {\n"
                      "      const res = await fetch('status.json?_=' + Date.now());\n"
                      "      const s = await res.json();\n"
                      "      document.getElementById('card').innerHTML = `\n"
                      "        <div class=\"row\"><span class=\"label\">Price:</span> ${s.price}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">Mode:</span> ${s.mode}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">Entry:</span> ${s.entry_price}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">Exit:</span> ${s.exit_price}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">TP:</span> ${s.tp_price}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">SL:</span> ${s.sl_price}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">Decision:</span> ${s.decision}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">Reason:</span> ${s.decision_reason}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">Trades Today:</span> ${s.trades_today}/${s.max_trades_per_day}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">Equity (CAD):</span> ${s.equity_cad}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">Available (CAD):</span> ${s.available_cad}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">Spread %:</span> ${(s.spread_pct * 100).toFixed(4)}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">ATR:</span> ${s.atr}</div>\n"
                      "        <div class=\"row\"><span class=\"label\">SMA Short/Long:</span> ${s.sma_short} / ${s.sma_long}</div>\n"
                      "      `;\n"
                      "    }\n"
                      "    loadStatus();\n"
                      "    setInterval(loadStatus, 2000);\n"
                      "  </script>\n"
                      "</body>\n"
                      "</html>\n";
    }
}

int main(int argc, char* argv[]) {
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    // Determine config file path
    std::string config_file = "config.json";
    if (argc > 1) {
        config_file = argv[1];
    }
    
    // Load configuration
    Config config;
    try {
        config = Config::load(config_file);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 1;
    }
    
    // Initialize logger
    Logger::instance().init(config.log_dir);
    
    LOG_INFO("========================================");
    LOG_INFO("Kraken Trading Bot Starting");
    LOG_INFO("========================================");
    
    // Validate configuration
    if (!config.validate()) {
        LOG_ERROR("Configuration validation failed");
        return 1;
    }
    
    config.log_config();
    
    // Log mode
    if (config.dry_run) {
        LOG_INFO("*** RUNNING IN DRY-RUN MODE - NO REAL ORDERS WILL BE PLACED ***");
    } else {
        LOG_WARNING("*** RUNNING IN LIVE MODE - REAL ORDERS WILL BE PLACED ***");
    }
    
    // Check kill switch before starting
    if (check_kill_switch(config.kill_switch_file)) {
        LOG_INFO("Exiting due to kill switch");
        return 0;
    }
    
    // Initialize Kraken client
    KrakenClient client(config.kraken_api_base, config.rate_limit_min_delay_ms);
    
    if (!config.dry_run) {
        if (!client.init()) {
            LOG_ERROR("Failed to initialize Kraken client - API credentials required for live mode");
            return 1;
        }
    } else {
        // In dry-run mode, credentials are optional (only public endpoints used)
        client.init();  // Will warn if not set, but won't fail
    }
    
    // Load or initialize state
    TradingState state = TradingState::load(config.state_file);
    state.check_date_rollover();
    state.log_state();
    
    // Create strategy
    Strategy strategy(config, state, client);
    
    // Initialize simulation if in dry-run mode
    if (config.dry_run) {
        if (state.mode == TradingMode::FLAT && state.sim_cad_balance <= 0) {
            strategy.init_simulation(config.sim_initial_cad);
            state.save(config.state_file);
        }
        LOG_INFO("Simulation initialized: CAD=" + std::to_string(state.sim_cad_balance) + 
                 ", XBT=" + std::to_string(state.sim_btc_balance));
    } else {
        // Live mode: reconcile state with actual balances
        // First get current price for potential entry_price fallback
        TickerResult ticker = client.get_ticker(config.pair);
        if (ticker.success) {
            reconcile_live_state(state, client, ticker.last_price, config);
        } else {
            LOG_ERROR("Failed to get price for reconciliation: " + ticker.error);
            LOG_WARNING("Proceeding without reconciliation");
        }
    }
    
    LOG_INFO("Entering main loop...");
    LOG_INFO("Poll interval: " + std::to_string(config.poll_interval_seconds) + " seconds");
    
    // Main trading loop
    while (g_running) {
        // Check kill switch
        if (check_kill_switch(config.kill_switch_file)) {
            LOG_INFO("Exiting due to kill switch");
            break;
        }
        
        // Check for too many consecutive failures
        if (client.get_consecutive_failures() >= config.max_consecutive_failures) {
            LOG_ERROR("Too many consecutive API failures (" + 
                      std::to_string(client.get_consecutive_failures()) + 
                      "), halting bot");
            break;
        }
        
        // Evaluate strategy
        TradeContext ctx = strategy.evaluate();
        
        // Log status
        log_status(state, ctx, config);
        write_ui_status(state, ctx, config);
        
        // Execute if needed
        if (ctx.decision == Decision::BUY || ctx.decision == Decision::SELL) {
            if (!strategy.execute(ctx)) {
                LOG_ERROR("Failed to execute " + decision_to_string(ctx.decision));
            }
        }
        
        // Sleep until next poll
        for (int64_t i = 0; i < config.poll_interval_seconds && g_running; i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    LOG_INFO("Shutting down...");
    
    // Final state save
    state.save(config.state_file);
    
    LOG_INFO("Bot stopped cleanly");
    return 0;
}
