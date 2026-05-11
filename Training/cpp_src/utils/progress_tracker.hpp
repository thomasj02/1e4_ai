#pragma once

#include <mutex>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include "utils/logger.hpp"

class ProgressTracker {
public:
    ProgressTracker(size_t total, std::string operation) 
        : total(total), completed(0), operation(std::move(operation)), 
          start_time(std::time(nullptr)) {}
    
    void update(size_t new_completed = 1) {
        std::lock_guard lock(mutex);
        completed += new_completed;
        
        // Calculate ETA and display progress
        double progress = static_cast<double>(completed) / total;
        time_t now = std::time(nullptr);
        time_t elapsed = now - start_time;
        
        if (completed == total || completed % std::max<size_t>(total / 100, 1) == 0) {
            time_t eta = progress > 0 ? (elapsed / progress - elapsed) : 0;
            CM_LOG_INFO("{}: {}/{} ({}%) ETA: {}", 
                        operation, completed, total, 
                        static_cast<int>(progress * 100), 
                        format_time(eta));
        }
    }
    
private:
    static std::string format_time(time_t seconds) {
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        int secs = seconds % 60;
        
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(2) << hours << ":"
           << std::setfill('0') << std::setw(2) << minutes << ":"
           << std::setfill('0') << std::setw(2) << secs;
        return ss.str();
    }

    std::mutex mutex;
    size_t total;
    size_t completed;
    std::string operation;
    time_t start_time;
};