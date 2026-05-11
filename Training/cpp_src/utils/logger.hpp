#pragma once

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"
#include <memory>
#include <string>
#include <chrono>
#include <fmt/format.h>

namespace chessmimic {

// Global logger instance
inline quill::Logger* g_quill_logger = nullptr;

class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };
    
    explicit Logger(Level level = Level::INFO) : level_(level) {
        // Initialize Quill backend once
        static bool quill_initialized = false;
        if (!quill_initialized) {
            // Configure backend options
            quill::BackendOptions backend_options;
            backend_options.thread_name = "Logging Thread";
            backend_options.sleep_duration = std::chrono::milliseconds(10);
            quill::Backend::start(backend_options);
            quill_initialized = true;
        }
        
        // Create console sink
        auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console_sink");
        
        // Configure pattern formatter options to show source location
        quill::PatternFormatterOptions formatter_options;
        formatter_options.format_pattern = "%(time) [%(thread_id)] %(short_source_location:<28) LOG_%(log_level:<9) %(logger:<12) %(message)";
        formatter_options.timestamp_pattern = "%H:%M:%S.%Qns";
        
        // Create logger with custom format to show source location
        quill_logger_ = quill::Frontend::create_or_get_logger(
            "chessmimic", 
            std::move(console_sink),
            formatter_options
        );
        
        // Set the global logger
        g_quill_logger = quill_logger_;
        
        // Set log level
        setLevel(level);
    }
    
    void setLevel(Level level) {
        level_ = level;
        
        // Map our levels to Quill levels
        switch (level) {
            case Level::DEBUG:
                quill_logger_->set_log_level(quill::LogLevel::Debug);
                break;
            case Level::INFO:
                quill_logger_->set_log_level(quill::LogLevel::Info);
                break;
            case Level::WARNING:
                quill_logger_->set_log_level(quill::LogLevel::Warning);
                break;
            case Level::ERROR:
                quill_logger_->set_log_level(quill::LogLevel::Error);
                break;
        }
    }
    
    // Get the quill logger for macro use
    quill::Logger* getQuillLogger() const { return quill_logger_; }

    // Legacy methods for backward compatibility
    template <typename... Args>
    void log(const std::string& format, Args&&... args) {
        info(format, std::forward<Args>(args)...);
    }
    
    void log(const std::string& message) const {
        info(message);
    }
    
    template <typename... Args>
    void debug(const std::string& format, Args&&... args) {
        if (level_ <= Level::DEBUG) {
            std::string msg = fmt::format(fmt::runtime(format), std::forward<Args>(args)...);
            LOG_DEBUG(quill_logger_, "{}", msg);
        }
    }
    
    void debug(const std::string& message) const {
        if (level_ <= Level::DEBUG) {
            LOG_DEBUG(quill_logger_, "{}", message);
        }
    }
    
    template <typename... Args>
    void info(const std::string& format, Args&&... args) {
        if (level_ <= Level::INFO) {
            std::string msg = fmt::format(fmt::runtime(format), std::forward<Args>(args)...);
            LOG_INFO(quill_logger_, "{}", msg);
        }
    }
    
    void info(const std::string& message) const {
        if (level_ <= Level::INFO) {
            LOG_INFO(quill_logger_, "{}", message);
        }
    }
    
    template <typename... Args>
    void warning(const std::string& format, Args&&... args) {
        if (level_ <= Level::WARNING) {
            std::string msg = fmt::format(fmt::runtime(format), std::forward<Args>(args)...);
            LOG_WARNING(quill_logger_, "{}", msg);
        }
    }
    
    void warning(const std::string& message) const {
        if (level_ <= Level::WARNING) {
            LOG_WARNING(quill_logger_, "{}", message);
        }
    }
    
    template <typename... Args>
    void error(const std::string& format, Args&&... args) {
        std::string msg = fmt::format(fmt::runtime(format), std::forward<Args>(args)...);
        LOG_ERROR(quill_logger_, "{}", msg);
    }
    
    void error(const std::string& message) const {
        LOG_ERROR(quill_logger_, "{}", message);
    }

private:
    Level level_ = Level::INFO;
    quill::Logger* quill_logger_ = nullptr;
};

// Helper function to get the global quill logger
inline quill::Logger* get_quill_logger() {
    if (g_quill_logger == nullptr) {
        // Create a default logger if none exists
        static Logger default_logger(Logger::Level::INFO);
        g_quill_logger = default_logger.getQuillLogger();
    }
    return g_quill_logger;
}

} // namespace chessmimic

// Global logger instance for compatibility
inline chessmimic::Logger g_logger;

// Macros for direct logging that will show correct source location
#define CHESSMIMIC_LOG_DEBUG(fmt, ...) QUILL_LOG_DEBUG(chessmimic::get_quill_logger(), fmt, ##__VA_ARGS__)
#define CHESSMIMIC_LOG_INFO(fmt, ...) QUILL_LOG_INFO(chessmimic::get_quill_logger(), fmt, ##__VA_ARGS__)
#define CHESSMIMIC_LOG_WARNING(fmt, ...) QUILL_LOG_WARNING(chessmimic::get_quill_logger(), fmt, ##__VA_ARGS__)
#define CHESSMIMIC_LOG_ERROR(fmt, ...) QUILL_LOG_ERROR(chessmimic::get_quill_logger(), fmt, ##__VA_ARGS__)

// Short aliases for convenience
#define CM_LOG_DEBUG(fmt, ...) CHESSMIMIC_LOG_DEBUG(fmt, ##__VA_ARGS__)
#define CM_LOG_INFO(fmt, ...) CHESSMIMIC_LOG_INFO(fmt, ##__VA_ARGS__)
#define CM_LOG_WARNING(fmt, ...) CHESSMIMIC_LOG_WARNING(fmt, ##__VA_ARGS__)
#define CM_LOG_ERROR(fmt, ...) CHESSMIMIC_LOG_ERROR(fmt, ##__VA_ARGS__)
