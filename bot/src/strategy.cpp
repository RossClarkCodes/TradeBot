#include "strategy.hpp"
#include "logger.hpp"
#include "util.hpp"
#include <sstream>
#include <iomanip>
#include <thread>
#include <cmath>

std::string decision_to_string(Decision d) {
    switch (d) {
        case Decision::NOOP:    return "NOOP";
        case Decision::BUY:     return "BUY";
        case Decision::SELL:    return "SELL";
        case Decision::BLOCKED: return "BLOCKED";
        default:                return "UNKNOWN";
    }
}

void TradeContext::log() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    
    oss << "TradeContext:"
        << "\n  current_price: " << current_price
        << "\n  price_stale: " << (price_stale ? "YES" : "no")
        << "\n  tp_price: " << tp_price
        << "\n  sl_price: " << sl_price
        << "\n  rebuy_price: " << rebuy_price
        << "\n  equity_cad: " << sizing.equity_cad
        << "\n  available_cad: " << sizing.available_cad
        << "\n  risk_cad: " << sizing.risk_cad
        << "\n  raw_position_cad: " << sizing.raw_position_cad
        << "\n  max_position_cad: " << sizing.max_position_cad
        << "\n  position_cad: " << sizing.position_cad
        << "\n  fee_buffer_cad: " << sizing.fee_buffer_cad
        << "\n  btc_to_buy: " << std::setprecision(8) << sizing.btc_to_buy
        << "\n  can_trade: " << (sizing.can_trade ? "YES" : "NO")
        << "\n  block_reason: " << sizing.block_reason
        << "\n  decision: " << decision_to_string(decision)
        << "\n  decision_reason: " << decision_reason;
    
    LOG_INFO(oss.str());
}

Strategy::Strategy(const Config& config, TradingState& state, KrakenClient& client)
    : config_(config)
    , state_(state)
    , client_(client) {
}

void Strategy::init_simulation(double initial_cad) {
    if (state_.mode == TradingMode::FLAT) {
        state_.sim_cad_balance = initial_cad;
        state_.sim_btc_balance = 0.0;
    }
    // If LONG, keep existing simulated position
    LOG_INFO("Simulation initialized with CAD: " + std::to_string(initial_cad));
}

bool Strategy::fetch_price(TradeContext& ctx) {
    TickerResult ticker = client_.get_ticker(config_.pair);
    
    if (!ticker.success) {
        LOG_ERROR("Failed to fetch ticker: " + ticker.error);
        ctx.decision = Decision::BLOCKED;
        ctx.decision_reason = "Price fetch failed: " + ticker.error;
        return false;
    }
    
    ctx.current_price = ticker.last_price;
    ctx.bid_price = ticker.bid_price;
    ctx.ask_price = ticker.ask_price;
    ctx.price_timestamp = ticker.timestamp;
    
    // Check for stale price
    int64_t age = util::now_epoch_seconds() - ctx.price_timestamp;
    ctx.price_stale = age > config_.stale_price_seconds;
    
    if (ctx.price_stale) {
        LOG_WARNING("Price is stale (age: " + std::to_string(age) + "s)");
        ctx.decision = Decision::BLOCKED;
        ctx.decision_reason = "Price data is stale";
        return false;
    }
    
    return true;
}

void Strategy::update_indicators(TradeContext& ctx) {
    if (ctx.current_price <= 0) {
        return;
    }

    if (!price_history_.empty()) {
        double prev_price = price_history_.back();
        double tr = std::abs(ctx.current_price - prev_price);
        tr_history_.push_back(tr);
        if (static_cast<int>(tr_history_.size()) > config_.atr_window) {
            tr_history_.pop_front();
        }
    }

    price_history_.push_back(ctx.current_price);
    if (static_cast<int>(price_history_.size()) > config_.trend_window_long) {
        price_history_.pop_front();
    }

    if (config_.atr_window > 0 && !tr_history_.empty()) {
        double tr_sum = 0.0;
        for (double tr : tr_history_) {
            tr_sum += tr;
        }
        ctx.atr = tr_sum / static_cast<double>(tr_history_.size());
    }

    if (config_.trend_window_short > 0 && config_.trend_window_long > 0 &&
        static_cast<int>(price_history_.size()) >= config_.trend_window_long) {
        double short_sum = 0.0;
        double long_sum = 0.0;
        int long_count = 0;
        int short_count = 0;
        int start_index = static_cast<int>(price_history_.size()) - config_.trend_window_long;
        for (int i = start_index; i < static_cast<int>(price_history_.size()); ++i) {
            long_sum += price_history_[i];
            long_count++;
            if (i >= static_cast<int>(price_history_.size()) - config_.trend_window_short) {
                short_sum += price_history_[i];
                short_count++;
            }
        }
        if (long_count > 0) {
            ctx.sma_long = long_sum / static_cast<double>(long_count);
        }
        if (short_count > 0) {
            ctx.sma_short = short_sum / static_cast<double>(short_count);
        }
    }

    if (ctx.bid_price > 0.0 && ctx.ask_price > 0.0 && ctx.ask_price >= ctx.bid_price) {
        double mid = (ctx.bid_price + ctx.ask_price) / 2.0;
        if (mid > 0) {
            ctx.spread_pct = (ctx.ask_price - ctx.bid_price) / mid;
        }
    }
}

