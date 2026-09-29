//--------------------------------------------------------------------------------------
// UVAtlas - uvatlas_timing.h
//
// Optional, local-only phase timing instrumentation used to profile UVAtlas.
// Enable by defining UVATLAS_ENABLE_TIMING when building the library.
//--------------------------------------------------------------------------------------

#pragma once

#if defined(UVATLAS_ENABLE_TIMING)

#include <chrono>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace uvatlas_timing
{
    inline int& indent()
    {
        static int value = 0;
        return value;
    }

    struct ScopeTimer
    {
        const char* label;
        uint64_t work;
        std::chrono::steady_clock::time_point start;

        explicit ScopeTimer(const char* label_, uint64_t work_ = 0) noexcept
            : label(label_), work(work_), start(std::chrono::steady_clock::now())
        {
            ++indent();
            std::fprintf(stderr, "[uvatlas] %*s>>> %-42s (work=%llu) begin\n",
                (indent() - 1) * 2, "", label,
                static_cast<unsigned long long>(work_));
        }

        ~ScopeTimer()
        {
            const auto end = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(end - start).count();
            const int level = --indent();
            std::fprintf(stderr, "[uvatlas] %*s%-44s %10.2f ms", level * 2, "", label, ms);
            if (work != 0)
            {
                std::fprintf(stderr, "  (work=%llu, %.3f us/unit)",
                    static_cast<unsigned long long>(work),
                    work ? (ms * 1000.0 / static_cast<double>(work)) : 0.0);
            }
            std::fprintf(stderr, "\n");
        }
    };
}

#define UVATLAS_CONCAT_IMPL(a, b) a##b
#define UVATLAS_CONCAT(a, b) UVATLAS_CONCAT_IMPL(a, b)
#define UVATLAS_TIME_SCOPE(label) \
    const uvatlas_timing::ScopeTimer UVATLAS_CONCAT(_uvas_scope_timer_, __LINE__) (label)
#define UVATLAS_TIME_SCOPE_WORK(label, work) \
    const uvatlas_timing::ScopeTimer UVATLAS_CONCAT(_uvas_scope_timer_, __LINE__) (label, static_cast<uint64_t>(work))

#else
#define UVATLAS_TIME_SCOPE(label) ((void)0)
#define UVATLAS_TIME_SCOPE_WORK(label, work) ((void)0)
#endif
