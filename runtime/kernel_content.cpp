// Conteúdo do XAM (saves): XamContent*, XamEnumerate e a montagem de raízes como "save:".
// Cada conteúdo vira uma pasta do host: <DMC3_SAVE_DIR ou "saves">/<nome do arquivo do conteúdo>.
#include "kernel.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr uint32_t X_ERROR_SUCCESS = 0;
    constexpr uint32_t X_ERROR_FILE_NOT_FOUND = 2;
    constexpr uint32_t X_ERROR_NO_MORE_FILES = 18;
    constexpr uint32_t X_ERROR_ALREADY_EXISTS = 183;
    constexpr uint32_t X_ERROR_IO_PENDING = 997;
    constexpr uint32_t X_ERROR_DEVICE_NOT_CONNECTED = 0x48F;
    constexpr uint32_t HardDrive = 1;

    // XCONTENT_DATA { u32 DeviceID; u32 ContentType; u16 DisplayName[128] (UTF-16 BE); char FileName[42]; u8 pad[2] }
    constexpr uint32_t ContentDataSize = 0x134;

    std::mutex lock;
    std::map<std::string, std::string> mounts;   // dispositivo em minúsculas -> pasta do host

    struct Enumerator
    {
        std::vector<std::pair<std::string, std::u16string>> items;   // nome do arquivo, nome de exibição
        size_t next = 0;
        uint32_t contentType = 0;
    };
    std::map<uint32_t, Enumerator> enumerators;
    uint32_t nextEnumerator = 0xFA000004;

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(tolower(c)); });
        return s;
    }

    fs::path SaveRoot()
    {
        const char* env = getenv("DMC3_SAVE_DIR");
        fs::path root = env ? env : "saves";
        fs::create_directories(root);
        return root;
    }

    std::string FileNameOf(uint32_t contentData)
    {
        const char* name = kernel::Guest<char>(contentData + 264);
        return std::string(name, strnlen(name, 42));
    }

    std::u16string DisplayNameOf(uint32_t contentData)
    {
        std::u16string s;
        for (int i = 0; i < 128; i++)
        {
            char16_t c = char16_t(__builtin_bswap16(*kernel::Guest<uint16_t>(contentData + 8 + 2 * i)));
            if (!c)
                break;
            s += c;
        }
        return s;
    }

    // nome de arquivo do conteúdo sem nada que saia da pasta de saves
    bool SafeName(const std::string& name)
    {
        return !name.empty() && name.find_first_of("\\/:*?\"<>|") == std::string::npos && name != "." && name != "..";
    }

    uint32_t Finish(PPCContext& ctx, uint32_t overlapped, uint32_t result)
    {
        if (overlapped)
        {
            kernel::CompleteOverlapped(overlapped, result);
            return X_ERROR_IO_PENDING;
        }
        return result;
    }

    uint32_t StackArg(PPCContext& ctx, int index)   // argumentos a partir do 9º: r1 + 0x54 + 8 * (n - 8)
    {
        return kernel::Load32(ctx.r1.u32 + 0x54 + 8 * (index - 8));
    }
}

namespace kernel
{
    std::string MountedPath(const std::string& device)
    {
        std::lock_guard guard(lock);
        auto it = mounts.find(Lower(device));
        return it == mounts.end() ? std::string() : it->second;
    }

    void Mount(const std::string& device, const std::string& hostPath)
    {
        std::lock_guard guard(lock);
        mounts[Lower(device)] = hostPath;
    }

    void Unmount(const std::string& device)
    {
        std::lock_guard guard(lock);
        mounts.erase(Lower(device));
    }
}

