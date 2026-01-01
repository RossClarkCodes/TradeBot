#ifndef KRAKEN_CLIENT_HPP
#define KRAKEN_CLIENT_HPP

#include <string>
#include <optional>
#include <map>
#include <chrono>
#include <mutex>

// Result types for API responses
struct TickerResult {
    bool success = false;
    std::string error;
    double last_price = 0.0;
    int64_t timestamp = 0;  // Unix epoch seconds when fetched
};

struct BalanceResult {
    bool success = false;
    std::string error;
    double cad_balance = 0.0;
    double btc_balance = 0.0;  // XBT in Kraken terminology
};

struct OrderResult {
    bool success = false;
    std::string error;
    std::string txid;
    double avg_price = 0.0;
    double volume = 0.0;
    double fee = 0.0;
    std::string status;
};

class KrakenClient {
public:
    KrakenClient(const std::string& api_base, int64_t min_delay_ms);
    
    // Initialize with API credentials
    bool init();
    
    // Public API - Ticker
    TickerResult get_ticker(const std::string& pair);
    
    // Private API - Balance
    BalanceResult get_balance();
    
    // Private API - Place market order
    OrderResult place_market_order(const std::string& pair, const std::string& side, double volume);
    
    // Private API - Query order status
    OrderResult query_order(const std::string& txid);
    
    // Set exponential backoff parameters
    void set_backoff_params(int max_retries, int64_t initial_backoff_ms, int64_t max_backoff_ms);
    
    // Get consecutive failure count
    int get_consecutive_failures() const { return consecutive_failures_; }
    
    // Reset consecutive failures
    void reset_failures() { consecutive_failures_ = 0; }

private:
    // HTTP request helpers
    std::string http_get(const std::string& url);
    std::string http_post(const std::string& url, const std::string& postdata, 
                          const std::map<std::string, std::string>& headers);
    
    // Kraken authentication
    std::string generate_signature(const std::string& uri_path, const std::string& nonce, 
                                   const std::string& postdata);
    
    // Rate limiting
    void enforce_rate_limit();
    void apply_backoff();
    
    // API credentials
    std::string api_key_;
    std::string api_secret_;
    std::string api_base_;
    
    // Rate limiting
    int64_t min_delay_ms_;
    std::chrono::steady_clock::time_point last_request_time_;
    std::mutex request_mutex_;
    
    // Backoff
    int max_retries_ = 3;
    int64_t initial_backoff_ms_ = 1000;
    int64_t max_backoff_ms_ = 30000;
    int consecutive_failures_ = 0;
    int64_t current_backoff_ms_ = 0;
    
    // Initialization flag
    bool initialized_ = false;
};

#endif // KRAKEN_CLIENT_HPP

