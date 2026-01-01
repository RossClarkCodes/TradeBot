#include "state.hpp"
#include "logger.hpp"
#include "util.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

std::string mode_to_string(TradingMode mode) {
    switch (mode) {
        case TradingMode::FLAT: return "FLAT";
        case TradingMode::LONG: return "LONG";
        default: return "UNKNOWN";
    }
}

TradingMode string_to_mode(const std::string& str) {
    if (str == "FLAT") return TradingMode::FLAT;
    if (str == "LONG") return TradingMode::LONG;
    LOG_WARNING("Unknown mode string: " + str + ", defaulting to FLAT");
    return TradingMode::FLAT;
}

TradingState TradingState::default_state() {
    TradingState state;
    state.mode = TradingMode::FLAT;
    state.entry_price = std::nullopt;
    state.exit_price = std::nullopt;
    state.btc_amount = 0.0;
    state.last_trade_time = std::nullopt;
    state.trades_today = 0;
    state.trades_date_yyyy_mm_dd = util::today_yyyy_mm_dd();
    state.sim_cad_balance = 0.0;
    state.sim_btc_balance = 0.0;
    return state;
}

TradingState TradingState::load(const std::string& path) {
    TradingState state = default_state();
    
    if (!util::file_exists(path)) {
        LOG_INFO("State file not found, initializing defaults: " + path);
        return state;
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARNING("Failed to open state file, initializing defaults: " + path);
        return state;
    }
    
    json j;
    try {
        file >> j;
    } catch (const json::parse_error& e) {
        LOG_ERROR("Failed to parse state JSON: " + std::string(e.what()));
        LOG_WARNING("Initializing defaults due to parse error");
        return state;
    }
    
    // Parse mode
    if (j.contains("mode") && j["mode"].is_string()) {
        state.mode = string_to_mode(j["mode"].get<std::string>());
    }
    
    // Parse entry_price
    if (j.contains("entry_price") && !j["entry_price"].is_null()) {
        state.entry_price = j["entry_price"].get<double>();
    }
    
    // Parse exit_price
    if (j.contains("exit_price") && !j["exit_price"].is_null()) {
        state.exit_price = j["exit_price"].get<double>();
    }
    
    // Parse btc_amount
    if (j.contains("btc_amount") && j["btc_amount"].is_number()) {
        state.btc_amount = j["btc_amount"].get<double>();
    }
    
    // Parse last_trade_time
    if (j.contains("last_trade_time") && !j["last_trade_time"].is_null()) {
        if (j["last_trade_time"].is_number()) {
            state.last_trade_time = j["last_trade_time"].get<int64_t>();
        } else if (j["last_trade_time"].is_string()) {
            state.last_trade_time = util::iso8601_to_epoch(j["last_trade_time"].get<std::string>());
        }
    }
    
    // Parse trades_today
    if (j.contains("trades_today") && j["trades_today"].is_number()) {
        state.trades_today = j["trades_today"].get<int>();
    }
    
    // Parse trades_date_yyyy_mm_dd
    if (j.contains("trades_date_yyyy_mm_dd") && j["trades_date_yyyy_mm_dd"].is_string()) {
        state.trades_date_yyyy_mm_dd = j["trades_date_yyyy_mm_dd"].get<std::string>();
    }
    
    // Parse simulated balances
    if (j.contains("sim_cad_balance") && j["sim_cad_balance"].is_number()) {
        state.sim_cad_balance = j["sim_cad_balance"].get<double>();
    }
    if (j.contains("sim_btc_balance") && j["sim_btc_balance"].is_number()) {
        state.sim_btc_balance = j["sim_btc_balance"].get<double>();
    }
    
    LOG_INFO("Loaded state from: " + path);
    return state;
}

void TradingState::save(const std::string& path) const {
    json j;
    
    j["mode"] = mode_to_string(mode);
    
    if (entry_price.has_value()) {
        j["entry_price"] = entry_price.value();
    } else {
        j["entry_price"] = nullptr;
    }
    
    if (exit_price.has_value()) {
        j["exit_price"] = exit_price.value();
    } else {
        j["exit_price"] = nullptr;
    }
    
    j["btc_amount"] = btc_amount;
    
    if (last_trade_time.has_value()) {
        j["last_trade_time"] = last_trade_time.value();
    } else {
        j["last_trade_time"] = nullptr;
    }
    
    j["trades_today"] = trades_today;
    j["trades_date_yyyy_mm_dd"] = trades_date_yyyy_mm_dd;
    j["sim_cad_balance"] = sim_cad_balance;
    j["sim_btc_balance"] = sim_btc_balance;
    
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open state file for writing: " + path);
        throw std::runtime_error("Failed to save state to: " + path);
    }
    
    file << j.dump(2) << std::endl;
    file.close();
    
    LOG_DEBUG("State saved to: " + path);
}

void TradingState::check_date_rollover() {
    std::string today = util::today_yyyy_mm_dd();
    if (trades_date_yyyy_mm_dd != today) {
        LOG_INFO("Date rollover detected: " + trades_date_yyyy_mm_dd + " -> " + today + ", resetting trades_today");
        trades_today = 0;
        trades_date_yyyy_mm_dd = today;
    }
}

bool TradingState::is_in_cooldown(int64_t cooldown_seconds) const {
    if (!last_trade_time.has_value()) {
        return false;
    }
    int64_t now = util::now_epoch_seconds();
    int64_t elapsed = now - last_trade_time.value();
    return elapsed < cooldown_seconds;
}

int64_t TradingState::cooldown_remaining(int64_t cooldown_seconds) const {
    if (!last_trade_time.has_value()) {
        return 0;
    }
    int64_t now = util::now_epoch_seconds();
    int64_t elapsed = now - last_trade_time.value();
    int64_t remaining = cooldown_seconds - elapsed;
    return remaining > 0 ? remaining : 0;
}

void TradingState::log_state() const {
    std::ostringstream oss;
    oss << "Current state:"
        << "\n  mode: " << mode_to_string(mode)
        << "\n  entry_price: " << (entry_price.has_value() ? std::to_string(entry_price.value()) : "null")
        << "\n  exit_price: " << (exit_price.has_value() ? std::to_string(exit_price.value()) : "null")
        << "\n  btc_amount: " << btc_amount
        << "\n  last_trade_time: " << (last_trade_time.has_value() ? util::epoch_to_iso8601(last_trade_time.value()) : "null")
        << "\n  trades_today: " << trades_today
        << "\n  trades_date: " << trades_date_yyyy_mm_dd
        << "\n  sim_cad_balance: " << sim_cad_balance
        << "\n  sim_btc_balance: " << sim_btc_balance;
    
    LOG_INFO(oss.str());
}

