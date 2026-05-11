#pragma once

#include <string>
#include <chrono>
#include <iostream>
#include <map>
#include <mutex>
#include <iomanip>

namespace chessmimic {

/**
 * A simple stopwatch class for timing code execution.
 * Provides both individual timing and hierarchical timing capabilities.
 */
class Stopwatch {
public:
    // Constructor
    explicit Stopwatch(const std::string& name = "Total") : 
        name_(name), 
        start_time_(std::chrono::high_resolution_clock::now()),
        running_(true),
        elapsed_(0) {
    }

    // Start or restart the stopwatch
    void start() {
        std::lock_guard lock(mutex_);
        if (!running_) {
            start_time_ = std::chrono::high_resolution_clock::now();
            running_ = true;
        }
    }

    // Stop the stopwatch
    void stop() {
        std::lock_guard lock(mutex_);
        if (running_) {
            auto end_time = std::chrono::high_resolution_clock::now();
            elapsed_ += std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - start_time_).count();
            running_ = false;
        }
    }

    // Reset the stopwatch
    void reset() {
        std::lock_guard lock(mutex_);
        start_time_ = std::chrono::high_resolution_clock::now();
        elapsed_ = 0;
        running_ = true;
    }

    // Get elapsed time in seconds
    double getElapsedSeconds() const {
        std::lock_guard lock(mutex_);
        if (running_) {
            auto now = std::chrono::high_resolution_clock::now();
            auto current = elapsed_ + std::chrono::duration_cast<std::chrono::microseconds>(
                now - start_time_).count();
            return current / 1000000.0;
        }
        return elapsed_ / 1000000.0;
    }

    // Get elapsed time in milliseconds
    double getElapsedMilliseconds() const {
        return getElapsedSeconds() * 1000.0;
    }

    // Get elapsed time as formatted string (e.g., "3.45 s" or "245 ms")
    std::string getElapsedString() const {
        double seconds = getElapsedSeconds();
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        
        if (seconds < 0.001) {
            ss << (seconds * 1000000.0) << " μs";
        } else if (seconds < 1.0) {
            ss << (seconds * 1000.0) << " ms";
        } else {
            ss << seconds << " s";
        }
        
        return ss.str();
    }

    // Get the name of this stopwatch
    const std::string& getName() const {
        return name_;
    }

private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;
    bool running_;
    int64_t elapsed_;  // Stored in microseconds
    mutable std::mutex mutex_;
};

/**
 * Global stopwatch manager to track multiple named stopwatches.
 */
class StopwatchManager {
public:
    static StopwatchManager& getInstance() {
        static StopwatchManager instance;
        return instance;
    }

    // Start a named stopwatch (creates if doesn't exist)
    void start(const std::string& name) {
        std::lock_guard lock(mutex_);
        if (auto it = stopwatches_.find(name); it == stopwatches_.end()) {
            stopwatches_[name] = std::make_shared<Stopwatch>(name);
        } else {
            it->second->start();
        }
    }

    // Stop a named stopwatch
    void stop(const std::string& name) {
        std::lock_guard lock(mutex_);
        if (auto it = stopwatches_.find(name); it != stopwatches_.end()) {
            it->second->stop();
        }
    }

    // Reset a named stopwatch
    void reset(const std::string& name) {
        std::lock_guard lock(mutex_);
        if (auto it = stopwatches_.find(name); it != stopwatches_.end()) {
            it->second->reset();
        }
    }

    // Get elapsed time for a named stopwatch in seconds
    double getElapsedSeconds(const std::string& name) const {
        std::lock_guard lock(mutex_);
        if (auto it = stopwatches_.find(name); it != stopwatches_.end()) {
            return it->second->getElapsedSeconds();
        }
        return 0.0;
    }

    // Print report of all stopwatches
    void printReport(std::ostream& os = std::cout) const {
        std::lock_guard lock(mutex_);
        
        os << "\n===== Performance Timing Report =====\n";
        
        // Calculate column widths for nice formatting
        size_t maxNameWidth = 0;
        size_t maxTimeWidth = 0;
        
        for (const auto& [fst, snd] : stopwatches_) {
            maxNameWidth = std::max(maxNameWidth, fst.length());
            maxTimeWidth = std::max(maxTimeWidth, snd->getElapsedString().length());
        }
        
        // Print header
        os << std::left << std::setw(maxNameWidth + 2) << "Operation"
           << std::right << std::setw(maxTimeWidth + 2) << "Time" 
           << "  % of Total\n";
        os << std::string(maxNameWidth + maxTimeWidth + 15, '-') << "\n";
        
        // Find the "Total" timer if it exists
        double totalTime = 0.0;
        if (auto it = stopwatches_.find("Total"); it != stopwatches_.end()) {
            totalTime = it->second->getElapsedSeconds();
        }
        
        // If no "Total" timer, use the sum of all timers
        if (totalTime == 0.0) {
            for (const auto& val: stopwatches_ | std::views::values) {
                totalTime += val->getElapsedSeconds();
            }
        }
        
        // Print each timer
        for (const auto& [fst, snd] : stopwatches_) {
            double time = snd->getElapsedSeconds();
            double percentage = totalTime > 0 ? time / totalTime * 100.0 : 0.0;
            
            os << std::left << std::setw(maxNameWidth + 2) << fst
               << std::right << std::setw(maxTimeWidth + 2) << snd->getElapsedString();
               
            if (fst != "Total") {
                os << std::right << std::setw(7) << std::fixed << std::setprecision(1) << percentage << "%";
            } else {
                os << std::right << std::setw(7) << "100.0%";
            }
            
            os << "\n";
        }
        
        os << "====================================\n";
    }

    // Create a scoped timer that automatically stops when it goes out of scope
    class ScopedTimer {
    public:
        explicit ScopedTimer(const std::string& name) : name_(name) {
            getInstance().start(name_);
        }
        
        ~ScopedTimer() {
            getInstance().stop(name_);
        }
        
    private:
        std::string name_;
    };

private:
    StopwatchManager() = default;
    ~StopwatchManager() = default;
    
    // Prevent copy construction and assignment
    StopwatchManager(const StopwatchManager&) = delete;
    StopwatchManager& operator=(const StopwatchManager&) = delete;
    
    std::map<std::string, std::shared_ptr<Stopwatch>> stopwatches_;
    mutable std::mutex mutex_;
};

// Convenient global functions
inline void startTimer(const std::string& name) {
    StopwatchManager::getInstance().start(name);
}

inline void stopTimer(const std::string& name) {
    StopwatchManager::getInstance().stop(name);
}

inline void resetTimer(const std::string& name) {
    StopwatchManager::getInstance().reset(name);
}

inline void printTimingReport(std::ostream& os = std::cout) {
    StopwatchManager::getInstance().printReport(os);
}

// Macro for creating a scoped timer
#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)

#define SCOPED_TIMER(name) \
    chessmimic::StopwatchManager::ScopedTimer CONCAT(scoped_timer_, __LINE__)(name)

} // namespace chessmimic