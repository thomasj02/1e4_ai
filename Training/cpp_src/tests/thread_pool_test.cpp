#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <numeric>
#include <thread>
#include <random>
#include <atomic>
#include <future>
#include "../utils/thread_pool.hpp"

class ThreadPoolTest : public testing::Test {
protected:
    // Helper function to create a thread pool with default parameters
    static std::unique_ptr<ThreadPool> createPool(size_t threads = 4) {
        return std::make_unique<ThreadPool>(threads);
    }
    
    // Helper function to simulate CPU-bound work
    static int cpuBoundTask(int input, int milliseconds = 10) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Simulate CPU work
        volatile int sum = 0;
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::high_resolution_clock::now() - start).count() < milliseconds) {
            for (int i = 0; i < 1000; ++i) {
                sum += input + i;
            }
        }
        
        return input * 2;
    }
    
    // Helper function to simulate I/O-bound work
    static int ioBoundTask(int input, int milliseconds = 50) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        return input * 3;
    }
};

// Test basic functionality: creating tasks and getting results
TEST_F(ThreadPoolTest, BasicFunctionality) {
    auto pool = createPool(2);
    
    // Submit a simple task
    auto result1 = pool->enqueue([] { return 42; });
    
    // Submit a task with arguments
    auto result2 = pool->enqueue([](int x, int y){ return x + y; }, 10, 20);
    
    // Verify results
    EXPECT_EQ(result1.get(), 42);
    EXPECT_EQ(result2.get(), 30);
}

// We skip testing error handling for enqueuing on a stopped pool
// since we can't directly access the private 'stop' member
// This would require modifying the ThreadPool class which is beyond the scope of these tests

// Test task cancellation via futures
TEST_F(ThreadPoolTest, TaskCancellation) {
    auto pool = createPool(2);
    
    // Create a task that just sleeps a bit
    auto task_future = pool->enqueue([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 42;
    });
    
    // Don't wait for the task, just destroy the pool
    // This tests that we can destroy a pool with pending tasks
    pool.reset();
    
    // Since the task is canceled or finished with the pool,
    // trying to get the result may throw, or it might return the value
    // depending on the implementation. We don't make assertions here.
}

// Test waiting for all tasks to complete
TEST_F(ThreadPoolTest, WaitAll) {
    auto pool = createPool(4);
    std::atomic counter{0};
    
    // Submit several tasks
    constexpr int num_tasks = 20;
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(pool->enqueue([&counter, i]{
            std::this_thread::sleep_for(std::chrono::milliseconds(i * 5));
            ++counter;
        }));
    }
    
    // Wait for all tasks to complete
    pool->wait_all();
    
    // Verify that all tasks have completed
    EXPECT_EQ(counter, num_tasks);
    
    // Verify that all futures are ready
    for (auto& future : futures) {
        EXPECT_TRUE(future.valid());
        EXPECT_EQ(future.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    }
}

// Test with many tasks: stress test the thread pool
TEST_F(ThreadPoolTest, ManyTasks) {
    auto pool = createPool(4);
    
    // Submit many tasks
    constexpr int num_tasks = 1000;
    std::vector<std::future<int>> futures;
    
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(pool->enqueue([i]{ 
            return i; 
        }));
    }
    
    // Verify results
    for (int i = 0; i < num_tasks; ++i) {
        EXPECT_EQ(futures[i].get(), i);
    }
}

// Test with mixed CPU and I/O bound tasks
TEST_F(ThreadPoolTest, MixedTasks) {
    auto pool = createPool(4);
    
    // Submit a mix of CPU-bound and I/O-bound tasks
    constexpr int num_tasks = 20;
    std::vector<std::future<int>> futures;
    
    for (int i = 0; i < num_tasks; ++i) {
        if (i % 2 == 0) {
            // CPU-bound task
            futures.push_back(pool->enqueue(cpuBoundTask, i, 10));
        } else {
            // I/O-bound task
            futures.push_back(pool->enqueue(ioBoundTask, i, 20));
        }
    }
    
    // Verify results
    for (int i = 0; i < num_tasks; ++i) {
        if (i % 2 == 0) {
            EXPECT_EQ(futures[i].get(), i * 2);
        } else {
            EXPECT_EQ(futures[i].get(), i * 3);
        }
    }
}

