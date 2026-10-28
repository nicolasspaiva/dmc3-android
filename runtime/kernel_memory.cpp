// Memória: NtAllocateVirtualMemory e companhia, e a memória física (MmAllocatePhysicalMemoryEx),
// que no 360 é a usada pela GPU.
#include "kernel.h"

#include <cstdlib>

namespace
{
    constexpr uint32_t STATUS_SUCCESS = 0;
    constexpr uint32_t MEM_COMMIT = 0x1000;
    constexpr uint32_t MEM_RESERVE = 0x2000;
    constexpr uint32_t MEM_LARGE_PAGES = 0x20000000;

    uint32_t RoundUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
}

// NTSTATUS NtAllocateVirtualMemory(void** BaseAddress, uint32_t* RegionSize, uint32_t AllocationType, uint32_t Protect, ...)
KERNEL_IMPL(NtAllocateVirtualMemory)
{
    uint32_t pBase = ctx.r3.u32, pSize = ctx.r4.u32, type = ctx.r5.u32;
    uint32_t address = kernel::Load32(pBase);
    uint32_t page = type & MEM_LARGE_PAGES ? 0x10000 : 0x1000;
    uint32_t size = RoundUp(kernel::Load32(pSize), page);

    uint32_t requested = address;
    if (address == 0)
        address = kernel::AllocVirtual(size, 0x10000);   // reserva e confirma de uma vez
    else if (type & MEM_COMMIT)
        kernel::Commit(address & ~(page - 1), size);

    static bool log = getenv("DMC3_LOG_ALLOC") != nullptr;
    if (log)
        fprintf(stderr, "[mem] NtAllocateVirtualMemory(base %08X, tamanho %X, tipo %X) -> %08X-%08X\n",
            requested, size, type, address, address + size);
    kernel::Store32(pBase, address);
    kernel::Store32(pSize, size);
    ctx.r3.u64 = STATUS_SUCCESS;
}

// NTSTATUS NtFreeVirtualMemory(void** BaseAddress, uint32_t* RegionSize, uint32_t FreeType, ...)
KERNEL_IMPL(NtFreeVirtualMemory)
{
    // por enquanto a memória não volta para o alocador
    ctx.r3.u64 = STATUS_SUCCESS;
}

// void* MmAllocatePhysicalMemoryEx(uint32_t Flags, uint32_t Size, uint32_t Protect, uint32_t MinAddress, uint32_t MaxAddress, uint32_t Alignment)
KERNEL_IMPL(MmAllocatePhysicalMemoryEx)
{
    uint32_t size = RoundUp(ctx.r4.u32, 0x1000);
    uint32_t align = ctx.r8.u32 > 0x1000 ? ctx.r8.u32 : 0x1000;
    ctx.r3.u64 = kernel::AllocPhysical(size, align);
}

KERNEL_IMPL(MmFreePhysicalMemory)
{
    ctx.r3.u64 = 0;
}

// uint32_t MmGetPhysicalAddress(void* Address): no 360 o endereço físico é o virtual sem os bits altos
KERNEL_IMPL(MmGetPhysicalAddress)
{
    ctx.r3.u64 = ctx.r3.u32 & 0x1FFFFFFF;
}

// uint32_t MmQueryAddressProtect(void* Address)
KERNEL_IMPL(MmQueryAddressProtect)
{
    ctx.r3.u64 = 0x04;   // PAGE_READWRITE
}
