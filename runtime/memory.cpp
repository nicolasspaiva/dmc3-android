#include "kernel.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/sharedmem.h>
#endif
#endif
#include <cstdlib>
#include <mutex>

namespace kernel
{
    uint8_t* Base = nullptr;

    namespace
    {
        uint32_t nextVirtual = 0x40000000;
        uint32_t nextPhysical = 0xA0000000;
        std::mutex allocLock;

        // o código gerado endereça base + endereço de 32 bits, então reserva os 4 GB inteiros. A memória física
        // (512 MB) aparece em 0xA0000000 (páginas de 64 KB) e de novo em 0xC0000000 (páginas de 16 MB): a D3D
        // converte endereços da GPU de volta com | 0xC0000000. As duas faixas são vistas da mesma seção.
        constexpr uint64_t PhysicalSize = 0x20000000;
        constexpr uint32_t PhysicalViews[] = { 0xA0000000, 0xC0000000 };

        uint32_t Bump(uint32_t& next, uint32_t limit, uint32_t size, uint32_t align, const char* what)
        {
            std::lock_guard lock(allocLock);
            uint32_t address = (next + align - 1) & ~(align - 1);
            if (uint64_t(address) + size > limit)
            {
                fprintf(stderr, "[mem] sem espaço em %s para 0x%X bytes\n", what, size);
                std::abort();
            }
            next = address + size;
            Commit(address, size);
            return address;
        }

        bool InPhysicalView(uint32_t guest)
        {
            for (uint32_t view : PhysicalViews)
                if (guest >= view && uint64_t(guest) < view + PhysicalSize)
                    return true;
            return false;
        }
    }

#ifdef _WIN32
    void InitMemory()
    {
        for (int attempt = 0; attempt < 8 && Base == nullptr; attempt++)
        {
            auto* area = static_cast<uint8_t*>(VirtualAlloc(nullptr, PPC_MEMORY_SIZE, MEM_RESERVE, PAGE_NOACCESS));
            if (area == nullptr)
                break;
            VirtualFree(area, 0, MEM_RELEASE);
            HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_RESERVE,
                DWORD(PhysicalSize >> 32), DWORD(PhysicalSize), nullptr);
            bool ok = section != nullptr && VirtualAlloc(area, PhysicalViews[0], MEM_RESERVE, PAGE_NOACCESS) == area;
            for (uint32_t view : PhysicalViews)
                ok = ok && MapViewOfFileEx(section, FILE_MAP_ALL_ACCESS, 0, 0, PhysicalSize, area + view) == area + view;
            if (ok)
                Base = area;   // outra thread pode ter tomado o espaço entre a liberação e o mapeamento: tenta de novo
            else
            {
                VirtualFree(area, 0, MEM_RELEASE);
                for (uint32_t view : PhysicalViews)
                    UnmapViewOfFile(area + view);
            }
            if (section)
                CloseHandle(section);   // as vistas mantêm a seção viva
        }
        if (Base == nullptr)
        {
            fprintf(stderr, "[mem] não foi possível reservar 4 GB de endereço\n");
            std::abort();
        }
    }

    void Commit(uint32_t guest, uint32_t size)
    {
        if (size != 0 && VirtualAlloc(Base + guest, size, MEM_COMMIT, PAGE_READWRITE) == nullptr)
        {
            fprintf(stderr, "[mem] falha ao confirmar 0x%08X (+0x%X)\n", guest, size);
            std::abort();
        }
    }
#else
    // POSIX/Android: os 4 GB reservados sem acesso (as páginas só existem quando tocadas), a memória física num
    // objeto de memória compartilhada mapeado nas duas faixas, e o resto liberado por mprotect no Commit
    void InitMemory()
    {
        void* area = mmap(nullptr, PPC_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (area == MAP_FAILED)
        {
            fprintf(stderr, "[mem] não foi possível reservar 4 GB de endereço\n");
            std::abort();
        }
#ifdef __ANDROID__
        int fd = ASharedMemory_create("dmc3-physical", PhysicalSize);
#else
        int fd = memfd_create("dmc3-physical", 0);
        if (fd >= 0 && ftruncate(fd, off_t(PhysicalSize)) != 0)
            fd = -1;
#endif
        if (fd < 0)
        {
            fprintf(stderr, "[mem] não foi possível criar a memória física\n");
            std::abort();
        }
        for (uint32_t view : PhysicalViews)
            if (mmap(static_cast<uint8_t*>(area) + view, PhysicalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == MAP_FAILED)
            {
                fprintf(stderr, "[mem] não foi possível mapear a memória física em 0x%08X\n", view);
                std::abort();
            }
        close(fd);   // os mapeamentos mantêm a memória viva
        Base = static_cast<uint8_t*>(area);
    }

    void Commit(uint32_t guest, uint32_t size)
    {
        if (size == 0 || InPhysicalView(guest))
            return;   // as faixas físicas já estão liberadas
        static const uintptr_t page = uintptr_t(sysconf(_SC_PAGESIZE));
        uintptr_t start = uintptr_t(Base + guest) & ~(page - 1);
        uintptr_t end = (uintptr_t(Base + guest) + size + page - 1) & ~(page - 1);
        if (mprotect(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_WRITE) != 0)
        {
            fprintf(stderr, "[mem] falha ao confirmar 0x%08X (+0x%X)\n", guest, size);
            std::abort();
        }
    }
#endif

    uint32_t AllocVirtual(uint32_t size, uint32_t align)
    {
        uint32_t a = Bump(nextVirtual, 0x80000000, size, align, "memória virtual");
        static bool log = getenv("DMC3_LOG_ALLOC") != nullptr;
        if (log)
            fprintf(stderr, "[mem] AllocVirtual %08X-%08X\n", a, a + size);
        return a;
    }

    uint32_t AllocPhysical(uint32_t size, uint32_t align)
    {
        return Bump(nextPhysical, 0xC0000000, size, align, "memória física");
    }
}