bool Strategy::passes_trend_filter(TradeContext& ctx) const {
    if (!config_.require_trend_up) {
        return true;
    }
    if (ctx.sma_short <= 0 || ctx.sma_long <= 0) {
        return false;
    }
    return ctx.sma_short >= ctx.sma_long;
}

bool Strategy::passes_volatility_filter(TradeContext& ctx) const {
    if (config_.min_atr_pct <= 0 || ctx.current_price <= 0) {
        return true;
    }
    if (ctx.atr <= 0) {
        return false;
    }
    double atr_pct = ctx.atr / ctx.current_price;
    return atr_pct >= config_.min_atr_pct;
}

void Strategy::calculate_sizing(TradeContext& ctx) {
    // Get equity and available balances
    if (config_.dry_run) {
        // In dry-run mode, use simulated balances
        if (state_.mode == TradingMode::FLAT) {
            ctx.sizing.equity_cad = state_.sim_cad_balance;
            ctx.sizing.available_cad = state_.sim_cad_balance;
        } else {
            // LONG mode: equity is CAD + (BTC value)
            double btc_value = state_.sim_btc_balance * ctx.current_price;
            ctx.sizing.equity_cad = state_.sim_cad_balance + btc_value;
            ctx.sizing.available_cad = state_.sim_cad_balance;
        }
    } else {
        // Live mode: fetch from Kraken
        BalanceResult balance = client_.get_balance();
        if (!balance.success) {
            ctx.sizing.can_trade = false;
            ctx.sizing.block_reason = "Balance fetch failed: " + balance.error;
            return;
        }
        
        ctx.sizing.available_cad = balance.cad_balance;
        double btc_value = balance.btc_balance * ctx.current_price;
        ctx.sizing.equity_cad = balance.cad_balance + btc_value;
    }
    
    // Calculate fee buffer
    double min_buffer = 1.0;  // Absolute minimum buffer
    ctx.sizing.fee_buffer_cad = std::max(min_buffer, 
        ctx.sizing.equity_cad * config_.min_cad_required_pct);
    
    // Calculate position sizing using percent risk
    // risk_cad = equity_cad * risk_per_trade_pct
    ctx.sizing.risk_cad = ctx.sizing.equity_cad * config_.risk_per_trade_pct;
    
    // raw_position_cad = risk_cad / stop_loss_pct
    // This is the position size where a stop_loss_pct move equals risk_cad loss
    if (config_.stop_loss_pct > 0) {
        ctx.sizing.raw_position_cad = ctx.sizing.risk_cad / config_.stop_loss_pct;
    } else {
        ctx.sizing.raw_position_cad = 0;
    }
    
    // max_position_cad = equity_cad * max_position_pct
    ctx.sizing.max_position_cad = ctx.sizing.equity_cad * config_.max_position_pct;
    
    // position_cad = min(raw_position_cad, max_position_cad)
    ctx.sizing.position_cad = std::min(ctx.sizing.raw_position_cad, ctx.sizing.max_position_cad);
    
    // Calculate BTC amount to buy
    if (ctx.current_price > 0) {
        ctx.sizing.btc_to_buy = ctx.sizing.position_cad / ctx.current_price;
    } else {
        ctx.sizing.btc_to_buy = 0;
    }
    
    // Check if we can trade
    double required_cad = ctx.sizing.position_cad + ctx.sizing.fee_buffer_cad;
    if (ctx.sizing.available_cad < required_cad) {
        ctx.sizing.can_trade = false;
        ctx.sizing.block_reason = "Insufficient CAD: need " + 
            std::to_string(required_cad) + ", have " + 
            std::to_string(ctx.sizing.available_cad);
    } else if (ctx.sizing.position_cad < 1.0) {
        ctx.sizing.can_trade = false;
        ctx.sizing.block_reason = "Position size too small: " + 
            std::to_string(ctx.sizing.position_cad) + " CAD";
    } else {
        ctx.sizing.can_trade = true;
    }
}

