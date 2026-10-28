// Threads e sincronização.
//
// Objetos de espera (eventos, semáforos, threads) vivem na memória do console com um cabeçalho de
// despacho { uint8 Type; ...; int32 SignalState (+4); ... }. As funções Ke* recebem o endereço desse
// cabeçalho; as Nt* recebem um handle, que aponta para um cabeçalho alocado aqui. O estado real fica
// no host, num mapa protegido por um único mutex com variável de condição, e é copiado de volta para
// o cabeçalho no console a cada mudança.
#include "kernel.h"
#include "gpu.h"
#include "platform.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace
{
    constexpr uint32_t X_STATUS_SUCCESS = 0;
    constexpr uint32_t X_STATUS_TIMEOUT = 0x102;
    constexpr uint32_t X_STATUS_INVALID_HANDLE = 0xC0000008;

    enum ObjectType : uint8_t
    {
        NotificationEvent = 0,
        SynchronizationEvent = 1,
        Mutant = 2,
        Semaphore = 5,
        Thread = 6,
    };

    struct Dispatcher
    {
        uint8_t type;
        int32_t signal;
        int32_t limit;
    };

    std::mutex waitLock;
    std::condition_variable waitChanged;
    std::unordered_map<uint32_t, Dispatcher> objects;   // endereço do cabeçalho -> estado
    std::unordered_map<uint32_t, uint32_t> handles;     // handle -> endereço do cabeçalho
    uint32_t nextHandle = 0xF8000004;

    // precisa de waitLock
    Dispatcher& Object(uint32_t header)
    {
        auto it = objects.find(header);
        if (it != objects.end())
            return it->second;
        Dispatcher d;
        d.type = *kernel::Guest<uint8_t>(header);
        d.signal = int32_t(kernel::Load32(header + 4));
        d.limit = d.type == Semaphore ? int32_t(kernel::Load32(header + 0x10)) : 1;
        return objects.emplace(header, d).first->second;
    }

    void Publish(uint32_t header, const Dispatcher& d)
    {
        kernel::Store32(header + 4, uint32_t(d.signal));
    }

    // registra um objeto num cabeçalho já alocado (alocando um de 0x20 bytes se header = 0)
    uint32_t NewObject(uint8_t type, int32_t signal, int32_t limit, uint32_t header = 0)
    {
        if (header == 0)
            header = kernel::AllocPhysical(0x20, 0x10);
        *kernel::Guest<uint8_t>(header) = type;
        kernel::Store32(header + 4, uint32_t(signal));
        kernel::Store32(header + 0x10, uint32_t(limit));
        std::lock_guard lock(waitLock);
        objects[header] = { type, signal, limit };
        return header;
    }

    uint32_t NewHandle(uint32_t header)
    {
        std::lock_guard lock(waitLock);
        uint32_t h = nextHandle;
        nextHandle += 4;
        handles[h] = header;
        return h;
    }

    uint32_t HeaderOf(uint32_t handle)
    {
        std::lock_guard lock(waitLock);
        auto it = handles.find(handle);
        return it == handles.end() ? 0 : it->second;
    }

    // precisa de waitLock: o objeto está sinalizado? se consume, aplica o efeito da espera satisfeita
    bool TryAcquire(uint32_t header, bool consume)
    {
        Dispatcher& d = Object(header);
        if (d.signal <= 0)
            return false;
        if (consume)
        {
            if (d.type == SynchronizationEvent || d.type == Mutant)
                d.signal = 0;
            else if (d.type == Semaphore)
                d.signal--;
            Publish(header, d);
        }
        return true;
    }

    // tempo limite do kernel: nulo = infinito, negativo = relativo em 100 ns, positivo = absoluto (FILETIME)
    std::chrono::steady_clock::time_point Deadline(uint32_t pTimeout, bool& infinite)
    {
        infinite = pTimeout == 0;
        auto now = std::chrono::steady_clock::now();
        if (infinite)
            return now;
        int64_t v = int64_t(__builtin_bswap64(*kernel::Guest<uint64_t>(pTimeout)));
        if (v < 0)
            return now + std::chrono::microseconds(-v / 10);
        // hora absoluta em FILETIME (100 ns desde 1601)
        auto since = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t current = int64_t(since) * 10 + 116444736000000000ll;
        return now + std::chrono::microseconds(v > current ? (v - current) / 10 : 0);
    }

    // espera um ou vários objetos; waitAll = todos ao mesmo tempo. Devolve índice ou STATUS_TIMEOUT
    uint32_t Wait(const uint32_t* headers, uint32_t count, bool waitAll, uint32_t pTimeout)
    {
        bool infinite;
        auto deadline = Deadline(pTimeout, infinite);
        std::unique_lock lock(waitLock);
        for (;;)
        {
            if (waitAll)
            {
                bool all = true;
                for (uint32_t i = 0; i < count && all; i++)
                    all = TryAcquire(headers[i], false);
                if (all)
                {
                    for (uint32_t i = 0; i < count; i++)
                        TryAcquire(headers[i], true);
                    return X_STATUS_SUCCESS;
                }
            }
            else
            {
                for (uint32_t i = 0; i < count; i++)
                    if (TryAcquire(headers[i], true))
                        return X_STATUS_SUCCESS + i;
            }
            if (infinite)
                waitChanged.wait(lock);
            else if (waitChanged.wait_until(lock, deadline) == std::cv_status::timeout)
                return X_STATUS_TIMEOUT;
        }
    }

    // precisa de waitLock
    int32_t SetSignal(uint32_t header, int32_t signal)
    {
        Dispatcher& d = Object(header);
        int32_t previous = d.signal;
        d.signal = signal;
        Publish(header, d);
        waitChanged.notify_all();
        return previous;
    }

    // ---- threads ----
    struct GuestThread
    {
        uint32_t entry, arg1, arg2;
        uint32_t kthread, pcr, stack, stackSize, id;
        std::mutex resumeLock;
        std::condition_variable resumed;
        int suspendCount = 0;
        void* host = nullptr;   // thread do host (platform::StartThread)
        kernel::ImportTrace trace;
        std::function<void(PPCContext&)> hostBody;   // thread do runtime com contexto do jogo (CreateHostedThread)
    };

    std::mutex threadsLock;
    std::map<uint32_t, std::unique_ptr<GuestThread>> threads;   // kthread -> thread
    std::atomic<uint32_t> nextThreadId{ 1 };

    void ExitGuestThread(GuestThread& t)
    {
        std::lock_guard lock(waitLock);
        SetSignal(t.kthread, 1);
    }

    void ThreadMain(void* param)
    {
        auto& t = *static_cast<GuestThread*>(param);
        kernel::CurrentTrace = &t.trace;
        {
            std::unique_lock lock(t.resumeLock);
            t.resumed.wait(lock, [&] { return t.suspendCount <= 0; });
        }
        auto* ctxMemory = static_cast<PPCContext*>(platform::AlignedAlloc(sizeof(PPCContext), 64));
        new (ctxMemory) PPCContext{};
        PPCContext& ctx = *ctxMemory;
        kernel::CurrentContext = ctxMemory;
        uint8_t* base = kernel::Base;
        ctx.r13.u64 = t.pcr;
        ctx.r1.u64 = t.stack + t.stackSize - 0x100;
        ctx.r3.u64 = t.arg1;
        ctx.r4.u64 = t.arg2;
        ctx.fpscr.loadFromHost();
        if (t.hostBody)
            t.hostBody(ctx);
        else
            PPC_CALL_INDIRECT_FUNC(t.entry);
        ExitGuestThread(t);
    }

    GuestThread* ThreadOfHandle(uint32_t handle)
    {
        uint32_t header = HeaderOf(handle);
        std::lock_guard lock(threadsLock);
        auto it = threads.find(header);
        return it == threads.end() ? nullptr : it->second.get();
    }

    GuestThread* CurrentThread(PPCContext& ctx)
    {
        uint32_t kthread = kernel::Load32(ctx.r13.u32 + 0x100);
        std::lock_guard lock(threadsLock);
        auto it = threads.find(kthread);
        return it == threads.end() ? nullptr : it->second.get();
    }
}

