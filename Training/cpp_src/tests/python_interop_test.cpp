#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "core/bagz.hpp"
#include <filesystem>
#include <algorithm>
#include <random>
#include <fstream>
#include <iostream>
#include <cstdlib>

using namespace chessmimic;
using namespace testing;

class BagzPythonInteropTest : public Test {
protected:
    void SetUp() override {
        // Create a temporary test file in the build directory
        m_test_file = std::filesystem::temp_directory_path() / "cpp_to_python_test.bagz";
        
        // Remove any existing test file
        std::filesystem::remove(m_test_file);
    }

    void TearDown() override {
        // Clean up test files
        std::filesystem::remove(m_test_file);
    }

    // Helper to generate random byte vectors
    static std::vector<uint8_t> generateRandomData(size_t size) {
        std::vector<uint8_t> data(size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution distrib(0, 255);
        
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(distrib(gen));
        }
        
        return data;
    }

    std::filesystem::path m_test_file;
};

// Test that the C++ BagWriter creates files that can be read by the C++ BagFileReader
TEST_F(BagzPythonInteropTest, CppWriterCppReader) {
    // Create test data with various sizes of records
    std::vector<std::vector<uint8_t>> test_data;
    
    // Add some small records
    test_data.push_back({1, 2, 3, 4, 5});
    test_data.push_back({10, 20, 30, 40, 50});
    
    // Add a medium-sized record
    test_data.push_back(generateRandomData(100));  // Smaller for faster testing
    
    // Add an empty record (for Python compatibility)
    test_data.push_back(std::vector<uint8_t>());
    
    // Write the data using C++ BagWriter
    {
        BagWriter writer(m_test_file.string());
        for (const auto& record : test_data) {
            writer.write(record);
        }
        writer.close();
    }
    
    // Read the data back using C++ BagFileReader
    {
        BagFileReader reader(m_test_file.string());
        
        // Check the number of records
        std::cout << "C++ BagFileReader found " << reader.size() << " records" << std::endl;
        EXPECT_EQ(reader.size(), test_data.size()) << "C++ reader found wrong number of records";
        
        // Check each record
        for (size_t i = 0; i < reader.size(); ++i) {
            auto read_record = reader.get_record(i);
            std::cout << "  Record " << i << ": " << read_record.size() << " bytes" << std::endl;
            
            EXPECT_EQ(read_record.size(), test_data[i].size()) 
                << "Record " << i << " has wrong size";
                
            if (read_record.size() <= 10) {
                std::cout << "  Content: ";
                for (uint8_t byte : read_record) {
                    std::cout << static_cast<int>(byte) << " ";
                }
                std::cout << std::endl;
            }
            
            // Check content equality for small records
            if (read_record.size() <= 100) {
                EXPECT_TRUE(std::equal(read_record.begin(), read_record.end(), test_data[i].begin()))
                    << "Record " << i << " content doesn't match";
            }
        }
    }
}

