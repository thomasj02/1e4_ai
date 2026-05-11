#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <barrier>

// Simple test to verify seq_cst memory ordering prevents reordering issues
TEST(MemoryOrderingTest, SequentialConsistencyBasic) {
    // This test verifies that with seq_cst, all threads see atomic operations
    // in the same order, preventing the kind of race conditions that could
    // cause data loss in the shuffle manager
    
    std::atomic x{0};
    std::atomic y{0};
    std::atomic z1{0};
    std::atomic z2{0};
    
    std::thread t1([&] {
        x.store(1, std::memory_order_seq_cst);
        int r1 = y.load(std::memory_order_seq_cst);
        z1.store(r1, std::memory_order_seq_cst);
    });
    
    std::thread t2([&] {
        y.store(1, std::memory_order_seq_cst);
        int r2 = x.load(std::memory_order_seq_cst);
        z2.store(r2, std::memory_order_seq_cst);
    });
    
    t1.join();
    t2.join();
    
    // With seq_cst, it's impossible for both z1 and z2 to be 0
    // At least one thread must see the other's write
    EXPECT_FALSE(z1.load() == 0 && z2.load() == 0)
        << "Sequential consistency violation: both threads missed each other's writes";
}

// Test simulating the producer-consumer pattern with memory ordering
TEST(MemoryOrderingTest, ProducerConsumerOrdering) {
    constexpr int num_iterations = 1000;

    for (int iter = 0; iter < num_iterations; ++iter) {
        constexpr int num_producers = 4;
        std::atomic active_producers{num_producers};
        std::atomic producers_done{false};
        std::atomic items_produced{0};
        std::atomic items_consumed{0};
        std::atomic consumer_saw_done{false};
        
        // Barrier to ensure all threads start together
        std::barrier sync_point(num_producers + 1);
        
        // Producer threads
        std::vector<std::thread> producers;
        for (int i = 0; i < num_producers; ++i) {
            producers.emplace_back([&] {
                sync_point.arrive_and_wait();
                
                // Produce some items
                for (int j = 0; j < 10; ++j) {
                    items_produced.fetch_add(1, std::memory_order_seq_cst);
                }
                
                // Decrement active producers

                // Last producer sets done flag
                if (int remaining = active_producers.fetch_sub(1, std::memory_order_seq_cst); remaining == 1) {
                    producers_done.store(true, std::memory_order_seq_cst);
                }
            });
        }
        
        // Consumer thread
        std::thread consumer([&] {
            sync_point.arrive_and_wait();
            
            // Keep consuming until producers are done
            while (!producers_done.load(std::memory_order_seq_cst)) {
                // Simulate consuming by checking items_produced
                if (int produced = items_produced.load(std::memory_order_seq_cst); produced > items_consumed.load(std::memory_order_seq_cst)) {
                    items_consumed.fetch_add(1, std::memory_order_seq_cst);
                }
            }
            
            // Consume any remaining items after producers_done is true
            consumer_saw_done.store(true, std::memory_order_seq_cst);
            int final_produced = items_produced.load(std::memory_order_seq_cst);
            while (items_consumed.load(std::memory_order_seq_cst) < final_produced) {
                items_consumed.fetch_add(1, std::memory_order_seq_cst);
            }
        });
        
        // Wait for all threads
        for (auto& t : producers) {
            t.join();
        }
        consumer.join();
        
        // Verify all items were consumed
        int produced = items_produced.load(std::memory_order_seq_cst);
        int consumed = items_consumed.load(std::memory_order_seq_cst);
        
        EXPECT_EQ(produced, num_producers * 10)
            << "Iteration " << iter << ": Expected " << (num_producers * 10) 
            << " items produced, got " << produced;
            
        EXPECT_EQ(consumed, produced)
            << "Iteration " << iter << ": Not all items were consumed. "
            << "Produced: " << produced << ", Consumed: " << consumed;
            
        EXPECT_TRUE(consumer_saw_done.load())
            << "Iteration " << iter << ": Consumer didn't see producers_done flag";
    }
}

// Test that demonstrates the importance of proper memory ordering
TEST(MemoryOrderingTest, MemoryOrderingImportance) {
    // This test shows how seq_cst prevents the race condition where
    // the writer thread might not see all enqueued records

    constexpr int num_runs = 100;
    int issues_found = 0;
    
    for (int run = 0; run < num_runs; ++run) {
        std::atomic flag{false};
        std::atomic data{0};
        bool consumer_saw_data = false;
        
        std::thread producer([&] {
            // Write data first
            data.store(42, std::memory_order_seq_cst);
            // Then set flag
            flag.store(true, std::memory_order_seq_cst);
        });
        
        std::thread consumer([&] {
            // Wait for flag
            while (!flag.load(std::memory_order_seq_cst)) {
                std::this_thread::yield();
            }
            // Flag is true, data must be visible with seq_cst
            if (data.load(std::memory_order_seq_cst) == 42) {
                consumer_saw_data = true;
            }
        });
        
        producer.join();
        consumer.join();
        
        if (!consumer_saw_data) {
            issues_found++;
        }
    }
    
    // With seq_cst, we should never have visibility issues
    EXPECT_EQ(issues_found, 0)
        << "Found " << issues_found << " runs where consumer didn't see producer's data. "
        << "This would cause lost records in the shuffle manager.";
}

