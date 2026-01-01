#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <cstdint>

struct Config {
    // Trading pair (Kraken API format: XXBT=BTC, ZCAD=CAD)
    std::string pair = "XXBTZCAD";
    
    // Strategy parameters
    double take_profit_pct = 0.015;       // 1.5%
    double stop_loss_pct = 0.006;         // 0.6%
    double rebuy_reset_pct = 0.006;       // Price must drop by this % from exit before re-entry

    // Trend/volatility filters
    int trend_window_short = 20;
    int trend_window_long = 50;
    bool require_trend_up = true;
    int atr_window = 14;
    double min_atr_pct = 0.003;           // 0.3% minimum volatility
    double max_spread_pct = 0.002;        // 0.2% max bid-ask spread for entries
    
    // Position sizing
    double risk_per_trade_pct = 0.01;     // 1% of equity risked per trade
    double max_position_pct = 0.90;       // Max 90% of equity in a position
    double min_cad_required_pct = 0.02;   // 2% of equity reserved for fees/buffer

    // Exit management
    double partial_tp_pct = 0.01;         // Partial take-profit at +1.0%
    double partial_tp_sell_pct = 0.5;     // Sell 50% on partial take-profit
    double trailing_stop_pct = 0.004;     // Trailing stop 0.4% below peak
    int64_t max_hold_seconds = 3600;      // Max 1 hour hold
    bool use_dynamic_tp_sl = true;
    double tp_atr_mult = 2.0;
    double sl_atr_mult = 1.2;
    
    // Timing
    int64_t poll_interval_seconds = 5;
    int64_t cooldown_seconds = 600;       // 10 minutes
    int max_trades_per_day = 3;
    
    // Execution mode
    bool dry_run = true;
    double sim_fee_pct_roundtrip = 0.004; // 0.4% simulated round-trip fee
    double sim_initial_cad = 1000.0;      // Initial simulated equity
    
    // API configuration
    std::string kraken_api_base = "https://api.kraken.com";
    int64_t rate_limit_min_delay_ms = 500;
    int max_consecutive_failures = 10;
    int64_t stale_price_seconds = 30;
    
    // File paths (relative to working directory)
    std::string state_file = "state.json";
    std::string kill_switch_file = "KILL_SWITCH";
    std::string log_dir = "logs";
    std::string ui_dir = "ui";
    
    // Load from JSON file
    static Config load(const std::string& path);
    
    // Validate configuration
    bool validate() const;
    
    // Log current configuration
    void log_config() const;
};

#endif // CONFIG_HPP