bool Strategy::check_blocking_conditions(TradeContext& ctx) {
    // Check cooldown
    if (state_.is_in_cooldown(config_.cooldown_seconds)) {
        ctx.decision = Decision::BLOCKED;
        ctx.decision_reason = "Cooldown active: " + 
            std::to_string(state_.cooldown_remaining(config_.cooldown_seconds)) + "s remaining";
        return true;
    }
    
    // Check max trades per day
    if (state_.trades_today >= config_.max_trades_per_day) {
        ctx.decision = Decision::BLOCKED;
        ctx.decision_reason = "Max trades per day reached: " + 
            std::to_string(state_.trades_today) + "/" + 
            std::to_string(config_.max_trades_per_day);
        return true;
    }
    
    // Check consecutive API failures
    if (client_.get_consecutive_failures() >= config_.max_consecutive_failures) {
        ctx.decision = Decision::BLOCKED;
        ctx.decision_reason = "Too many consecutive API failures: " + 
            std::to_string(client_.get_consecutive_failures());
        return true;
    }
    
    return false;  // Not blocked
}

bool Strategy::check_market_conditions(TradeContext& ctx) {
    if (config_.max_spread_pct > 0 && ctx.spread_pct > config_.max_spread_pct) {
        ctx.decision = Decision::BLOCKED;
        ctx.decision_reason = "Spread too wide: " + std::to_string(ctx.spread_pct * 100) + "%";
        return true;
    }

    if (!passes_volatility_filter(ctx)) {
        ctx.decision = Decision::BLOCKED;
        ctx.decision_reason = "Volatility too low (ATR): " + std::to_string(ctx.atr);
        return true;
    }

    if (!passes_trend_filter(ctx)) {
        ctx.decision = Decision::BLOCKED;
        ctx.decision_reason = "Trend filter: SMA short below SMA long";
        return true;
    }

    return false;
}

bool Strategy::check_entry_condition(TradeContext& ctx) {
    // First trade ever: enter immediately
    if (!state_.exit_price.has_value()) {
        ctx.decision_reason = "First trade: entering immediately";
        return true;
    }
    
    // Subsequent trades: require price reset
    ctx.rebuy_price = state_.exit_price.value() * (1.0 - config_.rebuy_reset_pct);
    
    if (ctx.current_price <= ctx.rebuy_price) {
        ctx.decision_reason = "Price reset condition met: " + 
            std::to_string(ctx.current_price) + " <= rebuy_price " + 
            std::to_string(ctx.rebuy_price);
        return true;
    }
    
    ctx.decision_reason = "Waiting for price reset: current=" + 
        std::to_string(ctx.current_price) + ", rebuy_price=" + 
        std::to_string(ctx.rebuy_price);
    return false;
}

