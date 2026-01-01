#include "kraken_client.hpp"
#include "logger.hpp"
#include "util.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>
#include <cstdlib>

using json = nlohmann::json;

// Curl write callback
static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

KrakenClient::KrakenClient(const std::string& api_base, int64_t min_delay_ms)
    : api_base_(api_base)
    , min_delay_ms_(min_delay_ms)
    , last_request_time_(std::chrono::steady_clock::now() - std::chrono::milliseconds(min_delay_ms))
    , current_backoff_ms_(0) {
}

bool KrakenClient::init() {
    // Get API credentials from environment
    const char* key = std::getenv("KRAKEN_API_KEY");
    const char* secret = std::getenv("KRAKEN_API_SECRET");
    
    if (key == nullptr || secret == nullptr) {
        LOG_WARNING("KRAKEN_API_KEY or KRAKEN_API_SECRET environment variables not set");
        LOG_WARNING("Private API endpoints will not be available");
        initialized_ = false;
        return false;
    }
    
    api_key_ = key;
    api_secret_ = secret;
    
    if (api_key_.empty() || api_secret_.empty()) {
        LOG_WARNING("KRAKEN_API_KEY or KRAKEN_API_SECRET is empty");
        initialized_ = false;
        return false;
    }
    
    initialized_ = true;
    LOG_INFO("Kraken client initialized with API credentials");
    return true;
}

void KrakenClient::enforce_rate_limit() {
    std::lock_guard<std::mutex> lock(request_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_request_time_).count();
    
    int64_t total_delay = min_delay_ms_ + current_backoff_ms_;
    
    if (elapsed < total_delay) {
        int64_t sleep_ms = total_delay - elapsed;
        LOG_DEBUG("Rate limiting: sleeping " + std::to_string(sleep_ms) + "ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    
    last_request_time_ = std::chrono::steady_clock::now();
}

void KrakenClient::apply_backoff() {
    consecutive_failures_++;
    
    if (current_backoff_ms_ == 0) {
        current_backoff_ms_ = initial_backoff_ms_;
    } else {
        current_backoff_ms_ = std::min(current_backoff_ms_ * 2, max_backoff_ms_);
    }
    
    // Add jitter (10-50% of backoff)
    int64_t jitter = util::random_jitter_ms(current_backoff_ms_ / 2);
    current_backoff_ms_ += jitter;
    
    LOG_WARNING("Applying backoff: " + std::to_string(current_backoff_ms_) + 
                "ms (consecutive failures: " + std::to_string(consecutive_failures_) + ")");
}

void KrakenClient::set_backoff_params(int max_retries, int64_t initial_backoff_ms, int64_t max_backoff_ms) {
    max_retries_ = max_retries;
    initial_backoff_ms_ = initial_backoff_ms;
    max_backoff_ms_ = max_backoff_ms;
}

std::string KrakenClient::http_get(const std::string& url) {
    enforce_rate_limit();
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to initialize curl");
        apply_backoff();
        return "";
    }
    
    std::string response;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "KrakenTradingBot/1.0");
    
    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        LOG_ERROR("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
        curl_easy_cleanup(curl);
        apply_backoff();
        return "";
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    
    if (http_code != 200) {
        LOG_ERROR("HTTP error: " + std::to_string(http_code));
        apply_backoff();
        return "";
    }
    
    // Reset backoff on success
    consecutive_failures_ = 0;
    current_backoff_ms_ = 0;
    
    return response;
}

std::string KrakenClient::http_post(const std::string& url, const std::string& postdata,
                                     const std::map<std::string, std::string>& headers) {
    enforce_rate_limit();
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        LOG_ERROR("Failed to initialize curl");
        apply_backoff();
        return "";
    }
    
    std::string response;
    
    struct curl_slist* header_list = nullptr;
    for (const auto& [key, value] : headers) {
        std::string header = key + ": " + value;
        header_list = curl_slist_append(header_list, header.c_str());
    }
    header_list = curl_slist_append(header_list, "Content-Type: application/x-www-form-urlencoded");
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "KrakenTradingBot/1.0");
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(header_list);
    
    if (res != CURLE_OK) {
        LOG_ERROR("curl_easy_perform() failed: " + std::string(curl_easy_strerror(res)));
        curl_easy_cleanup(curl);
        apply_backoff();
        return "";
    }
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    
    if (http_code != 200) {
        LOG_ERROR("HTTP error: " + std::to_string(http_code));
        apply_backoff();
        return "";
    }
    
    // Reset backoff on success
    consecutive_failures_ = 0;
    current_backoff_ms_ = 0;
    
    return response;
}

std::string KrakenClient::generate_signature(const std::string& uri_path, const std::string& nonce,
                                              const std::string& postdata) {
    // Kraken signature: 
    // HMAC-SHA512 of (URI path + SHA256(nonce + POST data)) using base64-decoded secret
    
    std::string sha256_input = nonce + postdata;
    std::string sha256_hash = util::sha256_raw(sha256_input);
    
    std::string hmac_input = uri_path + sha256_hash;
    std::string decoded_secret = util::base64_decode(api_secret_);
    
    return util::hmac_sha512_raw(decoded_secret, hmac_input);
}

