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
    
    // Position sizing
    double risk_per_trade_pct = 0.01;     // 1% of equity risked per trade
    double max_position_pct = 0.90;       // Max 90% of equity in a position
    double min_cad_required_pct = 0.02;   // 2% of equity reserved for fees/buffer
    
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
    
    // Load from JSON file
    static Config load(const std::string& path);
    
    // Validate configuration
    bool validate() const;
    
    // Log current configuration
    void log_config() const;
};

#endif // CONFIG_HPP