// uint32 XamContentGetDeviceData(uint32 DeviceID, XDEVICE_DATA*)
// XDEVICE_DATA { u32 DeviceID; u32 DeviceType; u64 ulDeviceBytes; u64 ulDeviceFreeBytes; u16 wszName[28] }
KERNEL_IMPL(XamContentGetDeviceData)
{
    uint32_t device = ctx.r3.u32, data = ctx.r4.u32;
    if (device != HardDrive)
    {
        ctx.r3.u64 = X_ERROR_DEVICE_NOT_CONNECTED;
        return;
    }
    memset(kernel::Guest<uint8_t>(data), 0, 0x50);
    kernel::Store32(data + 0, HardDrive);
    kernel::Store32(data + 4, 1);   // disco rígido
    *kernel::Guest<uint64_t>(data + 8) = __builtin_bswap64(4ull << 30);    // 4 GB
    *kernel::Guest<uint64_t>(data + 16) = __builtin_bswap64(3ull << 30);   // 3 GB livres
    const char16_t name[] = u"Hard Drive";
    for (size_t i = 0; name[i]; i++)
        *kernel::Guest<uint16_t>(data + 24 + 2 * uint32_t(i)) = __builtin_bswap16(uint16_t(name[i]));
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// uint32 XamContentGetDeviceState(uint32 DeviceID, XOVERLAPPED*)
KERNEL_IMPL(XamContentGetDeviceState)
{
    uint32_t result = ctx.r3.u32 == HardDrive ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
    ctx.r3.u64 = Finish(ctx, ctx.r4.u32, result);
}

// uint32 XamContentCreateEnumerator(uint32 UserIndex, uint32 DeviceID, uint32 ContentType, uint32 ContentFlags,
//                                   uint32 ItemsPerEnumerate, uint32* BufferSize, HANDLE* Enumerator)
KERNEL_IMPL(XamContentCreateEnumerator)
{
    uint32_t device = ctx.r4.u32, type = ctx.r5.u32, items = ctx.r7.u32, pSize = ctx.r8.u32, pHandle = ctx.r9.u32;
    Enumerator e;
    e.contentType = type;
    if (device == 0 || device == HardDrive)
        for (auto& entry : fs::directory_iterator(SaveRoot()))
            if (entry.is_directory())
            {
                std::u16string display;
                if (FILE* f = platform::OpenFile(entry.path() / ".display", "rb"))
                {
                    char16_t c;
                    while (fread(&c, 2, 1, f) == 1)
                        display += c;
                    fclose(f);
                }
                e.items.emplace_back(entry.path().filename().string(), display);
            }
    std::lock_guard guard(lock);
    uint32_t handle = nextEnumerator;
    nextEnumerator += 4;
    fprintf(stderr, "[content] enumerador %08X: tipo %u, %zu conteúdos em %s\n", handle, type, e.items.size(),
        SaveRoot().string().c_str());
    enumerators[handle] = std::move(e);
    if (pSize)
        kernel::Store32(pSize, std::max<uint32_t>(items, 1) * ContentDataSize);
    if (pHandle)
        kernel::Store32(pHandle, handle);
    ctx.r3.u64 = X_ERROR_SUCCESS;
}

// uint32 XamEnumerate(HANDLE Enumerator, uint32 Flags, void* Buffer, uint32 BufferSize, uint32* ItemsReturned, XOVERLAPPED*)
KERNEL_IMPL(XamEnumerate)
{
    uint32_t handle = ctx.r3.u32, buffer = ctx.r5.u32, size = ctx.r6.u32, pItems = ctx.r7.u32, overlapped = ctx.r8.u32;
    std::lock_guard guard(lock);
    auto it = enumerators.find(handle);
    uint32_t count = 0;
    if (it != enumerators.end())
    {
        Enumerator& e = it->second;
        while (e.next < e.items.size() && (count + 1) * ContentDataSize <= size)
        {
            uint32_t entry = buffer + count * ContentDataSize;
            memset(kernel::Guest<uint8_t>(entry), 0, ContentDataSize);
            kernel::Store32(entry + 0, HardDrive);
            kernel::Store32(entry + 4, e.contentType);
            const auto& [file, display] = e.items[e.next++];
            for (size_t i = 0; i < display.size() && i < 127; i++)
                *kernel::Guest<uint16_t>(entry + 8 + 2 * uint32_t(i)) = __builtin_bswap16(uint16_t(display[i]));
            memcpy(kernel::Guest<char>(entry + 264), file.c_str(), std::min<size_t>(file.size(), 41));
            count++;
        }
    }
    if (pItems)
        kernel::Store32(pItems, count);
    if (overlapped)
        kernel::Store32(overlapped + 4, count);   // InternalHigh = itens devolvidos
    ctx.r3.u64 = Finish(ctx, overlapped, count ? X_ERROR_SUCCESS : X_ERROR_NO_MORE_FILES);
}

// uint32 XamContentCreateEx(uint32 UserIndex, const char* RootName, const XCONTENT_DATA*, uint32 Flags,
//                           uint32* Disposition, uint32* LicenseMask, uint32 CacheSize, uint64 ContentSize, XOVERLAPPED*)
KERNEL_IMPL(XamContentCreateEx)
{
    std::string root = kernel::Guest<char>(ctx.r4.u32);
    uint32_t data = ctx.r5.u32, flags = ctx.r6.u32, pDisposition = ctx.r7.u32, pLicense = ctx.r8.u32;
    uint32_t overlapped = StackArg(ctx, 9);
    std::string name = FileNameOf(data);
    if (!SafeName(name))
    {
        fprintf(stderr, "[content] nome de conteúdo inválido: \"%s\"\n", name.c_str());
        ctx.r3.u64 = Finish(ctx, overlapped, X_ERROR_FILE_NOT_FOUND);
        return;
    }
    fs::path dir = SaveRoot() / name;
    bool exists = fs::exists(dir);
    uint32_t mode = flags & 0xF;   // 1 CREATE_NEW, 2 CREATE_ALWAYS, 3 OPEN_EXISTING, 4 OPEN_ALWAYS, 5 TRUNCATE_EXISTING
    uint32_t result = X_ERROR_SUCCESS, disposition = exists ? 2 : 1;   // 1 = criado, 2 = aberto
    if (mode == 1 && exists)
        result = X_ERROR_ALREADY_EXISTS;
    else if ((mode == 3 || mode == 5) && !exists)
        result = X_ERROR_FILE_NOT_FOUND;
    if (result == X_ERROR_SUCCESS)
    {
        if (!exists)
        {
            fs::create_directories(dir);
            std::u16string display = DisplayNameOf(data);
            if (FILE* f = platform::OpenFile(dir / ".display", "wb"))
            {
                fwrite(display.data(), 2, display.size(), f);
                fclose(f);
            }
        }
        kernel::Mount(root, dir.string());
        if (pDisposition)
            kernel::Store32(pDisposition, disposition);
        if (pLicense)
            kernel::Store32(pLicense, 0);
    }
    fprintf(stderr, "[content] XamContentCreateEx(\"%s\", \"%s\", modo %u) -> %s, resultado %u\n", root.c_str(), name.c_str(),
        mode, result ? "falhou" : disposition == 1 ? "criado" : "aberto", result);
    ctx.r3.u64 = Finish(ctx, overlapped, result);
}

// uint32 XamContentClose(const char* RootName, XOVERLAPPED*)
KERNEL_IMPL(XamContentClose)
{
    std::string root = kernel::Guest<char>(ctx.r3.u32);
    kernel::Unmount(root);
    ctx.r3.u64 = Finish(ctx, ctx.r4.u32, X_ERROR_SUCCESS);
}

// uint32 XamContentGetCreator(uint32 UserIndex, const XCONTENT_DATA*, BOOL* IsCreator, uint64* Xuid, XOVERLAPPED*)
KERNEL_IMPL(XamContentGetCreator)
{
    if (ctx.r5.u32)
        kernel::Store32(ctx.r5.u32, 1);
    if (ctx.r6.u32)
        *kernel::Guest<uint64_t>(ctx.r6.u32) = __builtin_bswap64(0xE000000000000001ull);   // XUID local do jogador 1
    ctx.r3.u64 = Finish(ctx, ctx.r7.u32, X_ERROR_SUCCESS);
}

// uint32 XamContentDelete(uint32 UserIndex, const XCONTENT_DATA*, XOVERLAPPED*)
KERNEL_IMPL(XamContentDelete)
{
    std::string name = FileNameOf(ctx.r4.u32);
    uint32_t result = X_ERROR_FILE_NOT_FOUND;
    if (SafeName(name))
    {
        fs::path dir = SaveRoot() / name;
        if (fs::exists(dir))
        {
            fs::remove_all(dir);
            result = X_ERROR_SUCCESS;
        }
    }
    fprintf(stderr, "[content] XamContentDelete(\"%s\") -> %u\n", name.c_str(), result);
    ctx.r3.u64 = Finish(ctx, ctx.r5.u32, result);
}