// Test that the C++ BagWriter creates files that can be read by the Python BagFileReader
TEST_F(BagzPythonInteropTest, CppWriterPythonReader) {
    // Create test data with various sizes of records
    std::vector<std::vector<uint8_t>> test_data;
    
    // Add some small records
    test_data.push_back({1, 2, 3, 4, 5});
    test_data.push_back({10, 20, 30, 40, 50});
    
    // Add a medium-sized record
    test_data.push_back(generateRandomData(1000));
    
    // Add a larger record
    test_data.push_back(generateRandomData(5000));
    
    // Add an empty record (for Python compatibility) 
    test_data.push_back(std::vector<uint8_t>());
    
    // Print record info for debugging
    std::cout << "Input test data size: " << test_data.size() << " records" << std::endl;
    for (size_t i = 0; i < test_data.size(); ++i) {
        std::cout << "  Record " << i << ": " << test_data[i].size() << " bytes" << std::endl;
    }
    
    // Save record sizes for verification
    std::vector<size_t> record_sizes;
    for (const auto& record : test_data) {
        record_sizes.push_back(record.size());
    }
    
    // Write the data using C++ BagWriter
    {
        BagWriter writer(m_test_file.string());
        for (const auto& record : test_data) {
            writer.write(record);
        }
        writer.close();
    }
    
    // Write a Python script file
    std::string python_script_path = (std::filesystem::temp_directory_path() / "test_cpp_python_interop.py").string();
    std::ofstream script_file(python_script_path);
    
    script_file << "import sys\n"
                << "import os\n"
                << "import importlib.util\n\n"
                << "# Check for required dependencies\n"
                << "try:\n"
                << "    import zstandard\n"
                << "    print('Found required package: zstandard')\n"
                << "except ImportError as e:\n"
                << "    print(f'Error: zstandard module not found: {e}')\n"
                << "    print('This is required by bagz.py. Please run this test within the virtual environment.')\n"
                << "    sys.exit(1)\n\n"
                << "try:\n"
                << "    from etils import epath\n"
                << "    print('Found required package: etils')\n"
                << "except ImportError as e:\n"
                << "    print(f'Error: etils module not found: {e}')\n"
                << "    print('This is required by bagz.py. Please run this test within the virtual environment.')\n"
                << "    sys.exit(1)\n\n"
                << "try:\n"
                << "    import typing_extensions\n"
                << "    print('Found required package: typing_extensions')\n"
                << "except ImportError as e:\n"
                << "    print(f'Error: typing_extensions module not found: {e}')\n"
                << "    print('This is required by bagz.py. Please run this test within the virtual environment.')\n"
                << "    sys.exit(1)\n\n"
                << "# Find the bagz.py file relative to the build directory\n"
                << "bagz_path = os.path.abspath(os.path.join(os.getcwd(), '..', 'bagz.py'))\n"
                << "print(f'Looking for bagz.py at: {bagz_path}')\n\n"
                << "if not os.path.exists(bagz_path):\n"
                << "    print(f'Error: bagz.py not found at {bagz_path}')\n"
                << "    sys.exit(1)\n\n"
                << "# Load the bagz module directly from the file\n"
                << "spec = importlib.util.spec_from_file_location('bagz', bagz_path)\n"
                << "bagz = importlib.util.module_from_spec(spec)\n"
                << "spec.loader.exec_module(bagz)\n"
                << "print('Successfully loaded bagz module from:', bagz_path)\n\n"
                << "test_file = '" << m_test_file.string() << "'\n\n"
                << "try:\n"
                << "    reader = bagz.BagFileReader(test_file)\n"
                << "    num_records = len(reader)\n"
                << "    print(f'Number of records: {num_records}')\n\n"
                << "    # Print out record sizes for verification\n"
                << "    record_sizes = [len(reader[i]) for i in range(num_records)]\n"
                << "    for i, size in enumerate(record_sizes):\n"
                << "        print(f'Record {i} size: {size} bytes')\n\n"
                << "    # For small records, print out their content to verify\n"
                << "    for i in range(num_records):\n"
                << "        record = reader[i]\n"
                << "        if len(record) < 100:\n"
                << "            print(f'Record {i} content: {list(record)}')\n\n"
                << "    print('Successfully read all records from bagz file')\n"
                << "except Exception as e:\n"
                << "    print(f'Error: {e}')\n";
    
    script_file.close();
    
    // Run the Python script using the repo venv Python
    std::string venv_python = "../../.venv/bin/python";
    std::string command = venv_python + " " + python_script_path;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        ADD_FAILURE() << "Failed to run Python script";
        return;
    }
    
    char buffer[128];
    std::string python_output;
    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr) {
            python_output += buffer;
        }
    }
    pclose(pipe);
    
    // Clean up the script file
    std::filesystem::remove(python_script_path);
    
    std::cout << "Python script output: \n" << python_output << std::endl;
    
    // Print out info about expected records for debugging
    std::cout << "Expecting " << test_data.size() << " records in the bagz file" << std::endl;
    for (size_t i = 0; i < record_sizes.size(); ++i) {
        std::cout << "  Expected record " << i << ": " << record_sizes[i] << " bytes" << std::endl;
    }
    
    // Verify the Python script found the correct number of records
    std::string expected_records_msg = "Number of records: " + std::to_string(test_data.size());
    EXPECT_TRUE(python_output.find(expected_records_msg) != std::string::npos)
        << "Python reader found wrong number of records. Expected message: '" << expected_records_msg
        << "' not found in output: " << python_output;
    
    // Check if the "Successfully read all records" message is present
    EXPECT_TRUE(python_output.find("Successfully read all records from bagz file") != std::string::npos)
        << "Python failed to read all records from the bagz file";
    
    // Verify each record size
    for (size_t i = 0; i < record_sizes.size(); ++i) {
        std::string expected_size_message = "Record " + std::to_string(i) + " size: " + std::to_string(record_sizes[i]) + " bytes";
        EXPECT_TRUE(python_output.find(expected_size_message) != std::string::npos)
            << "Record " << i << " has wrong size when read by Python";
    }
}
