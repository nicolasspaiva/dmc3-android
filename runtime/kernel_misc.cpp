// Funções simples do kernel: processo, TLS, seções críticas, tempo, strings e depuração.
#include "kernel.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
    constexpr uint32_t STATUS_SUCCESS = 0;

    // TLS: os índices são do processo, os valores de cada thread
    std::mutex tlsLock;
    uint64_t tlsUsed = 0;
    thread_local uint32_t tlsValues[64];

    // seção crítica do console (endereço) -> mutex recursivo do host; criada no primeiro uso,
    // porque o jogo também usa seções zeradas que nunca passaram por RtlInitializeCriticalSection
    std::mutex sectionsLock;
    std::unordered_map<uint32_t, std::unique_ptr<std::recursive_mutex>> sections;

    std::recursive_mutex& Section(uint32_t address)
    {
        std::lock_guard lock(sectionsLock);
        auto& m = sections[address];
        if (!m)
            m = std::make_unique<std::recursive_mutex>();
        return *m;
    }
}

// uint8_t KeGetCurrentProcessType(): 1 = processo do título (jogo)
KERNEL_IMPL(KeGetCurrentProcessType)
{
    ctx.r3.u64 = 1;
}

KERNEL_IMPL(KeSetCurrentProcessType)
{
}

// NTSTATUS XexCheckExecutablePrivilege(uint32_t Privilege): nenhum privilégio especial
KERNEL_IMPL(XexCheckExecutablePrivilege)
{
    ctx.r3.u64 = 0;
}

// ---- TLS ----
KERNEL_IMPL(KeTlsAlloc)
{
    std::lock_guard lock(tlsLock);
    for (uint32_t i = 0; i < 64; i++)
    {
        if (!(tlsUsed & (1ull << i)))
        {
            tlsUsed |= 1ull << i;
            tlsValues[i] = 0;
            ctx.r3.u64 = i;
            return;
        }
    }
    ctx.r3.u64 = 0xFFFFFFFF;
}

KERNEL_IMPL(KeTlsFree)
{
    std::lock_guard lock(tlsLock);
    if (ctx.r3.u32 < 64)
        tlsUsed &= ~(1ull << ctx.r3.u32);
    ctx.r3.u64 = 1;
}

KERNEL_IMPL(KeTlsGetValue)
{
    ctx.r3.u64 = ctx.r3.u32 < 64 ? tlsValues[ctx.r3.u32] : 0;
}

KERNEL_IMPL(KeTlsSetValue)
{
    if (ctx.r3.u32 < 64)
        tlsValues[ctx.r3.u32] = ctx.r4.u32;
    ctx.r3.u64 = ctx.r3.u32 < 64;
}

// ---- seções críticas ----
KERNEL_IMPL(RtlInitializeCriticalSection)
{
    memset(kernel::Guest<uint8_t>(ctx.r3.u32), 0, 28);
    Section(ctx.r3.u32);
}

KERNEL_IMPL(RtlInitializeCriticalSectionAndSpinCount)
{
    memset(kernel::Guest<uint8_t>(ctx.r3.u32), 0, 28);
    Section(ctx.r3.u32);
    ctx.r3.u64 = STATUS_SUCCESS;
}

KERNEL_IMPL(RtlEnterCriticalSection)
{
    Section(ctx.r3.u32).lock();
}

KERNEL_IMPL(RtlLeaveCriticalSection)
{
    Section(ctx.r3.u32).unlock();
}

KERNEL_IMPL(RtlTryEnterCriticalSection)
{
    ctx.r3.u64 = Section(ctx.r3.u32).try_lock() ? 1 : 0;
}

KERNEL_IMPL(KeEnterCriticalRegion)
{
}

KERNEL_IMPL(KeLeaveCriticalRegion)
{
}

// ---- tempo ----
// void KeQuerySystemTime(int64_t* Time): FILETIME (100 ns desde 1601)
KERNEL_IMPL(KeQuerySystemTime)
{
    // relógio do sistema em FILETIME: segundos desde 1970 + a distância de 1601 a 1970
    auto since = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t t = uint64_t(since) * 10 + 116444736000000000ull;
    *kernel::Guest<uint64_t>(ctx.r3.u32) = __builtin_bswap64(t);
}

// int64_t KeQueryPerformanceFrequency(): o contador do 360 roda a 50 MHz
KERNEL_IMPL(KeQueryPerformanceFrequency)
{
    ctx.r3.u64 = 50000000;
}

