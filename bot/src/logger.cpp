#include "logger.hpp"
#include "util.hpp"
#include <iostream>
#include <filesystem>
#include <iomanip>

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (file_.is_open()) {
        file_.close();
    }
}

void Logger::ensure_log_dir(const std::string& log_dir) {
    std::filesystem::path dir_path(log_dir);
    if (!std::filesystem::exists(dir_path)) {
        std::filesystem::create_directories(dir_path);
    }
}

void Logger::init(const std::string& log_dir, const std::string& log_filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return;
    }
    
    ensure_log_dir(log_dir);
    
    std::string log_path = log_dir + "/" + log_filename;
    file_.open(log_path, std::ios::app);
    
    if (!file_.is_open()) {
        std::cerr << "ERROR: Failed to open log file: " << log_path << std::endl;
    }
    
    initialized_ = true;
}

void Logger::set_level(Level level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

std::string Logger::level_to_string(Level level) const {
    switch (level) {
        case Level::DEBUG:   return "DEBUG";
        case Level::INFO:    return "INFO";
        case Level::WARNING: return "WARNING";
        case Level::ERROR:   return "ERROR";
        default:             return "UNKNOWN";
    }
}

void Logger::write(Level level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (level < min_level_) {
        return;
    }
    
    std::string timestamp = util::now_iso8601();
    std::string level_str = level_to_string(level);
    
    std::ostringstream oss;
    oss << "[" << timestamp << "] [" << std::setw(7) << level_str << "] " << msg;
    std::string formatted = oss.str();
    
    // Write to console
    if (level == Level::ERROR) {
        std::cerr << formatted << std::endl;
    } else {
        std::cout << formatted << std::endl;
    }
    
    // Write to file
    if (file_.is_open()) {
        file_ << formatted << std::endl;
        file_.flush();
    }
}

void Logger::debug(const std::string& msg) {
    write(Level::DEBUG, msg);
}

void Logger::info(const std::string& msg) {
    write(Level::INFO, msg);
}

void Logger::warning(const std::string& msg) {
    write(Level::WARNING, msg);
}

void Logger::error(const std::string& msg) {
    write(Level::ERROR, msg);
}

void Logger::log(Level level, const std::string& msg) {
    write(level, msg);
}