namespace kernel
{
    uint32_t CreateGuestThread(uint32_t entry, uint32_t arg1, uint32_t arg2, uint32_t stackSize, bool suspended,
        uint32_t* threadId, uint32_t* kthreadOut)
    {
        return CreateHostedThread(nullptr, entry, arg1, arg2, stackSize, suspended, threadId, kthreadOut);
    }

    uint32_t CreateHostedThread(std::function<void(PPCContext&)> body, uint32_t entry, uint32_t arg1, uint32_t arg2,
        uint32_t stackSize, bool suspended, uint32_t* threadId, uint32_t* kthreadOut)
    {
        auto t = std::make_unique<GuestThread>();
        t->hostBody = std::move(body);
        t->entry = entry;
        t->arg1 = arg1;
        t->arg2 = arg2;
        t->stackSize = std::max<uint32_t>((stackSize + 0xFFFF) & ~0xFFFFu, 256 * 1024);
        t->stack = AllocVirtual(t->stackSize);
        t->id = nextThreadId++;
        t->suspendCount = suspended ? 1 : 0;

        // KTHREAD: cabeçalho de despacho (tipo thread, não sinalizado até terminar) + campos que o CRT lê
        t->kthread = NewObject(Thread, 0, 1, AllocPhysical(0x1000));
        Store32(t->kthread + 0x05C, t->stack + t->stackSize);
        Store32(t->kthread + 0x060, t->stack);
        Store32(t->kthread + 0x14C, t->id);

        // bloco do processador (r13), layout do Xenia: TLS em +0, pilha em +0x70/+0x74, thread em +0x100
        t->pcr = AllocPhysical(0x1000);
        uint32_t tls = AllocPhysical(0x1000);
        Store32(t->pcr + 0x000, tls);
        Store32(t->pcr + 0x070, t->stack + t->stackSize);
        Store32(t->pcr + 0x074, t->stack);
        Store32(t->pcr + 0x100, t->kthread);

        GuestThread* raw = t.get();
        uint32_t kthread = t->kthread;
        {
            std::lock_guard lock(threadsLock);
            threads[kthread] = std::move(t);
        }
        if (threadId)
            *threadId = raw->id;
        if (kthreadOut)
            *kthreadOut = kthread;
        uint32_t handle = NewHandle(kthread);
        fprintf(stderr, "[thread] criada %u: entrada %08X (%08X, %08X)%s\n", raw->id, entry, arg1, arg2,
            suspended ? " suspensa" : "");
        // o código recompilado aninha uma chamada do host por chamada do jogo: pilha grande
        raw->host = platform::StartThread(ThreadMain, raw, 64 * 1024 * 1024);
        InstallWatch(raw->host);
        return handle;
    }

