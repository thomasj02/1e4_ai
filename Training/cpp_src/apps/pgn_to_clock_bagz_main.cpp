#include <iostream>
#include <exception>
#include "clock_converter/clock_data_converter.hpp"

int main(int argc, char* argv[]) {
    try {
        // Parse command line arguments
        auto config = chessmimic::ClockDataConverter::parseArguments(argc, argv);
        
        // Validate configuration
        chessmimic::ClockDataConverter::validateConfig(config);
        
        // Create and run converter
        chessmimic::ClockDataConverter converter(config);
        converter.convert();
        
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}