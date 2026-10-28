// Implementação da camada de plataforma (ver platform.h).
#include "platform.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <malloc.h>
#else
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <atomic>
#endif

namespace platform
{
#ifdef _WIN32
    namespace
    {
        struct Start
        {
            ThreadEntry entry;
            void* arg;
        };

        DWORD WINAPI Trampoline(void* param)
        {
            Start start = *static_cast<Start*>(param);
            delete static_cast<Start*>(param);
            start.entry(start.arg);
            return 0;
        }
    }

    void* StartThread(ThreadEntry entry, void* arg, size_t stackSize)
    {
        return ::CreateThread(nullptr, stackSize, Trampoline, new Start{ entry, arg }, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    }

    bool ThreadRunning(void* thread)
    {
        DWORD code = 0;
        return GetExitCodeThread(thread, &code) && code == STILL_ACTIVE;
    }

    void* AlignedAlloc(size_t size, size_t alignment) { return _aligned_malloc(size, alignment); }
    void SleepMs(uint32_t ms) { Sleep(ms); }
    void YieldThread() { SwitchToThread(); }
    void ExitCurrentThread() { ExitThread(0); }
#else
    namespace
    {
        struct Start
        {
            ThreadEntry entry;
            void* arg;
            std::atomic<bool> running{ true };
            pthread_t thread{};
        };

        void* Trampoline(void* param)
        {
            auto* start = static_cast<Start*>(param);
            start->entry(start->arg);
            start->running = false;
            return nullptr;
        }
    }

    void* StartThread(ThreadEntry entry, void* arg, size_t stackSize)
    {
        auto* start = new Start{ entry, arg };
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, stackSize);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&start->thread, &attr, Trampoline, start) != 0)
        {
            delete start;
            start = nullptr;
        }
        pthread_attr_destroy(&attr);
        return start;   // o "handle" é o bloco de início (vive enquanto o processo vive)
    }

    bool ThreadRunning(void* thread)
    {
        return thread && static_cast<Start*>(thread)->running;
    }

    void* AlignedAlloc(size_t size, size_t alignment)
    {
        void* p = nullptr;
        return posix_memalign(&p, alignment, size) == 0 ? p : nullptr;
    }

    void SleepMs(uint32_t ms)
    {
        timespec t{ time_t(ms / 1000), long(ms % 1000) * 1000000L };
        nanosleep(&t, nullptr);
    }

    void YieldThread() { sched_yield(); }

    void ExitCurrentThread()
    {
        pthread_exit(nullptr);
    }
#endif
}