bool Strategy::check_exit_condition(TradeContext& ctx) {
    if (!state_.entry_price.has_value()) {
        LOG_ERROR("In LONG mode but entry_price is null!");
        ctx.decision_reason = "Error: missing entry price in LONG mode";
        return false;
    }
    
    double entry = state_.entry_price.value();
    if (config_.use_dynamic_tp_sl && ctx.atr > 0) {
        ctx.tp_price = entry + (ctx.atr * config_.tp_atr_mult);
        ctx.sl_price = entry - (ctx.atr * config_.sl_atr_mult);
    } else {
        ctx.tp_price = entry * (1.0 + config_.take_profit_pct);
        ctx.sl_price = entry * (1.0 - config_.stop_loss_pct);
    }

    if (!state_.partial_take_profit_done && config_.partial_tp_pct > 0) {
        double partial_tp_price = entry * (1.0 + config_.partial_tp_pct);
        if (ctx.current_price >= partial_tp_price) {
            ctx.decision_reason = "Partial take profit triggered";
            ctx.is_partial_exit = true;
            double current_btc = config_.dry_run ? state_.sim_btc_balance : state_.btc_amount;
            ctx.sell_volume = current_btc * config_.partial_tp_sell_pct;
            return true;
        }
    }

    if (config_.trailing_stop_pct > 0) {
        double trailing_base = ctx.current_price * (1.0 - config_.trailing_stop_pct);
        if (!state_.trailing_stop_price.has_value()) {
            state_.trailing_stop_price = trailing_base;
        } else if (trailing_base > state_.trailing_stop_price.value()) {
            state_.trailing_stop_price = trailing_base;
        }
        if (ctx.current_price <= state_.trailing_stop_price.value()) {
            ctx.decision_reason = "Trailing stop triggered";
            return true;
        }
    }

    if (config_.max_hold_seconds > 0 && state_.entry_time.has_value()) {
        int64_t now = util::now_epoch_seconds();
        if ((now - state_.entry_time.value()) >= config_.max_hold_seconds) {
            ctx.decision_reason = "Time-based exit triggered";
            return true;
        }
    }
    
    // Check take profit
    if (ctx.current_price >= ctx.tp_price) {
        ctx.decision_reason = "Take profit triggered: " + 
            std::to_string(ctx.current_price) + " >= tp_price " + 
            std::to_string(ctx.tp_price);
        return true;
    }
    
    // Check stop loss
    if (ctx.current_price <= ctx.sl_price) {
        ctx.decision_reason = "Stop loss triggered: " + 
            std::to_string(ctx.current_price) + " <= sl_price " + 
            std::to_string(ctx.sl_price);
        return true;
    }
    
    ctx.decision_reason = "Holding position: price=" + 
        std::to_string(ctx.current_price) + 
        ", entry=" + std::to_string(entry) + 
        ", tp=" + std::to_string(ctx.tp_price) + 
        ", sl=" + std::to_string(ctx.sl_price);
    return false;
}

TradeContext Strategy::evaluate() {
    TradeContext ctx;
    
    // Check date rollover (resets trades_today)
    state_.check_date_rollover();
    
    // Fetch current price
    if (!fetch_price(ctx)) {
        return ctx;
    }

    update_indicators(ctx);
    
    // Check blocking conditions
    if (check_blocking_conditions(ctx)) {
        return ctx;
    }
    
    // Mode-specific logic
    if (state_.mode == TradingMode::FLAT) {
        // Calculate sizing for potential entry
        calculate_sizing(ctx);
        
        if (!ctx.sizing.can_trade) {
            ctx.decision = Decision::BLOCKED;
            ctx.decision_reason = ctx.sizing.block_reason;
            return ctx;
        }

        if (check_market_conditions(ctx)) {
            return ctx;
        }
        
        if (check_entry_condition(ctx)) {
            ctx.decision = Decision::BUY;
        } else {
            ctx.decision = Decision::NOOP;
        }
    } else {
        // LONG mode
        // Set levels for logging even if not exiting
        if (state_.entry_price.has_value()) {
            if (config_.use_dynamic_tp_sl && ctx.atr > 0) {
                ctx.tp_price = state_.entry_price.value() + (ctx.atr * config_.tp_atr_mult);
                ctx.sl_price = state_.entry_price.value() - (ctx.atr * config_.sl_atr_mult);
            } else {
                ctx.tp_price = state_.entry_price.value() * (1.0 + config_.take_profit_pct);
                ctx.sl_price = state_.entry_price.value() * (1.0 - config_.stop_loss_pct);
            }
        }
        
        if (check_exit_condition(ctx)) {
            ctx.decision = Decision::SELL;
        } else {
            ctx.decision = Decision::NOOP;
        }
    }
    
    return ctx;
}

bool Strategy::wait_for_fill(const std::string& txid, OrderResult& out_result, int max_attempts) {
    for (int i = 0; i < max_attempts; i++) {
        // Wait before querying (market orders should fill quickly)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        out_result = client_.query_order(txid);
        
        if (out_result.success && out_result.status == "closed") {
            return true;
        }
        
        if (!out_result.error.empty() && 
            (out_result.status == "canceled" || out_result.status == "expired")) {
            LOG_ERROR("Order " + txid + " was " + out_result.status);
            return false;
        }
        
        LOG_INFO("Waiting for fill, attempt " + std::to_string(i + 1) + "/" + std::to_string(max_attempts));
    }
    
    LOG_ERROR("Timeout waiting for order fill: " + txid);
    return false;
}

