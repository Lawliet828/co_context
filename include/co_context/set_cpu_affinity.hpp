#pragma once

#ifdef USE_CPU_AFFINITY // see config.hpp

#include <sched.h>

namespace co_context {

namespace detail {

    inline void set_cpu_affinity(int cpu) {
        ::cpu_set_t cpu_set;
        CPU_SET(cpu, &cpu_set);
        int ret = sched_setaffinity(gettid(), sizeof(cpu_set_t), &cpu_set);
        if (ret != 0) [[unlikely]]
            throw std::system_error{
                errno, std::system_category(), "sched_setaffinity"};
    }

} // namespace detail

} // namespace co_context

#endif USE_CPU_AFFINITY
