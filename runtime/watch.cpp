// Watchpoint de escrita por hardware (registradores de depuração do x64) num endereço do console.
// DMC3_WATCH=<hex>: cada escrita de 4 bytes nesse endereço, por qualquer thread do jogo, é registrada
// com o valor escrito, a função do host e a pilha do jogo. A execução continua normalmente.
#include "kernel.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdlib>

namespace
{
    uint32_t WatchAddress()
    {
        static uint32_t address = [] {
            const char* env = getenv("DMC3_WATCH");
            return env ? uint32_t(strtoul(env, nullptr, 16)) & ~3u : 0u;
        }();
        return address;
    }

    std::atomic<int> hits{ 0 };
}

namespace kernel
{
    // liga o watchpoint numa thread do host (precisa suspendê-la para mudar os registradores de depuração)
    void InstallWatch(HANDLE thread)
    {
        if (!WatchAddress())
            return;
        SuspendThread(thread);
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        GetThreadContext(thread, &c);
        c.Dr0 = reinterpret_cast<DWORD64>(Base + WatchAddress());
        c.Dr7 = 1 | (1 << 16) | (3 << 18);   // L0, condição = escrita, tamanho = 4 bytes
        SetThreadContext(thread, &c);
        ResumeThread(thread);
    }

    // chamado pelo tratador de exceções: true se a exceção era do watchpoint
    bool HandleWatch(EXCEPTION_POINTERS* info)
    {
        if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(info->ContextRecord->Dr6 & 1))
            return false;
        info->ContextRecord->Dr6 = 0;
        int n = ++hits;
        if (n > 200)
            return true;
        uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        uintptr_t pc = info->ContextRecord->Rip;
        fprintf(stderr, "[watch] #%d escrita em %08X = %08X por sub_%08X (rva %llX)", n, WatchAddress(),
            Load32(WatchAddress()), GuestFunctionAt(pc), (unsigned long long)(pc - module));
        if (PPCContext* c = CurrentContext)
        {
            fprintf(stderr, " lr=%08X pilha:", uint32_t(c->lr));
            uint32_t sp = c->r1.u32;
            for (int i = 0; i < 10 && sp >= 0x40000000 && sp < 0x80000000; i++)
            {
                uint32_t previous = Load32(sp);
                if (previous <= sp || previous >= 0x80000000)
                    break;
                fprintf(stderr, " %08X", Load32(previous - 8));
                sp = previous;
            }
        }
        fprintf(stderr, "\n");
        return true;
    }
}
#else
// fora do Windows não há registradores de depuração acessíveis: o watchpoint fica desligado
namespace kernel
{
    void InstallWatch(void*) {}
}
#endif