TickerResult KrakenClient::get_ticker(const std::string& pair) {
    TickerResult result;
    
    std::string url = api_base_ + "/0/public/Ticker?pair=" + pair;
    LOG_DEBUG("Fetching ticker: " + url);
    
    std::string response = http_get(url);
    if (response.empty()) {
        result.error = "Empty response from Kraken";
        return result;
    }
    
    try {
        json j = json::parse(response);
        
        if (j.contains("error") && j["error"].is_array() && !j["error"].empty()) {
            result.error = "";
            for (const auto& err : j["error"]) {
                result.error += err.get<std::string>() + "; ";
            }
            LOG_ERROR("Kraken ticker error: " + result.error);
            apply_backoff();
            return result;
        }
        
        if (!j.contains("result") || j["result"].empty()) {
            result.error = "No result in ticker response";
            apply_backoff();
            return result;
        }
        
        // Get the first (and should be only) result
        auto& res = j["result"];
        for (auto it = res.begin(); it != res.end(); ++it) {
            // "c" is the last trade closed array [price, lot volume]
            if (it.value().contains("c") && it.value()["c"].is_array() && !it.value()["c"].empty()) {
                result.last_price = std::stod(it.value()["c"][0].get<std::string>());
                result.timestamp = util::now_epoch_seconds();
                result.success = true;
                LOG_DEBUG("Ticker " + pair + ": " + std::to_string(result.last_price));
                return result;
            }
        }
        
        result.error = "Could not parse last price from ticker response";
        apply_backoff();
        
    } catch (const json::exception& e) {
        result.error = "JSON parse error: " + std::string(e.what());
        LOG_ERROR(result.error);
        apply_backoff();
    } catch (const std::exception& e) {
        result.error = "Parse error: " + std::string(e.what());
        LOG_ERROR(result.error);
        apply_backoff();
    }
    
    return result;
}

BalanceResult KrakenClient::get_balance() {
    BalanceResult result;
    
    if (!initialized_) {
        result.error = "API credentials not initialized";
        return result;
    }
    
    std::string uri_path = "/0/private/Balance";
    std::string url = api_base_ + uri_path;
    
    std::string nonce = util::generate_nonce();
    std::string postdata = "nonce=" + nonce;
    
    std::string signature = generate_signature(uri_path, nonce, postdata);
    
    std::map<std::string, std::string> headers;
    headers["API-Key"] = api_key_;
    headers["API-Sign"] = signature;
    
    LOG_DEBUG("Fetching balance...");
    
    std::string response = http_post(url, postdata, headers);
    if (response.empty()) {
        result.error = "Empty response from Kraken";
        return result;
    }
    
    try {
        json j = json::parse(response);
        
        if (j.contains("error") && j["error"].is_array() && !j["error"].empty()) {
            result.error = "";
            for (const auto& err : j["error"]) {
                result.error += err.get<std::string>() + "; ";
            }
            LOG_ERROR("Kraken balance error: " + result.error);
            apply_backoff();
            return result;
        }
        
        if (!j.contains("result")) {
            result.error = "No result in balance response";
            apply_backoff();
            return result;
        }
        
        auto& res = j["result"];
        
        // CAD balance (might be ZCAD or CAD depending on Kraken's convention)
        if (res.contains("ZCAD")) {
            result.cad_balance = std::stod(res["ZCAD"].get<std::string>());
        } else if (res.contains("CAD")) {
            result.cad_balance = std::stod(res["CAD"].get<std::string>());
        }
        
        // BTC balance (XBT in Kraken terminology, might be XXBT or XBT)
        if (res.contains("XXBT")) {
            result.btc_balance = std::stod(res["XXBT"].get<std::string>());
        } else if (res.contains("XBT")) {
            result.btc_balance = std::stod(res["XBT"].get<std::string>());
        }
        
        result.success = true;
        LOG_INFO("Balance: CAD=" + std::to_string(result.cad_balance) + 
                 ", XBT=" + std::to_string(result.btc_balance));
        
    } catch (const json::exception& e) {
        result.error = "JSON parse error: " + std::string(e.what());
        LOG_ERROR(result.error);
        apply_backoff();
    } catch (const std::exception& e) {
        result.error = "Parse error: " + std::string(e.what());
        LOG_ERROR(result.error);
        apply_backoff();
    }
    
    return result;
}

