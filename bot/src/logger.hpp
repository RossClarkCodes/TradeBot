#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <fstream>
#include <mutex>
#include <memory>

class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    static Logger& instance();
    
    void init(const std::string& log_dir = "logs", const std::string& log_filename = "bot.log");
    void set_level(Level level);
    
    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warning(const std::string& msg);
    void error(const std::string& msg);
    
    void log(Level level, const std::string& msg);

private:
    Logger() = default;
    ~Logger();
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void write(Level level, const std::string& msg);
    std::string level_to_string(Level level) const;
    void ensure_log_dir(const std::string& log_dir);

    std::ofstream file_;
    std::mutex mutex_;
    Level min_level_ = Level::INFO;
    bool initialized_ = false;
};

// Convenience macros
#define LOG_DEBUG(msg) Logger::instance().debug(msg)
#define LOG_INFO(msg) Logger::instance().info(msg)
#define LOG_WARNING(msg) Logger::instance().warning(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)

#endif // LOGGER_HPP