bool Strategy::execute_buy(const TradeContext& ctx) {
    std::string mode_label = config_.dry_run ? "[SIMULATED] " : "";
    
    LOG_INFO(mode_label + "Executing BUY: " + 
             std::to_string(ctx.sizing.btc_to_buy) + " XBT @ ~" + 
             std::to_string(ctx.current_price) + " CAD");
    
    if (config_.dry_run) {
        // Simulate the buy
        simulate_fill("buy", ctx.sizing.btc_to_buy, ctx.current_price);
        return true;
    }
    
    // Live mode: place actual order
    OrderResult order = client_.place_market_order(config_.pair, "buy", ctx.sizing.btc_to_buy);
    
    if (!order.success) {
        LOG_ERROR("Failed to place buy order: " + order.error);
        return false;
    }
    
    // Wait for fill confirmation
    OrderResult fill_result;
    if (!wait_for_fill(order.txid, fill_result)) {
        LOG_ERROR("Failed to confirm buy fill");
        // Do NOT update state if we can't confirm the fill
        return false;
    }
    
    // Update state with confirmed fill details
    state_.entry_price = fill_result.avg_price;
    state_.btc_amount = fill_result.volume;
    state_.mode = TradingMode::LONG;
    state_.trades_today++;
    state_.last_trade_time = util::now_epoch_seconds();
    state_.entry_time = state_.last_trade_time;
    state_.partial_take_profit_done = false;
    if (config_.trailing_stop_pct > 0) {
        state_.trailing_stop_price = fill_result.avg_price * (1.0 - config_.trailing_stop_pct);
    } else {
        state_.trailing_stop_price = std::nullopt;
    }
    state_.save(config_.state_file);
    
    LOG_INFO("BUY FILLED: txid=" + fill_result.txid + 
             ", vol=" + std::to_string(fill_result.volume) + 
             ", avg_price=" + std::to_string(fill_result.avg_price) + 
             ", fee=" + std::to_string(fill_result.fee));
    
    return true;
}

bool Strategy::execute_sell(const TradeContext& ctx) {
    std::string mode_label = config_.dry_run ? "[SIMULATED] " : "";
    
    double current_btc = config_.dry_run ? state_.sim_btc_balance : state_.btc_amount;
    double btc_to_sell = (ctx.sell_volume > 0.0) ? ctx.sell_volume : current_btc;

    if (btc_to_sell <= 0.0) {
        LOG_ERROR("Sell volume is zero or negative");
        return false;
    }
    if (btc_to_sell > current_btc) {
        btc_to_sell = current_btc;
    }
    
    LOG_INFO(mode_label + "Executing SELL: " + 
             std::to_string(btc_to_sell) + " XBT @ ~" + 
             std::to_string(ctx.current_price) + " CAD");
    
    if (config_.dry_run) {
        // Simulate the sell
        simulate_fill("sell", btc_to_sell, ctx.current_price);
        return true;
    }
    
    // Live mode: place actual order
    OrderResult order = client_.place_market_order(config_.pair, "sell", btc_to_sell);
    
    if (!order.success) {
        LOG_ERROR("Failed to place sell order: " + order.error);
        return false;
    }
    
    // Wait for fill confirmation
    OrderResult fill_result;
    if (!wait_for_fill(order.txid, fill_result)) {
        LOG_ERROR("Failed to confirm sell fill");
        // Do NOT update state if we can't confirm the fill
        return false;
    }
    
    // Update state with confirmed fill details
    state_.exit_price = fill_result.avg_price;
    state_.btc_amount = std::max(0.0, state_.btc_amount - fill_result.volume);
    if (ctx.is_partial_exit && state_.btc_amount > 0.0) {
        state_.partial_take_profit_done = true;
        state_.mode = TradingMode::LONG;
    } else {
        state_.mode = TradingMode::FLAT;
        state_.entry_time = std::nullopt;
        state_.trailing_stop_price = std::nullopt;
    }
    state_.trades_today++;
    state_.last_trade_time = util::now_epoch_seconds();
    state_.save(config_.state_file);
    
    LOG_INFO("SELL FILLED: txid=" + fill_result.txid + 
             ", vol=" + std::to_string(fill_result.volume) + 
             ", avg_price=" + std::to_string(fill_result.avg_price) + 
             ", fee=" + std::to_string(fill_result.fee));
    
    // Log P&L if we have entry price
    if (state_.entry_price.has_value()) {
        double pnl_pct = ((fill_result.avg_price - state_.entry_price.value()) / 
                          state_.entry_price.value()) * 100.0;
        LOG_INFO("Trade P&L: " + std::to_string(pnl_pct) + "% (before fees)");
    }
    
    return true;
}

