// Camada de plataforma do runtime: o que muda entre o Windows (PC) e o Android/Linux (POSIX).
// Threads do host, memória alinhada, espera, relógio e pequenas diferenças da biblioteca C.
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

namespace platform
{
    // thread do host com pilha grande (o código recompilado aninha uma chamada do host por chamada do jogo)
    using ThreadEntry = void (*)(void* arg);
    void* StartThread(ThreadEntry entry, void* arg, size_t stackSize);
    bool ThreadRunning(void* thread);

    void* AlignedAlloc(size_t size, size_t alignment);
    void SleepMs(uint32_t ms);
    void YieldThread();
    [[noreturn]] void ExitCurrentThread();   // termina a thread do host atual (ExTerminateThread)

    // pausa curta dentro de um laço de espera ativa (spinlock)
    inline void CpuRelax()
    {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        __asm__ __volatile__("yield");
#endif
    }

    // abre um arquivo por std::filesystem::path (no Windows, com o nome em UTF-16)
    template<class Path>
    inline FILE* OpenFile(const Path& path, const char* mode)
    {
#ifdef _WIN32
        wchar_t wideMode[8] = {};
        for (int i = 0; i < 7 && mode[i]; i++)
            wideMode[i] = wchar_t(mode[i]);
        return _wfopen(path.c_str(), wideMode);
#else
        return fopen(path.c_str(), mode);
#endif
    }

    // relógio monotônico em nanossegundos
    inline uint64_t NowNs()
    {
        return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
}