    void RunMainThread(uint32_t entry)
    {
        uint32_t kthread;
        CreateGuestThread(entry, 0, 0, 1024 * 1024, false, nullptr, &kthread);
        uint32_t headers[] = { kthread };
        Wait(headers, 1, false, 0);
        fprintf(stderr, "[thread] a thread principal terminou\n");
    }

    // o endereço do host está dentro do código recompilado (entre a primeira e a última função)?
    bool InRecompiledCode(uintptr_t pc)
    {
        static uintptr_t lo = UINTPTR_MAX, hi = 0;
        if (hi == 0)
            for (auto* m = PPCFuncMappings; m->host != nullptr; m++)
            {
                lo = std::min(lo, reinterpret_cast<uintptr_t>(m->host));
                hi = std::max(hi, reinterpret_cast<uintptr_t>(m->host));
            }
        return pc >= lo && pc <= hi + 0x10000;
    }

    void DumpThreads()
    {
        std::lock_guard lock(threadsLock);
        fprintf(stderr, "\n[watchdog] %zu threads do jogo:\n", threads.size());
        for (auto& [kthread, t] : threads)
        {
            if (!platform::ThreadRunning(t->host))
            {
                fprintf(stderr, "  thread %u (entrada %08X): terminou\n", t->id, t->entry);
                continue;
            }
#ifdef _WIN32
            SuspendThread(t->host);
            CONTEXT c{};
            c.ContextFlags = CONTEXT_CONTROL;
            GetThreadContext(t->host, &c);
            ResumeThread(t->host);
            uint32_t closest = GuestFunctionAt(c.Rip);
            uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
            fprintf(stderr, "  thread %u (entrada %08X): %s sub_%08X (rva %llX); última chamada ao kernel: %s, retorno em %08X\n",
                t->id, t->entry, InRecompiledCode(c.Rip) ? "no código do jogo, perto de" : "no runtime, longe de",
                closest, (unsigned long long)(c.Rip - module), t->trace.name ? t->trace.name : "nenhuma", t->trace.lr);
#else
            fprintf(stderr, "  thread %u (entrada %08X): última chamada ao kernel: %s, retorno em %08X\n", t->id, t->entry,
                t->trace.name ? t->trace.name : "nenhuma", t->trace.lr);
#endif
        }
    }

