#include "config.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

Config Config::load(const std::string& path) {
    Config cfg;
    
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + path);
    }
    
    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Failed to parse config JSON: " + std::string(e.what()));
    }
    
    // Trading pair
    if (j.contains("pair")) cfg.pair = j["pair"].get<std::string>();
    
    // Strategy parameters
    if (j.contains("take_profit_pct")) cfg.take_profit_pct = j["take_profit_pct"].get<double>();
    if (j.contains("stop_loss_pct")) cfg.stop_loss_pct = j["stop_loss_pct"].get<double>();
    if (j.contains("rebuy_reset_pct")) cfg.rebuy_reset_pct = j["rebuy_reset_pct"].get<double>();
    if (j.contains("trend_window_short")) cfg.trend_window_short = j["trend_window_short"].get<int>();
    if (j.contains("trend_window_long")) cfg.trend_window_long = j["trend_window_long"].get<int>();
    if (j.contains("require_trend_up")) cfg.require_trend_up = j["require_trend_up"].get<bool>();
    if (j.contains("atr_window")) cfg.atr_window = j["atr_window"].get<int>();
    if (j.contains("min_atr_pct")) cfg.min_atr_pct = j["min_atr_pct"].get<double>();
    if (j.contains("max_spread_pct")) cfg.max_spread_pct = j["max_spread_pct"].get<double>();
    
    // Position sizing
    if (j.contains("risk_per_trade_pct")) cfg.risk_per_trade_pct = j["risk_per_trade_pct"].get<double>();
    if (j.contains("max_position_pct")) cfg.max_position_pct = j["max_position_pct"].get<double>();
    if (j.contains("min_cad_required_pct")) cfg.min_cad_required_pct = j["min_cad_required_pct"].get<double>();
    if (j.contains("partial_tp_pct")) cfg.partial_tp_pct = j["partial_tp_pct"].get<double>();
    if (j.contains("partial_tp_sell_pct")) cfg.partial_tp_sell_pct = j["partial_tp_sell_pct"].get<double>();
    if (j.contains("trailing_stop_pct")) cfg.trailing_stop_pct = j["trailing_stop_pct"].get<double>();
    if (j.contains("max_hold_seconds")) cfg.max_hold_seconds = j["max_hold_seconds"].get<int64_t>();
    if (j.contains("use_dynamic_tp_sl")) cfg.use_dynamic_tp_sl = j["use_dynamic_tp_sl"].get<bool>();
    if (j.contains("tp_atr_mult")) cfg.tp_atr_mult = j["tp_atr_mult"].get<double>();
    if (j.contains("sl_atr_mult")) cfg.sl_atr_mult = j["sl_atr_mult"].get<double>();
    
    // Timing
    if (j.contains("poll_interval_seconds")) cfg.poll_interval_seconds = j["poll_interval_seconds"].get<int64_t>();
    if (j.contains("cooldown_seconds")) cfg.cooldown_seconds = j["cooldown_seconds"].get<int64_t>();
    if (j.contains("max_trades_per_day")) cfg.max_trades_per_day = j["max_trades_per_day"].get<int>();
    
    // Execution mode
    if (j.contains("dry_run")) cfg.dry_run = j["dry_run"].get<bool>();
    if (j.contains("sim_fee_pct_roundtrip")) cfg.sim_fee_pct_roundtrip = j["sim_fee_pct_roundtrip"].get<double>();
    if (j.contains("sim_initial_cad")) cfg.sim_initial_cad = j["sim_initial_cad"].get<double>();
    
    // API configuration
    if (j.contains("kraken_api_base")) cfg.kraken_api_base = j["kraken_api_base"].get<std::string>();
    if (j.contains("rate_limit_min_delay_ms")) cfg.rate_limit_min_delay_ms = j["rate_limit_min_delay_ms"].get<int64_t>();
    if (j.contains("max_consecutive_failures")) cfg.max_consecutive_failures = j["max_consecutive_failures"].get<int>();
    if (j.contains("stale_price_seconds")) cfg.stale_price_seconds = j["stale_price_seconds"].get<int64_t>();
    
    // File paths
    if (j.contains("state_file")) cfg.state_file = j["state_file"].get<std::string>();
    if (j.contains("kill_switch_file")) cfg.kill_switch_file = j["kill_switch_file"].get<std::string>();
    if (j.contains("log_dir")) cfg.log_dir = j["log_dir"].get<std::string>();
    if (j.contains("ui_dir")) cfg.ui_dir = j["ui_dir"].get<std::string>();
    
    return cfg;
}