// ---- strings ----
// void RtlInitAnsiString(ANSI_STRING* Dest, const char* Source): { uint16 Length, uint16 MaximumLength, char* Buffer }
KERNEL_IMPL(RtlInitAnsiString)
{
    uint32_t dest = ctx.r3.u32, src = ctx.r4.u32;
    uint16_t len = src ? uint16_t(strlen(kernel::Guest<char>(src))) : 0;
    *kernel::Guest<uint16_t>(dest + 0) = __builtin_bswap16(len);
    *kernel::Guest<uint16_t>(dest + 2) = __builtin_bswap16(src ? uint16_t(len + 1) : 0);
    kernel::Store32(dest + 4, src);
}

// void RtlInitUnicodeString(UNICODE_STRING* Dest, const wchar16* Source) (UTF-16 big-endian)
KERNEL_IMPL(RtlInitUnicodeString)
{
    uint32_t dest = ctx.r3.u32, src = ctx.r4.u32;
    uint16_t len = 0;
    if (src)
        while (*kernel::Guest<uint16_t>(src + len))
            len += 2;
    *kernel::Guest<uint16_t>(dest + 0) = __builtin_bswap16(len);
    *kernel::Guest<uint16_t>(dest + 2) = __builtin_bswap16(src ? uint16_t(len + 2) : 0);
    kernel::Store32(dest + 4, src);
}

// ---- depuração ----
// DbgPrint(const char* Format, ...): mostra o formato sem expandir os argumentos
KERNEL_IMPL(DbgPrint)
{
    std::string s = kernel::Guest<char>(ctx.r3.u32);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    fprintf(stderr, "[DbgPrint] %s\n", s.c_str());
    ctx.r3.u64 = 0;
}

// ---- configuração do console ----
// NTSTATUS ExGetXConfigSetting(uint16 Category, uint16 Setting, void* Buffer, uint16 BufferSize, uint16* RequiredSize)
KERNEL_IMPL(ExGetXConfigSetting)
{
    uint32_t category = ctx.r3.u16, setting = ctx.r4.u16, buffer = ctx.r5.u32, size = ctx.r6.u16, required = ctx.r7.u32;
    uint32_t value = 0;
    if (category == 3 && setting == 9)
        value = 1;   // XCONFIG_USER_LANGUAGE: inglês
    if (buffer && size >= 4)
    {
        memset(kernel::Guest<uint8_t>(buffer), 0, size);
        kernel::Store32(buffer, value);
    }
    if (required)
        *kernel::Guest<uint16_t>(required) = __builtin_bswap16(4);
    ctx.r3.u64 = STATUS_SUCCESS;
}

// uint32_t XGetGameRegion(): todas as regiões
KERNEL_IMPL(XGetGameRegion)
{
    ctx.r3.u64 = 0xFFFF;
}

// uint32_t XamLoaderGetLaunchDataSize(uint32_t* Size): o jogo não foi lançado com dados
KERNEL_IMPL(XamLoaderGetLaunchDataSize)
{
    kernel::Store32(ctx.r3.u32, 0);
    ctx.r3.u64 = 0x490;   // ERROR_NOT_FOUND
}

// uint32_t RtlNtStatusToDosError(NTSTATUS): só os códigos que o runtime devolve
KERNEL_IMPL(RtlNtStatusToDosError)
{
    switch (ctx.r3.u32)
    {
    case 0x00000000: ctx.r3.u64 = 0; break;      // ERROR_SUCCESS
    case 0x80000006: ctx.r3.u64 = 18; break;     // ERROR_NO_MORE_FILES
    case 0xC0000008: ctx.r3.u64 = 6; break;      // ERROR_INVALID_HANDLE
    case 0xC0000011: ctx.r3.u64 = 38; break;     // ERROR_HANDLE_EOF
    case 0xC0000034: ctx.r3.u64 = 2; break;      // ERROR_FILE_NOT_FOUND
    case 0xC000003A: ctx.r3.u64 = 3; break;      // ERROR_PATH_NOT_FOUND
    default:
        fprintf(stderr, "[kernel] RtlNtStatusToDosError: status %08X sem tradução\n", ctx.r3.u32);
        ctx.r3.u64 = 317;                        // ERROR_MR_MID_NOT_FOUND
    }
}
