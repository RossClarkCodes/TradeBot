#ifndef STRATEGY_HPP
#define STRATEGY_HPP

#include "config.hpp"
#include "state.hpp"
#include "kraken_client.hpp"
#include <string>
#include <optional>
#include <deque>

enum class Decision {
    NOOP,
    BUY,
    SELL,
    BLOCKED
};

std::string decision_to_string(Decision d);

struct PositionSizing {
    double equity_cad = 0.0;
    double available_cad = 0.0;
    double risk_cad = 0.0;
    double raw_position_cad = 0.0;
    double max_position_cad = 0.0;
    double position_cad = 0.0;
    double fee_buffer_cad = 0.0;
    double btc_to_buy = 0.0;
    bool can_trade = false;
    std::string block_reason;
};

struct TradeContext {
    double current_price = 0.0;
    int64_t price_timestamp = 0;
    bool price_stale = false;
    double bid_price = 0.0;
    double ask_price = 0.0;
    double spread_pct = 0.0;
    double atr = 0.0;
    double sma_short = 0.0;
    double sma_long = 0.0;
    
    double tp_price = 0.0;
    double sl_price = 0.0;
    double rebuy_price = 0.0;
    
    PositionSizing sizing;
    
    Decision decision = Decision::NOOP;
    std::string decision_reason;
    double sell_volume = 0.0;
    bool is_partial_exit = false;
    
    // For logging
    void log() const;
};

class Strategy {
public:
    Strategy(const Config& config, TradingState& state, KrakenClient& client);
    
    // Main evaluation function - returns decision and context
    TradeContext evaluate();
    
    // Execute a decision (BUY or SELL)
    bool execute(const TradeContext& ctx);
    
    // For dry-run mode: initialize simulated balances
    void init_simulation(double initial_cad);

private:
    // Fetch current price
    bool fetch_price(TradeContext& ctx);

    // Update indicators (SMA, ATR, spread)
    void update_indicators(TradeContext& ctx);
    bool passes_trend_filter(TradeContext& ctx) const;
    bool passes_volatility_filter(TradeContext& ctx) const;
    
    // Calculate position sizing
    void calculate_sizing(TradeContext& ctx);
    
    // Determine if entry conditions are met (FLAT mode)
    bool check_entry_condition(TradeContext& ctx);
    
    // Determine if exit conditions are met (LONG mode)
    bool check_exit_condition(TradeContext& ctx);
    
    // Check all blocking conditions (cooldown, max trades, etc.)
    bool check_blocking_conditions(TradeContext& ctx);

    // Check market conditions (spread, trend, volatility)
    bool check_market_conditions(TradeContext& ctx);
    
    // Execute buy order
    bool execute_buy(const TradeContext& ctx);
    
    // Execute sell order  
    bool execute_sell(const TradeContext& ctx);
    
    // Simulate a fill (dry-run mode)
    void simulate_fill(const std::string& side, double btc_amount, double price);
    
    // Wait for order fill confirmation
    bool wait_for_fill(const std::string& txid, OrderResult& out_result, int max_attempts = 10);

    const Config& config_;
    TradingState& state_;
    KrakenClient& client_;
    std::deque<double> price_history_;
    std::deque<double> tr_history_;
};

#endif // STRATEGY_HPP