bool Config::validate() const {
    bool valid = true;
    
    if (pair.empty()) {
        LOG_ERROR("Config: pair is empty");
        valid = false;
    }
    
    if (take_profit_pct <= 0 || take_profit_pct > 1.0) {
        LOG_ERROR("Config: take_profit_pct must be in (0, 1.0], got " + std::to_string(take_profit_pct));
        valid = false;
    }
    
    if (stop_loss_pct <= 0 || stop_loss_pct > 1.0) {
        LOG_ERROR("Config: stop_loss_pct must be in (0, 1.0], got " + std::to_string(stop_loss_pct));
        valid = false;
    }
    
    if (rebuy_reset_pct < 0 || rebuy_reset_pct > 1.0) {
        LOG_ERROR("Config: rebuy_reset_pct must be in [0, 1.0], got " + std::to_string(rebuy_reset_pct));
        valid = false;
    }

    if (trend_window_short < 1 || trend_window_long < 1 || trend_window_short > trend_window_long) {
        LOG_ERROR("Config: trend_window_short must be >=1 and <= trend_window_long, got " +
                  std::to_string(trend_window_short) + "/" + std::to_string(trend_window_long));
        valid = false;
    }

    if (atr_window < 1) {
        LOG_ERROR("Config: atr_window must be >= 1, got " + std::to_string(atr_window));
        valid = false;
    }

    if (min_atr_pct < 0 || min_atr_pct > 1.0) {
        LOG_ERROR("Config: min_atr_pct must be in [0, 1.0], got " + std::to_string(min_atr_pct));
        valid = false;
    }

    if (max_spread_pct < 0 || max_spread_pct > 0.1) {
        LOG_ERROR("Config: max_spread_pct must be in [0, 0.1], got " + std::to_string(max_spread_pct));
        valid = false;
    }
    
    if (risk_per_trade_pct <= 0 || risk_per_trade_pct > 0.10) {
        LOG_ERROR("Config: risk_per_trade_pct must be in (0, 0.10], got " + std::to_string(risk_per_trade_pct));
        valid = false;
    }
    
    if (max_position_pct <= 0 || max_position_pct > 1.0) {
        LOG_ERROR("Config: max_position_pct must be in (0, 1.0], got " + std::to_string(max_position_pct));
        valid = false;
    }
    
    if (min_cad_required_pct < 0 || min_cad_required_pct > 0.5) {
        LOG_ERROR("Config: min_cad_required_pct must be in [0, 0.5], got " + std::to_string(min_cad_required_pct));
        valid = false;
    }

    if (partial_tp_pct < 0 || partial_tp_pct > 1.0) {
        LOG_ERROR("Config: partial_tp_pct must be in [0, 1.0], got " + std::to_string(partial_tp_pct));
        valid = false;
    }

    if (partial_tp_sell_pct < 0 || partial_tp_sell_pct > 1.0) {
        LOG_ERROR("Config: partial_tp_sell_pct must be in [0, 1.0], got " + std::to_string(partial_tp_sell_pct));
        valid = false;
    }

    if (trailing_stop_pct < 0 || trailing_stop_pct > 1.0) {
        LOG_ERROR("Config: trailing_stop_pct must be in [0, 1.0], got " + std::to_string(trailing_stop_pct));
        valid = false;
    }

    if (max_hold_seconds < 0) {
        LOG_ERROR("Config: max_hold_seconds must be >= 0, got " + std::to_string(max_hold_seconds));
        valid = false;
    }

    if (tp_atr_mult <= 0 || sl_atr_mult <= 0) {
        LOG_ERROR("Config: tp_atr_mult and sl_atr_mult must be > 0");
        valid = false;
    }
    
    if (poll_interval_seconds < 1) {
        LOG_ERROR("Config: poll_interval_seconds must be >= 1, got " + std::to_string(poll_interval_seconds));
        valid = false;
    }
    
    if (cooldown_seconds < 0) {
        LOG_ERROR("Config: cooldown_seconds must be >= 0, got " + std::to_string(cooldown_seconds));
        valid = false;
    }
    
    if (max_trades_per_day < 1) {
        LOG_ERROR("Config: max_trades_per_day must be >= 1, got " + std::to_string(max_trades_per_day));
        valid = false;
    }
    
    if (rate_limit_min_delay_ms < 100) {
        LOG_ERROR("Config: rate_limit_min_delay_ms must be >= 100, got " + std::to_string(rate_limit_min_delay_ms));
        valid = false;
    }
    
    if (max_consecutive_failures < 1) {
        LOG_ERROR("Config: max_consecutive_failures must be >= 1, got " + std::to_string(max_consecutive_failures));
        valid = false;
    }
    
    if (stale_price_seconds < 5) {
        LOG_ERROR("Config: stale_price_seconds must be >= 5, got " + std::to_string(stale_price_seconds));
        valid = false;
    }

    if (ui_dir.empty()) {
        LOG_ERROR("Config: ui_dir cannot be empty");
        valid = false;
    }
    
    return valid;
}

