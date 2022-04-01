/*
 *  An io_context library aimed at low-latency io, based on
 * [liburingcxx](https://github.com/Codesire-Deng/liburingcxx).
 *
 *  Copyright (C) 2022 Zifeng Deng
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <thread>
#include <vector>
#include <queue>
#include "uring.hpp"
#include "co_context/config.hpp"
#include "co_context/task_info.hpp"
#include "co_context/main_task.hpp"

namespace co_context {

using config::cache_line_size;

class io_context;
// class eager_io;
// class lazy_io;

namespace config {
    inline constexpr bool is_SQPOLL = io_uring_flags & IORING_SETUP_SQPOLL;

    inline constexpr unsigned worker_threads_number =
        total_threads_number - 1 - is_SQPOLL;
} // namespace config

namespace detail {
    class worker_meta;

    using swap_zone =
        task_info_ptr[config::worker_threads_number][config::swap_capacity];

    struct alignas(cache_line_size) thread_meta {
        io_context *ctx;
        worker_meta *worker;
        uint32_t tid;
    };

    extern thread_local thread_meta this_thread;

    struct alignas(cache_line_size) worker_meta {
        enum class worker_state : uint8_t { running, idle, blocked };
        /**
         * @brief sharing zone with main thread
         */
        struct sharing_zone {
            std::thread host_thread;
            // worker_state state; // TODO atomic?
            // int temp;
        };

        alignas(cache_line_size) sharing_zone sharing;

        struct swap_cur {
            uint16_t off = 0;
            void next() noexcept;
            bool try_find_empty(swap_zone &swap) noexcept;
            bool try_find_exist(swap_zone &swap) noexcept;
            void release(swap_zone &swap, task_info_ptr task) const noexcept;
            void
            release_relaxed(swap_zone &swap, task_info_ptr task) const noexcept;
        };
        swap_cur submit_cur;
        swap_cur reap_cur;
        std::queue<task_info *> submit_overflow_buf;

        void submit(task_info_ptr io_info) noexcept;

        std::coroutine_handle<> schedule() noexcept;

        void init(const int thread_index, io_context *const context);

        void co_spawn(main_task entrance) noexcept;

        void run(const int thread_index, io_context *const context);
    };

} // namespace detail

/*
inline constexpr uint16_t task_info_number_per_cache_line =
    cache_line_size / sizeof(detail::task_info *);
*/

class [[nodiscard]] io_context final {
  public:
    /**
     * One thread for io_context submit/reap. Another thread for io_uring (if
     * needed).
     */
    static_assert(config::worker_threads_number >= 1);

    using uring = liburingcxx::URing<config::io_uring_flags>;

    using task_info = detail::task_info;

    using task_info_ptr = detail::task_info_ptr;

    // May not necessary to read/write atomicly.
    using swap_zone =
        task_info_ptr[config::worker_threads_number][config::swap_capacity];

  private:
    alignas(cache_line_size) uring ring;
    alignas(cache_line_size) swap_zone submit_swap;
    alignas(cache_line_size) swap_zone reap_swap;

    struct ctx_swap_cur {
        uint16_t tid = 0;
        uint16_t off = 0;

        void next() noexcept;
        bool try_find_empty(const swap_zone &swap) noexcept;
        bool try_find_exist(const swap_zone &swap) noexcept;
        // release the task into the swap zone
        void release(swap_zone &swap, task_info_ptr task) const noexcept;
        void
        release_relaxed(swap_zone &swap, task_info_ptr task) const noexcept;
    };

    // place main thread's high frequency data here
    ctx_swap_cur s_cur;
    ctx_swap_cur r_cur;
    std::queue<task_info_ptr> submit_overflow_buf;
    std::queue<task_info_ptr> reap_overflow_buf;

    alignas(cache_line_size) char __cacheline_barrier[64];

  public:
    using worker_meta = detail::worker_meta;
    friend worker_meta;

  private:
    alignas(cache_line_size) worker_meta worker[config::worker_threads_number];

  private:
    bool will_stop = false;
    const unsigned ring_entries;

  private:
    void forward_task(task_info_ptr task) noexcept;

    bool try_submit(task_info_ptr task) noexcept;

    /**
     * @brief poll the submission swap zone
     * @return if submit_swap capacity might be healthy
     */
    bool poll_submission() noexcept;

    bool try_clear_submit_overflow_buf() noexcept;

    bool try_reap(task_info_ptr task) noexcept;

    /**
     * @brief poll the completion swap zone
     * @return if load exists and capacity of reap_swap might be healthy
     */
    bool poll_completion() noexcept;

    bool try_clear_reap_overflow_buf() noexcept;

    [[noreturn]] void stop() noexcept {
        log::i("ctx stopped\n");
        std::this_thread::sleep_for(std::chrono::minutes{1});
        ::exit(0);
    }

  public:
    io_context(unsigned io_uring_entries, uring::Params &io_uring_params)
        : ring(io_uring_entries, io_uring_params)
        , ring_entries(io_uring_entries) {
        init();
    }

    io_context(unsigned io_uring_entries)
        : ring(io_uring_entries), ring_entries(io_uring_entries) {
        init();
    }

    io_context(unsigned io_uring_entries, uring::Params &&io_uring_params)
        : io_context(io_uring_entries, io_uring_params) {}

    void init() noexcept;

    void probe() const;

    void make_thread_pool();

    void co_spawn(main_task entrance);

    void can_stop() noexcept { will_stop = true; }

    [[noreturn]] void run();

    inline uring &io() noexcept { return ring; }

    ~io_context() noexcept {
        for (int i = 0; i < config::worker_threads_number; ++i) {
            std::thread &t = worker[i].sharing.host_thread;
            if (t.joinable()) t.join();
        }
    }

    /**
     * ban all copying or moving
     */
    io_context(const io_context &) = delete;
    io_context(io_context &&) = delete;
    io_context &operator=(const io_context &) = delete;
    io_context &operator=(io_context &&) = delete;
};

inline void co_spawn(main_task entrance) noexcept {
    detail::this_thread.ctx->co_spawn(entrance);
}

inline void co_context_stop() noexcept {
    detail::this_thread.ctx->can_stop();
}

inline uint32_t co_get_tid() noexcept {
    return detail::this_thread.tid;
}

} // namespace co_context