OrderResult KrakenClient::place_market_order(const std::string& pair, const std::string& side, double volume) {
    OrderResult result;
    
    if (!initialized_) {
        result.error = "API credentials not initialized";
        return result;
    }
    
    std::string uri_path = "/0/private/AddOrder";
    std::string url = api_base_ + uri_path;
    
    std::string nonce = util::generate_nonce();
    
    // Format volume to 8 decimal places for BTC
    std::ostringstream vol_ss;
    vol_ss << std::fixed << std::setprecision(8) << volume;
    std::string volume_str = vol_ss.str();
    
    std::string postdata = "nonce=" + nonce +
                          "&ordertype=market" +
                          "&type=" + side +
                          "&volume=" + volume_str +
                          "&pair=" + pair;
    
    std::string signature = generate_signature(uri_path, nonce, postdata);
    
    std::map<std::string, std::string> headers;
    headers["API-Key"] = api_key_;
    headers["API-Sign"] = signature;
    
    LOG_INFO("Placing market " + side + " order: " + volume_str + " " + pair);
    
    std::string response = http_post(url, postdata, headers);
    if (response.empty()) {
        result.error = "Empty response from Kraken";
        return result;
    }
    
    try {
        json j = json::parse(response);
        
        if (j.contains("error") && j["error"].is_array() && !j["error"].empty()) {
            result.error = "";
            for (const auto& err : j["error"]) {
                result.error += err.get<std::string>() + "; ";
            }
            LOG_ERROR("Kraken order error: " + result.error);
            apply_backoff();
            return result;
        }
        
        if (!j.contains("result")) {
            result.error = "No result in order response";
            apply_backoff();
            return result;
        }
        
        auto& res = j["result"];
        
        // Get transaction ID
        if (res.contains("txid") && res["txid"].is_array() && !res["txid"].empty()) {
            result.txid = res["txid"][0].get<std::string>();
        } else {
            result.error = "No txid in order response";
            return result;
        }
        
        result.success = true;
        LOG_INFO("Order placed successfully, txid: " + result.txid);
        
        // For market orders, Kraken may not return immediate fill details
        // We need to query the order to get fill information
        
    } catch (const json::exception& e) {
        result.error = "JSON parse error: " + std::string(e.what());
        LOG_ERROR(result.error);
        apply_backoff();
    } catch (const std::exception& e) {
        result.error = "Parse error: " + std::string(e.what());
        LOG_ERROR(result.error);
        apply_backoff();
    }
    
    return result;
}

OrderResult KrakenClient::query_order(const std::string& txid) {
    OrderResult result;
    
    if (!initialized_) {
        result.error = "API credentials not initialized";
        return result;
    }
    
    std::string uri_path = "/0/private/QueryOrders";
    std::string url = api_base_ + uri_path;
    
    std::string nonce = util::generate_nonce();
    std::string postdata = "nonce=" + nonce + "&txid=" + txid + "&trades=true";
    
    std::string signature = generate_signature(uri_path, nonce, postdata);
    
    std::map<std::string, std::string> headers;
    headers["API-Key"] = api_key_;
    headers["API-Sign"] = signature;
    
    LOG_DEBUG("Querying order: " + txid);
    
    std::string response = http_post(url, postdata, headers);
    if (response.empty()) {
        result.error = "Empty response from Kraken";
        return result;
    }
    
    try {
        json j = json::parse(response);
        
        if (j.contains("error") && j["error"].is_array() && !j["error"].empty()) {
            result.error = "";
            for (const auto& err : j["error"]) {
                result.error += err.get<std::string>() + "; ";
            }
            LOG_ERROR("Kraken query order error: " + result.error);
            apply_backoff();
            return result;
        }
        
        if (!j.contains("result")) {
            result.error = "No result in query response";
            apply_backoff();
            return result;
        }
        
        auto& res = j["result"];
        
        if (!res.contains(txid)) {
            result.error = "Order not found: " + txid;
            return result;
        }
        
        auto& order = res[txid];
        
        result.txid = txid;
        result.status = order.value("status", "unknown");
        
        // Get executed volume
        if (order.contains("vol_exec")) {
            result.volume = std::stod(order["vol_exec"].get<std::string>());
        }
        
        // Get average price
        if (order.contains("price")) {
            result.avg_price = std::stod(order["price"].get<std::string>());
        }
        
        // Get fee
        if (order.contains("fee")) {
            result.fee = std::stod(order["fee"].get<std::string>());
        }
        
        // Check if order is closed/filled
        if (result.status == "closed") {
            result.success = true;
            LOG_INFO("Order " + txid + " filled: vol=" + std::to_string(result.volume) + 
                     ", avg_price=" + std::to_string(result.avg_price) + 
                     ", fee=" + std::to_string(result.fee));
        } else if (result.status == "canceled" || result.status == "expired") {
            result.error = "Order was " + result.status;
        } else {
            // Order still pending or partially filled
            result.success = false;
            LOG_INFO("Order " + txid + " status: " + result.status);
        }
        
    } catch (const json::exception& e) {
        result.error = "JSON parse error: " + std::string(e.what());
        LOG_ERROR(result.error);
        apply_backoff();
    } catch (const std::exception& e) {
        result.error = "Parse error: " + std::string(e.what());
        LOG_ERROR(result.error);
        apply_backoff();
    }
    
    return result;
}