void Strategy::simulate_fill(const std::string& side, double btc_amount, double price) {
    if (side == "buy") {
        double cost_cad = btc_amount * price;
        
        // Deduct CAD, add BTC
        state_.sim_cad_balance -= cost_cad;
        state_.sim_btc_balance = btc_amount;
        
        // Update state
        state_.entry_price = price;
        state_.btc_amount = btc_amount;
        state_.mode = TradingMode::LONG;
        state_.trades_today++;
        state_.last_trade_time = util::now_epoch_seconds();
        state_.entry_time = state_.last_trade_time;
        state_.partial_take_profit_done = false;
        if (config_.trailing_stop_pct > 0) {
            state_.trailing_stop_price = price * (1.0 - config_.trailing_stop_pct);
        } else {
            state_.trailing_stop_price = std::nullopt;
        }
        
        LOG_INFO("[SIMULATED] BUY FILLED: " + std::to_string(btc_amount) + 
                 " XBT @ " + std::to_string(price) + 
                 " (cost: " + std::to_string(cost_cad) + " CAD)");
        LOG_INFO("[SIMULATED] New balances: CAD=" + std::to_string(state_.sim_cad_balance) + 
                 ", XBT=" + std::to_string(state_.sim_btc_balance));
        
    } else {  // sell
        double proceeds_cad = btc_amount * price;
        
        // Apply simulated fee on round-trip
        double fee = proceeds_cad * config_.sim_fee_pct_roundtrip;
        proceeds_cad -= fee;
        
        // Add CAD, clear BTC
        state_.sim_cad_balance += proceeds_cad;
        state_.sim_btc_balance = std::max(0.0, state_.sim_btc_balance - btc_amount);
        
        // Log P&L
        double pnl_cad = 0.0;
        double pnl_pct = 0.0;
        if (state_.entry_price.has_value()) {
            double entry = state_.entry_price.value();
            double gross_proceeds = btc_amount * price;
            double cost = btc_amount * entry;
            pnl_cad = gross_proceeds - cost - fee;
            pnl_pct = (pnl_cad / cost) * 100.0;
        }
        
        // Update state
        state_.exit_price = price;
        state_.btc_amount = std::max(0.0, state_.btc_amount - btc_amount);
        if (state_.btc_amount > 0.0) {
            state_.partial_take_profit_done = true;
        } else {
            state_.mode = TradingMode::FLAT;
            state_.entry_time = std::nullopt;
            state_.trailing_stop_price = std::nullopt;
        }
        state_.trades_today++;
        state_.last_trade_time = util::now_epoch_seconds();
        
        LOG_INFO("[SIMULATED] SELL FILLED: " + std::to_string(btc_amount) + 
                 " XBT @ " + std::to_string(price) + 
                 " (proceeds: " + std::to_string(proceeds_cad) + 
                 " CAD, fee: " + std::to_string(fee) + " CAD)");
        LOG_INFO("[SIMULATED] P&L: " + std::to_string(pnl_cad) + " CAD (" + 
                 std::to_string(pnl_pct) + "%)");
        LOG_INFO("[SIMULATED] New balances: CAD=" + std::to_string(state_.sim_cad_balance) + 
                 ", XBT=" + std::to_string(state_.sim_btc_balance));
    }
    
    state_.save(config_.state_file);
}

bool Strategy::execute(const TradeContext& ctx) {
    switch (ctx.decision) {
        case Decision::BUY:
            return execute_buy(ctx);
        case Decision::SELL:
            return execute_sell(ctx);
        case Decision::NOOP:
        case Decision::BLOCKED:
            // No action needed
            return true;
        default:
            LOG_ERROR("Unknown decision type");
            return false;
    }
}
