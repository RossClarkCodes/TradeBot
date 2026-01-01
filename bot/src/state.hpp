#ifndef STATE_HPP
#define STATE_HPP

#include <string>
#include <optional>
#include <cstdint>

enum class TradingMode {
    FLAT,
    LONG
};

std::string mode_to_string(TradingMode mode);
TradingMode string_to_mode(const std::string& str);

struct TradingState {
    TradingMode mode = TradingMode::FLAT;
    std::optional<double> entry_price;
    std::optional<double> exit_price;
    std::optional<double> trailing_stop_price;
    double btc_amount = 0.0;
    std::optional<int64_t> last_trade_time;  // Unix epoch seconds
    std::optional<int64_t> entry_time;       // Unix epoch seconds
    int trades_today = 0;
    std::string trades_date_yyyy_mm_dd;
    bool partial_take_profit_done = false;
    
    // Simulated balances (only used in dry-run mode)
    double sim_cad_balance = 0.0;
    double sim_btc_balance = 0.0;
    
    // Load state from JSON file
    static TradingState load(const std::string& path);
    
    // Save state to JSON file
    void save(const std::string& path) const;
    
    // Initialize default state
    static TradingState default_state();
    
    // Reset trades_today if date has changed
    void check_date_rollover();
    
    // Check if in cooldown period
    bool is_in_cooldown(int64_t cooldown_seconds) const;
    
    // Get seconds remaining in cooldown
    int64_t cooldown_remaining(int64_t cooldown_seconds) const;
    
    // Log current state
    void log_state() const;
};

#endif // STATE_HPP