// Test task exceptions are properly propagated
TEST_F(ThreadPoolTest, TaskExceptions) {
    auto pool = createPool(2);
    
    // Submit a task that throws an exception
    auto future = pool->enqueue([]{ 
        throw std::runtime_error("Test exception");
    });
    
    // Verify the exception is propagated
    EXPECT_THROW({
        future.get();
    }, std::runtime_error);
    
    // Ensure the pool still works after a task threw an exception
    auto future2 = pool->enqueue([]{ return 84; });
    EXPECT_EQ(future2.get(), 84);
}

// 1) Nested task executes inline when pool size == 1 (would dead‑lock without
//    re‑entrancy)
TEST_F(ThreadPoolTest, ReentrancySingleThreadPool) {
    auto pool = createPool(1);          // only one worker

    /* outer task enqueues an inner task and blocks on it */
    auto outer = pool->enqueue([&pool] {
        auto inner = pool->enqueue([] { return 7; });
        return inner.get() * 6;         // should be 42
    });

    EXPECT_EQ(outer.get(), 42);         // finishes without dead‑lock
}

// 2) Outer task enqueues MANY subtasks, then waits on them – checks that the
//    inline‑execution path keeps the pool alive (no starvation)
TEST_F(ThreadPoolTest, ReentrancyManyNestedTasks) {
    constexpr int N = 50;
    auto pool = createPool(2);          // fewer threads than subtasks

    auto outer = pool->enqueue([&pool] {
        std::vector<std::future<int>> subs;
        for (int i = 0; i < N; ++i) {
            subs.push_back(pool->enqueue([i] { return i; }));
        }
        int sum = 0;
        for (auto& f : subs) sum += f.get();
        return sum;                     // 0 + … + 49 = 1225
    });

    EXPECT_EQ(outer.get(), (N - 1) * N / 2);
}

// 3) Nested task calls pool->wait_all() from inside a worker thread – previous
//    implementations would dead‑lock; re‑entrant version must succeed.
TEST_F(ThreadPoolTest, WaitAllInsideWorker) {
    auto pool = createPool(2);

    auto outer = pool->enqueue([&pool] {
        std::atomic counter{0};

        // launch subtasks on the same pool
        constexpr int K = 30;
        for (int i = 0; i < K; ++i) {
            pool->enqueue([&counter] { ++counter; });
        }

        /*‑‑ wait_all() is called while still inside pool – relies on
            nested tasks executing inline or on the other worker ‑‑*/
        pool->wait_all();
        return counter.load();
    });

    EXPECT_EQ(outer.get(), 30);
}

// Test that the thread pool can handle move-only types
// This tests that lambdas capturing std::unique_ptr and other move-only types work correctly
TEST_F(ThreadPoolTest, MoveOnlyTypeSupport) {
    auto pool = createPool(4);

    // Test 1: Lambda capturing a unique_ptr
    auto ptr = std::make_unique<int>(42);
    auto future1 = pool->enqueue([ptr = std::move(ptr)] {
        return *ptr;
    });
    EXPECT_EQ(future1.get(), 42);

    // Test 2: Function with unique_ptr argument
    auto future2 = pool->enqueue([](const std::unique_ptr<int>& p) {
        return *p * 2;
    }, std::make_unique<int>(21));
    EXPECT_EQ(future2.get(), 42);

    // Test 3: Multiple move-only arguments
    auto future3 = pool->enqueue([](const std::unique_ptr<int>& p1, const std::unique_ptr<int>& p2) {
        return *p1 + *p2;
    }, std::make_unique<int>(10), std::make_unique<int>(32));
    EXPECT_EQ(future3.get(), 42);

    // Test 4: Lambda capturing multiple unique_ptrs
    auto ptr1 = std::make_unique<int>(20);
    auto ptr2 = std::make_unique<int>(22);
    auto future4 = pool->enqueue([p1 = std::move(ptr1), p2 = std::move(ptr2)] {
        return *p1 + *p2;
    });
    EXPECT_EQ(future4.get(), 42);
}