    void SignalEventHandle(uint32_t handle)
    {
        uint32_t header = HeaderOf(handle);
        if (!header)
            return;
        std::lock_guard lock(waitLock);
        SetSignal(header, 1);
    }

    bool CloseObjectHandle(uint32_t handle)
    {
        std::lock_guard lock(waitLock);
        return handles.erase(handle) != 0;
    }
}

// ---- threads ----
// NTSTATUS ExCreateThread(HANDLE* Handle, uint32 StackSize, uint32* ThreadId, void* XapiThreadStartup,
//                         void* StartAddress, void* StartContext, uint32 CreationFlags)
KERNEL_IMPL(ExCreateThread)
{
    uint32_t pHandle = ctx.r3.u32, stackSize = ctx.r4.u32, pId = ctx.r5.u32;
    uint32_t startup = ctx.r6.u32, start = ctx.r7.u32, context = ctx.r8.u32;
    bool suspended = ctx.r9.u32 & 1;   // CREATE_SUSPENDED
    uint32_t id;
    // com XapiThreadStartup, ela recebe (StartAddress, StartContext) e chama a função do jogo
    uint32_t handle = startup ? kernel::CreateGuestThread(startup, start, context, stackSize, suspended, &id, nullptr)
                              : kernel::CreateGuestThread(start, context, 0, stackSize, suspended, &id, nullptr);
    if (pHandle)
        kernel::Store32(pHandle, handle);
    if (pId)
        kernel::Store32(pId, id);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// void ExTerminateThread(NTSTATUS ExitStatus)
KERNEL_IMPL(ExTerminateThread)
{
    if (GuestThread* t = CurrentThread(ctx))
        ExitGuestThread(*t);
    platform::ExitCurrentThread();
}

// NTSTATUS NtResumeThread(HANDLE, uint32* PreviousSuspendCount)
KERNEL_IMPL(NtResumeThread)
{
    GuestThread* t = ThreadOfHandle(ctx.r3.u32);
    if (!t)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    std::lock_guard lock(t->resumeLock);
    if (ctx.r4.u32)
        kernel::Store32(ctx.r4.u32, uint32_t(t->suspendCount));
    if (t->suspendCount > 0 && --t->suspendCount == 0)
        t->resumed.notify_all();
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// NTSTATUS NtSuspendThread(HANDLE, uint32* PreviousSuspendCount): só tem efeito antes da thread começar
KERNEL_IMPL(NtSuspendThread)
{
    GuestThread* t = ThreadOfHandle(ctx.r3.u32);
    if (!t)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    std::lock_guard lock(t->resumeLock);
    if (ctx.r4.u32)
        kernel::Store32(ctx.r4.u32, uint32_t(t->suspendCount));
    t->suspendCount++;
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

KERNEL_IMPL(KeSetAffinityThread)
{
    ctx.r3.u64 = 0;
}

KERNEL_IMPL(KeSetBasePriorityThread)
{
    ctx.r3.u64 = 0;
}

KERNEL_IMPL(KeQueryBasePriorityThread)
{
    ctx.r3.u64 = 0;
}

// NTSTATUS KeDelayExecutionThread(uint8 WaitMode, uint8 Alertable, int64* Interval)
KERNEL_IMPL(KeDelayExecutionThread)
{
    int64_t v = int64_t(__builtin_bswap64(*kernel::Guest<uint64_t>(ctx.r5.u32)));
    int64_t ms = v < 0 ? -v / 10000 : 0;
    uint64_t start = platform::NowNs();
    if (ms == 0)
        platform::YieldThread();
    else
        platform::SleepMs(uint32_t(ms));
    gpu::BlockedTime(2, platform::NowNs() - start);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// ---- eventos ----
// NTSTATUS NtCreateEvent(HANDLE* Handle, OBJECT_ATTRIBUTES*, uint32 EventType, uint8 InitialState)
KERNEL_IMPL(NtCreateEvent)
{
    // EventType: 0 = NotificationEvent, 1 = SynchronizationEvent (igual ao tipo do cabeçalho)
    uint32_t header = NewObject(uint8_t(ctx.r5.u32 ? SynchronizationEvent : NotificationEvent), ctx.r6.u8 ? 1 : 0, 1);
    kernel::Store32(ctx.r3.u32, NewHandle(header));
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// int32 KeSetEvent(KEVENT*, int32 Increment, uint8 Wait): devolve o estado anterior
KERNEL_IMPL(KeSetEvent)
{
    std::lock_guard lock(waitLock);
    ctx.r3.u64 = uint32_t(SetSignal(ctx.r3.u32, 1));
}

KERNEL_IMPL(KeResetEvent)
{
    std::lock_guard lock(waitLock);
    ctx.r3.u64 = uint32_t(SetSignal(ctx.r3.u32, 0));
}

// NTSTATUS NtSetEvent(HANDLE, int32* PreviousState)
KERNEL_IMPL(NtSetEvent)
{
    uint32_t header = HeaderOf(ctx.r3.u32);
    if (!header)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    std::lock_guard lock(waitLock);
    int32_t previous = SetSignal(header, 1);
    if (ctx.r4.u32)
        kernel::Store32(ctx.r4.u32, uint32_t(previous));
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

KERNEL_IMPL(NtClearEvent)
{
    uint32_t header = HeaderOf(ctx.r3.u32);
    if (!header)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    std::lock_guard lock(waitLock);
    SetSignal(header, 0);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// ---- semáforos ----
// NTSTATUS NtCreateSemaphore(HANDLE* Handle, OBJECT_ATTRIBUTES*, int32 InitialCount, int32 MaximumCount)
KERNEL_IMPL(NtCreateSemaphore)
{
    uint32_t header = NewObject(Semaphore, ctx.r5.s32, ctx.r6.s32);
    kernel::Store32(ctx.r3.u32, NewHandle(header));
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// NTSTATUS NtReleaseSemaphore(HANDLE, int32 ReleaseCount, int32* PreviousCount)
KERNEL_IMPL(NtReleaseSemaphore)
{
    uint32_t header = HeaderOf(ctx.r3.u32);
    if (!header)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    std::lock_guard lock(waitLock);
    Dispatcher& d = Object(header);
    int32_t previous = SetSignal(header, std::min(d.signal + ctx.r4.s32, d.limit));
    if (ctx.r5.u32)
        kernel::Store32(ctx.r5.u32, uint32_t(previous));
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// ---- espera ----
// NTSTATUS KeWaitForSingleObject(void* Object, uint32 WaitReason, uint8 WaitMode, uint8 Alertable, int64* Timeout)
KERNEL_IMPL(KeWaitForSingleObject)
{
    uint32_t header = ctx.r3.u32;
    uint64_t start = platform::NowNs();
    ctx.r3.u64 = Wait(&header, 1, false, ctx.r7.u32);
    gpu::BlockedTime(1, platform::NowNs() - start);
}

// NTSTATUS KeWaitForMultipleObjects(uint32 Count, void** Objects, uint32 WaitType, uint32 WaitReason,
//                                   uint8 WaitMode, uint8 Alertable, int64* Timeout, KWAIT_BLOCK* WaitBlocks)
KERNEL_IMPL(KeWaitForMultipleObjects)
{
    uint32_t count = std::min<uint32_t>(ctx.r3.u32, 64), headers[64];
    for (uint32_t i = 0; i < count; i++)
        headers[i] = kernel::Load32(ctx.r4.u32 + 4 * i);
    uint64_t start = platform::NowNs();
    ctx.r3.u64 = Wait(headers, count, ctx.r5.u32 == 0, ctx.r9.u32);
    gpu::BlockedTime(1, platform::NowNs() - start);
}

// NTSTATUS NtWaitForSingleObjectEx(HANDLE, uint8 WaitMode, uint8 Alertable, int64* Timeout)
KERNEL_IMPL(NtWaitForSingleObjectEx)
{
    uint32_t header = HeaderOf(ctx.r3.u32);
    if (!header)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    uint64_t start = platform::NowNs();
    ctx.r3.u64 = Wait(&header, 1, false, ctx.r6.u32);
    gpu::BlockedTime(1, platform::NowNs() - start);
}

// ---- referências ----
// NTSTATUS ObReferenceObjectByHandle(HANDLE, void* ObjectType, void** Object)
KERNEL_IMPL(ObReferenceObjectByHandle)
{
    uint32_t header = ctx.r3.u32 == 0xFFFFFFFE ? kernel::Load32(ctx.r13.u32 + 0x100)   // thread atual
                                               : HeaderOf(ctx.r3.u32);
    if (!header)
    {
        fprintf(stderr, "[kernel] ObReferenceObjectByHandle: handle %08X desconhecido\n", ctx.r3.u32);
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    if (ctx.r5.u32)
        kernel::Store32(ctx.r5.u32, header);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

KERNEL_IMPL(ObReferenceObject)
{
}

KERNEL_IMPL(ObDereferenceObject)
{
}

// NTSTATUS NtDuplicateObject(HANDLE Source, HANDLE* Target, uint32 Options)
KERNEL_IMPL(NtDuplicateObject)
{
    uint32_t header = HeaderOf(ctx.r3.u32);
    if (!header)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    if (ctx.r4.u32)
        kernel::Store32(ctx.r4.u32, NewHandle(header));
    if (ctx.r5.u32 & 1)   // DUPLICATE_CLOSE_SOURCE
        kernel::CloseObjectHandle(ctx.r3.u32);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// ---- spinlocks: uint32 na memória do console, 0 = livre ----
namespace
{
    void SpinAcquire(uint32_t lock, uint32_t owner)
    {
        uint32_t* word = kernel::Guest<uint32_t>(lock);
        uint32_t expected = 0;
        while (!__atomic_compare_exchange_n(word, &expected, __builtin_bswap32(owner), true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            expected = 0;
            platform::CpuRelax();
        }
    }

    void SpinRelease(uint32_t lock)
    {
        __atomic_store_n(kernel::Guest<uint32_t>(lock), 0u, __ATOMIC_RELEASE);
    }
}

// uint8 KfAcquireSpinLock(KSPIN_LOCK*): devolve o IRQL anterior
KERNEL_IMPL(KfAcquireSpinLock)
{
    SpinAcquire(ctx.r3.u32, ctx.r13.u32);
    ctx.r3.u64 = 0;
}

KERNEL_IMPL(KfReleaseSpinLock)
{
    SpinRelease(ctx.r3.u32);
}

KERNEL_IMPL(KeAcquireSpinLockAtRaisedIrql)
{
    SpinAcquire(ctx.r3.u32, ctx.r13.u32);
}

KERNEL_IMPL(KeReleaseSpinLockFromRaisedIrql)
{
    SpinRelease(ctx.r3.u32);
}
