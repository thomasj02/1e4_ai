#include "winner_converter/winner_data_converter.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    chessmimic::winner_converter::WinnerDataConverter::Config config;
    
    if (!chessmimic::winner_converter::WinnerDataConverter::parseArguments(argc, argv, config)) {
        return EXIT_FAILURE;
    }
    
    try {
        // Create and run converter
        if (chessmimic::winner_converter::WinnerDataConverter converter(config); converter.convert()) {
            std::cout << "Conversion completed successfully!" << std::endl;
            return EXIT_SUCCESS;
        } else {
            std::cerr << "Conversion failed!" << std::endl;
            return EXIT_FAILURE;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}