void Config::log_config() const {
    std::ostringstream oss;
    oss << "Configuration loaded:"
        << "\n  pair: " << pair
        << "\n  take_profit_pct: " << (take_profit_pct * 100) << "%"
        << "\n  stop_loss_pct: " << (stop_loss_pct * 100) << "%"
        << "\n  rebuy_reset_pct: " << (rebuy_reset_pct * 100) << "%"
        << "\n  trend_window_short: " << trend_window_short
        << "\n  trend_window_long: " << trend_window_long
        << "\n  require_trend_up: " << (require_trend_up ? "true" : "false")
        << "\n  atr_window: " << atr_window
        << "\n  min_atr_pct: " << (min_atr_pct * 100) << "%"
        << "\n  max_spread_pct: " << (max_spread_pct * 100) << "%"
        << "\n  risk_per_trade_pct: " << (risk_per_trade_pct * 100) << "%"
        << "\n  max_position_pct: " << (max_position_pct * 100) << "%"
        << "\n  min_cad_required_pct: " << (min_cad_required_pct * 100) << "%"
        << "\n  partial_tp_pct: " << (partial_tp_pct * 100) << "%"
        << "\n  partial_tp_sell_pct: " << (partial_tp_sell_pct * 100) << "%"
        << "\n  trailing_stop_pct: " << (trailing_stop_pct * 100) << "%"
        << "\n  max_hold_seconds: " << max_hold_seconds
        << "\n  use_dynamic_tp_sl: " << (use_dynamic_tp_sl ? "true" : "false")
        << "\n  tp_atr_mult: " << tp_atr_mult
        << "\n  sl_atr_mult: " << sl_atr_mult
        << "\n  poll_interval_seconds: " << poll_interval_seconds
        << "\n  cooldown_seconds: " << cooldown_seconds
        << "\n  max_trades_per_day: " << max_trades_per_day
        << "\n  dry_run: " << (dry_run ? "true" : "false")
        << "\n  sim_fee_pct_roundtrip: " << (sim_fee_pct_roundtrip * 100) << "%"
        << "\n  sim_initial_cad: " << sim_initial_cad
        << "\n  kraken_api_base: " << kraken_api_base
        << "\n  rate_limit_min_delay_ms: " << rate_limit_min_delay_ms
        << "\n  max_consecutive_failures: " << max_consecutive_failures
        << "\n  stale_price_seconds: " << stale_price_seconds
        << "\n  ui_dir: " << ui_dir;
    
    LOG_INFO(oss.str());
}
