// Runtime do DMC3 recompilado: memória do console, carga da imagem e o "kernel" do Xbox 360
// (funções de xboxkrnl.exe / xam.xex que o jogo importa).
#pragma once

#include <ppc_recomp_shared.h>
#include "platform.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <string>

namespace kernel
{
    // última chamada ao kernel de cada thread do jogo (para o watchdog): nome e endereço de retorno no jogo
    struct ImportTrace
    {
        const char* volatile name = nullptr;
        volatile uint32_t lr = 0;
    };
    extern thread_local ImportTrace* CurrentTrace;
    extern thread_local PPCContext* CurrentContext;   // contexto da thread do jogo atual (para o relatório de crash)

    inline void Trace(const char* name, uint32_t lr)
    {
        if (CurrentTrace)
        {
            CurrentTrace->name = name;
            CurrentTrace->lr = lr;
        }
    }
}

// Implementa uma função importada. O gerador (scripts/x360_imports.py) procura este nome
// em runtime/*.cpp e só cria stub para o que não foi implementado.
#define KERNEL_IMPL(name)                                                       \
    static void name##_impl(PPCContext& __restrict ctx, uint8_t* base);         \
    PPC_FUNC(__imp__##name)                                                     \
    {                                                                           \
        kernel::Trace(#name, uint32_t(ctx.lr));                                 \
        name##_impl(ctx, base);                                                 \
    }                                                                           \
    static void name##_impl(PPCContext& __restrict ctx, uint8_t* base)

// Substitui uma função do jogo. Cada sub_X gerado pelo XenonRecomp é um alias fraco de __imp__sub_X;
// definir sub_X aqui troca a função em todas as chamadas (diretas e pela tabela de funções).
// A original continua disponível como GUEST_ORIGINAL(endereço)(ctx, base).
#define GUEST_OVERRIDE(address) PPC_FUNC(sub_##address)
#define GUEST_ORIGINAL(address) __imp__sub_##address
#define GUEST_DECLARE_ORIGINAL(address) PPC_FUNC_IMPL(__imp__sub_##address)

namespace kernel
{
    struct ImportedVariable
    {
        uint32_t slot;        // registro na imagem que recebe o endereço da variável
        const char* name;
        uint32_t ordinal;
        const char* library;
    };

    struct ImportedPointer
    {
        uint32_t slot;        // registro que acompanha uma função importada
        uint32_t thunk;       // endereço do thunk da função
    };

    extern const ImportedVariable ImportedVariables[];
    extern const ImportedPointer ImportedPointers[];

    // chamada de função importada ainda sem implementação: registra e devolve 0 em r3
    void Unimplemented(const char* name, PPCContext& ctx);

    // chama uma função do jogo a partir do runtime, com até 8 argumentos (r3..r10), usando uma cópia
    // do contexto da thread com a pilha abaixo da atual. Devolve r3.
    uint32_t CallGuest(const PPCContext& parent, uint32_t function, std::initializer_list<uint32_t> args);

    // memória do console (4 GB de endereço reservados; páginas confirmadas sob demanda)
    extern uint8_t* Base;
    void InitMemory();
    void Commit(uint32_t guest, uint32_t size);
    uint32_t AllocVirtual(uint32_t size, uint32_t align = 0x10000);   // 0x40000000..0x7FFFFFFF
    uint32_t AllocPhysical(uint32_t size, uint32_t align = 0x1000);   // 0xA0000000..0xBFFFFFFF

    // threads do jogo: cada uma é uma thread do host com contexto, pilha, bloco do processador (r13) e TLS próprios
    // entry recebe r3 = arg1, r4 = arg2. Devolve o handle; o objeto da thread (KTHREAD) fica em *kthread.
    uint32_t CreateGuestThread(uint32_t entry, uint32_t arg1, uint32_t arg2, uint32_t stackSize, bool suspended,
        uint32_t* threadId, uint32_t* kthread);
    void RunMainThread(uint32_t entry);   // cria a thread principal e espera ela terminar
    // thread do runtime que roda código do host com um contexto do jogo completo (pilha, r13, TLS), para chamar
    // funções do jogo com CallGuest a partir de um laço do host (o "driver" de áudio, por exemplo)
    uint32_t CreateHostedThread(std::function<void(PPCContext&)> body, uint32_t entry, uint32_t arg1, uint32_t arg2,
        uint32_t stackSize, bool suspended, uint32_t* threadId, uint32_t* kthread);

    // handles de objetos do kernel (eventos, semáforos, threads); arquivos têm tabela própria
    bool CloseObjectHandle(uint32_t handle);
    void SignalEventHandle(uint32_t handle);   // como NtSetEvent, para uso do runtime (fim de E/S)

    // dispositivos montados (raízes de conteúdo como "save:"): nome sem ":" -> pasta do host; vazio se não montado
    std::string MountedPath(const std::string& device);
    // caminho do console ("game:\Media\x.wmv") -> arquivo do host, como o NtCreateFile resolve
    std::string HostPathOf(const std::string& guest);
    void Mount(const std::string& device, const std::string& hostPath);
    void Unmount(const std::string& device);

    // conclui uma operação assíncrona do XAM pelo XOVERLAPPED (resultado e evento)
    void CompleteOverlapped(uint32_t overlapped, uint32_t result);

    // diagnóstico: função recompilada mais próxima (abaixo) de um endereço de código do host,
    // e um retrato de onde cada thread do jogo está
    uint32_t GuestFunctionAt(uintptr_t hostAddress);
    void DumpThreads();
    void InstallWatch(void* hostThread);   // watchpoint DMC3_WATCH (runtime/watch.cpp)

    template<class T> inline T* Guest(uint32_t address) { return reinterpret_cast<T*>(Base + address); }

    inline void Store32(uint32_t address, uint32_t value) { *Guest<uint32_t>(address) = __builtin_bswap32(value); }
    inline uint32_t Load32(uint32_t address) { return __builtin_bswap32(*Guest<uint32_t>(address)); }
}
