#pragma once

#include <vector>
#include <queue>
#include <thread>                 // std::jthread
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>             // std::move_only_function
#include <utility>                // std::scope_exit
#include <atomic>
#include <stdexcept>
#include <experimental/scope>

/*-------------------------------------------------------------------------*/
/*  REENTRANCY SUPPORT                                                     */
/*-------------------------------------------------------------------------*/
/* Every worker thread stores a pointer to the pool it currently belongs to.
   `enqueue()` checks this TLS value; if it matches `this` the task is run
   inline and the returned future is already ready. */
inline thread_local class ThreadPool* tls_current_pool = nullptr;
/*-------------------------------------------------------------------------*/

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threads) {
        workers_.reserve(threads);
        for (std::size_t i = 0; i < threads; ++i) {
            workers_.emplace_back(
                [this](std::stop_token stoken) {
                    tls_current_pool = this; // mark TLS

                    while (true) {
                        Task task;
                        {
                            std::unique_lock lk(queue_mtx_);
                            cv_.wait(lk, stoken,
                                     [this, &stoken] {
                                         return stoken.stop_requested() ||
                                             !tasks_.empty();
                                     });
                            if (stoken.stop_requested() && tasks_.empty())
                                return;

                            task = std::move(tasks_.front());
                            tasks_.pop();
                        }

                        active_.fetch_add(1, std::memory_order_relaxed);

                        std::experimental::scope_exit decrement{
                            [this] {
                                active_.fetch_sub(1, std::memory_order_relaxed);
                                std::lock_guard lk(queue_mtx_);
                                cv_.notify_all();
                            }
                        };
                        task(); // may throw
                    }
                });
        }
    }

    /* number of threads in the pool */
    std::size_t thread_count() const noexcept {
        return workers_.size();
    }

    ~ThreadPool() {
        wait_all(); // finish in-flight work
        for (auto& t : workers_) t.request_stop();
        {
            std::lock_guard lk(queue_mtx_);
            cv_.notify_all(); // wake sleepers so they see stop
        }
        /* jthread auto-joins in its destructor */
    }

    /*---------------- enqueue (now re-entrant) ----------------*/
    template <class F, class... Args>
        requires std::invocable<F, Args...>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;
        auto pkg = std::make_shared<std::packaged_task<R()>>(
            [f = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable -> R {
                return std::invoke(std::move(f), std::move(args)...);
            });

        std::future<R> fut = pkg->get_future();

        /* If we’re already *inside* this pool, run the task inline.   */
        if (tls_current_pool == this) {
            (*pkg)(); // execute immediately; finishes before we return
            return fut; // already ready
        }

        /* Normal path – queue the task for a worker */
        {
            std::lock_guard lk(queue_mtx_);
            if (stopped_) {
                throw std::runtime_error("ThreadPool stopped");
            }
            tasks_.emplace([pkg = std::move(pkg)] {
                (*pkg)();
            });
        }
        cv_.notify_one();
        return fut;
    }

    /*---------------- synchronisation helpers ----------------*/
    void wait_all() {
        std::unique_lock lk(queue_mtx_);
        cv_.wait(lk, [this] {
            int act = active_.load(std::memory_order_relaxed);
            /* If *this* thread is inside the pool, ‘act’ always counts us.
               So “no other work” means (tasks empty && act == 1). */
            if (tls_current_pool == this && act == 1)
                act = 0;
            return tasks_.empty() && act == 0;
        });
        std::ignore = 1;
    }

private:
    using Task = std::move_only_function<void()>;

    /* thread-pool state -------------------------------------- */
    std::vector<std::jthread> workers_;
    std::queue<Task> tasks_;

    std::mutex queue_mtx_;
    std::condition_variable_any cv_;
    std::atomic<int> active_{0};
    bool stopped_{false};
};
