// Arquivos: NtCreateFile/NtOpenFile/NtReadFile e companhia.
// Os caminhos do console ("game:\", "d:\", "\Device\Cdrom0\"...) apontam para uma pasta do host
// que espelha o disco (por padrão x360\, que tem DMC3.xex e Media\).
#include "kernel.h"
#include "gpu.h"
#include "platform.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    constexpr uint32_t X_STATUS_SUCCESS = 0;
    constexpr uint32_t X_STATUS_NO_MORE_FILES = 0x80000006;
    constexpr uint32_t X_STATUS_INVALID_HANDLE = 0xC0000008;
    constexpr uint32_t X_STATUS_END_OF_FILE = 0xC0000011;
    constexpr uint32_t X_STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034;
    constexpr uint32_t X_STATUS_NOT_IMPLEMENTED = 0xC0000002;
    constexpr uint32_t AttributeDirectory = 0x10, AttributeNormal = 0x80;

    // entrada de pasta (o que NtQueryDirectoryFile devolve)
    struct DirectoryEntry
    {
        std::string name;
        uint64_t size = 0, time = 0;   // tempo em FILETIME (100 ns desde 1601)
        bool directory = false;
    };

    struct GuestFile
    {
        FILE* host = nullptr;
        std::string path;
        bool directory = false;
        uint64_t position = 0;
        std::vector<DirectoryEntry> entries;   // enumeração de pasta em andamento
        size_t nextEntry = 0;
        bool enumerated = false;
    };

    // E/S de 64 bits nos dois sistemas
    bool Seek(FILE* f, uint64_t position)
    {
#ifdef _WIN32
        return _fseeki64(f, int64_t(position), SEEK_SET) == 0;
#else
        return fseeko(f, off_t(position), SEEK_SET) == 0;
#endif
    }

    // hora de modificação do arquivo em FILETIME
    uint64_t FileTime(const fs::path& path)
    {
        std::error_code error;
        auto t = fs::last_write_time(path, error);
        if (error)
            return 0;
        // relógio do sistema de arquivos -> relógio do sistema pela diferença até agora (o file_clock::to_sys
        // não existe em todas as bibliotecas C++)
        auto system = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(
            t - fs::file_time_type::clock::now());
        auto since = std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
        return (uint64_t(since) + 11644473600ull) * 10000000ull;
    }

    // máscara do NtQueryDirectoryFile ("*", "*.wmv", "m01_?00.*"), sem diferenciar maiúsculas
    bool Matches(const char* mask, const char* name)
    {
        if (!*mask)
            return !*name;
        if (*mask == '*')
            return Matches(mask + 1, name) || (*name && Matches(mask, name + 1));
        return *name && (*mask == '?' || tolower(uint8_t(*mask)) == tolower(uint8_t(*name))) && Matches(mask + 1, name + 1);
    }

    std::map<uint32_t, GuestFile> files;
    std::recursive_mutex filesLock;   // protege files; cada função exportada trava na entrada
    uint32_t nextHandle = 0x1000;

    std::string& Root()
    {
        static std::string root = [] {
            const char* env = getenv("DMC3_DISC");
            return std::string(env ? env : "x360");
        }();
        return root;
    }

    // "\Device\Cdrom0\Media\x.zip", "game:\Media\x.zip", "d:\Media\x.zip" -> <raiz>\Media\x.zip
    std::string HostPath(std::string guest)
    {
        for (const char* prefix : { "\\Device\\Cdrom0\\", "\\Device\\Harddisk0\\Partition1\\", "\\??\\" })
            if (strncasecmp(guest.c_str(), prefix, strlen(prefix)) == 0)
                guest = guest.substr(strlen(prefix));
        std::string root = Root();
        size_t colon = guest.find(':');
        if (colon != std::string::npos && colon < 16)
        {
            // dispositivos montados pelo runtime (ex.: a raiz de um save) têm pasta própria; o resto é o disco
            std::string mounted = kernel::MountedPath(guest.substr(0, colon));
            if (!mounted.empty())
                root = mounted;
            guest = guest.substr(colon + 1);
        }
        while (!guest.empty() && (guest[0] == '\\' || guest[0] == '/'))
            guest.erase(0, 1);
        // separador '/' nos dois sistemas (o Windows também aceita)
        for (char& c : guest)
            if (c == '\\')
                c = '/';
        return guest.empty() ? root : root + "/" + guest;
    }

    // OBJECT_ATTRIBUTES { uint32 RootDirectory; ANSI_STRING* ObjectName; uint32 Attributes }
    std::string ObjectName(uint32_t attributes)
    {
        uint32_t ansi = kernel::Load32(attributes + 4);
        uint16_t length = __builtin_bswap16(*kernel::Guest<uint16_t>(ansi));
        std::string name(kernel::Guest<char>(kernel::Load32(ansi + 4)), length);
        uint32_t root = kernel::Load32(attributes);
        if (root != 0 && files.count(root))
            return files[root].path + "/" + name;   // já é caminho do host
        return name;
    }

    void SetIoStatus(uint32_t block, uint32_t status, uint32_t information)
    {
        if (block)
        {
            kernel::Store32(block, status);
            kernel::Store32(block + 4, information);
        }
    }

    void Store64(uint32_t address, uint64_t value)
    {
        *kernel::Guest<uint64_t>(address) = __builtin_bswap64(value);
    }

    // abre o arquivo; devolve o status do NT
    uint32_t Open(uint32_t pHandle, uint32_t attributes, uint32_t ioStatus, bool create, const char* who)
    {
        std::string guest = ObjectName(attributes);
        std::string host = kernel::Load32(attributes) && files.count(kernel::Load32(attributes)) ? guest : HostPath(guest);
        std::error_code error;
        fs::file_status status = fs::status(host, error);
        bool exists = !error && fs::exists(status);
        fprintf(stderr, "[file] %s \"%s\" -> %s%s\n", who, guest.c_str(), host.c_str(), exists ? "" : " (não existe)");

        GuestFile f;
        f.path = host;
        if (exists && fs::is_directory(status))
        {
            f.directory = true;
        }
        else
        {
            // leitura e escrita sem truncar; criar só quando pedido e o arquivo não existe
            f.host = fopen(host.c_str(), create ? "r+b" : "rb");
            if (!f.host && create && !exists)
                f.host = fopen(host.c_str(), "w+b");
            if (!f.host)
            {
                SetIoStatus(ioStatus, X_STATUS_OBJECT_NAME_NOT_FOUND, 0);
                return X_STATUS_OBJECT_NAME_NOT_FOUND;
            }
        }
        uint32_t handle = nextHandle;
        nextHandle += 4;
        files[handle] = f;
        kernel::Store32(pHandle, handle);
        SetIoStatus(ioStatus, X_STATUS_SUCCESS, 1);   // FILE_OPENED
        return X_STATUS_SUCCESS;
    }

    // ByteOffset de NtReadFile/NtWriteFile: nulo ou FILE_USE_FILE_POINTER_POSITION (-2) = posição atual;
    // FILE_WRITE_TO_END_OF_FILE (-1) = fim do arquivo
    void SetPositionFromOffset(GuestFile& f, uint32_t pOffset);

    bool LogReads()
    {
        static bool on = getenv("DMC3_LOG_READS") != nullptr;
        return on;
    }

    uint64_t FileSize(const GuestFile& f)
    {
        std::error_code error;
        uint64_t size = f.directory ? 0 : fs::file_size(f.path, error);
        return error ? 0 : size;
    }
}

std::string kernel::HostPathOf(const std::string& guest)
{
    return HostPath(guest);
}

namespace
{
    void SetPositionFromOffset(GuestFile& f, uint32_t pOffset)
    {
        if (!pOffset)
            return;
        int64_t offset = int64_t(__builtin_bswap64(*kernel::Guest<uint64_t>(pOffset)));
        if (offset == -1)
            f.position = FileSize(f);
        else if (offset >= 0)
            f.position = uint64_t(offset);
    }
}

// NTSTATUS NtCreateFile(HANDLE* FileHandle, uint32 DesiredAccess, OBJECT_ATTRIBUTES*, IO_STATUS_BLOCK*,
//                       int64* AllocationSize, uint32 FileAttributes, uint32 ShareAccess, uint32 CreateDisposition, uint32 CreateOptions)
KERNEL_IMPL(NtCreateFile)
{
    std::lock_guard lock(filesLock);
    // CreateDisposition 1 = FILE_OPEN: só abrir; o resto pode criar
    bool create = ctx.r10.u32 != 1;
    ctx.r3.u64 = Open(ctx.r3.u32, ctx.r5.u32, ctx.r6.u32, create, "NtCreateFile");
}

// NTSTATUS NtOpenFile(HANDLE* FileHandle, uint32 DesiredAccess, OBJECT_ATTRIBUTES*, IO_STATUS_BLOCK*, uint32 ShareAccess, uint32 OpenOptions)
KERNEL_IMPL(NtOpenFile)
{
    std::lock_guard lock(filesLock);
    ctx.r3.u64 = Open(ctx.r3.u32, ctx.r5.u32, ctx.r6.u32, false, "NtOpenFile");
}

// NTSTATUS NtReadFile(HANDLE, HANDLE Event, void* ApcRoutine, void* ApcContext, IO_STATUS_BLOCK*,
//                     void* Buffer, uint32 Length, int64* ByteOffset)
KERNEL_IMPL(NtReadFile)
{
    std::lock_guard lock(filesLock);
    auto it = files.find(ctx.r3.u32);
    uint32_t ioStatus = ctx.r7.u32, buffer = ctx.r8.u32, length = ctx.r9.u32, pOffset = ctx.r10.u32;
    if (it == files.end() || it->second.directory)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    GuestFile& f = it->second;
    SetPositionFromOffset(f, pOffset);
    uint64_t start = platform::NowNs();
    uint32_t read = Seek(f.host, f.position) ? uint32_t(fread(kernel::Guest<uint8_t>(buffer), 1, length, f.host)) : 0;
    gpu::BlockedTime(0, platform::NowNs() - start);
    if (LogReads())
        fprintf(stderr, "[file] NtReadFile %s @ %llu: pedidos %u, lidos %u\n", f.path.c_str(),
            (unsigned long long)f.position, length, read);
    f.position += read;
    uint32_t status = read == 0 && length != 0 ? X_STATUS_END_OF_FILE : X_STATUS_SUCCESS;
    SetIoStatus(ioStatus, status, read);
    if (ctx.r4.u32)
        kernel::SignalEventHandle(ctx.r4.u32);   // a leitura é síncrona: o evento já pode ser sinalizado
    ctx.r3.u64 = status;
}

// NTSTATUS NtWriteFile(HANDLE, HANDLE Event, void* ApcRoutine, void* ApcContext, IO_STATUS_BLOCK*,
//                      const void* Buffer, uint32 Length, int64* ByteOffset)
KERNEL_IMPL(NtWriteFile)
{
    std::lock_guard lock(filesLock);
    auto it = files.find(ctx.r3.u32);
    uint32_t ioStatus = ctx.r7.u32, buffer = ctx.r8.u32, length = ctx.r9.u32, pOffset = ctx.r10.u32;
    if (it == files.end() || it->second.directory)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    GuestFile& f = it->second;
    SetPositionFromOffset(f, pOffset);
    uint32_t written = Seek(f.host, f.position) ? uint32_t(fwrite(kernel::Guest<uint8_t>(buffer), 1, length, f.host)) : 0;
    fflush(f.host);
    if (LogReads())
    {
        uint32_t used = 0;
        for (uint32_t i = 0; i < length; i++)
            used += *kernel::Guest<uint8_t>(buffer + i) != 0xFF;
        fprintf(stderr, "[file] NtWriteFile %s @ %llu: %u bytes (%u diferentes de FF), buffer %08X, retorno em %08X\n",
            f.path.c_str(), (unsigned long long)f.position, length, used, buffer, uint32_t(ctx.lr));
    }
    f.position += written;
    SetIoStatus(ioStatus, X_STATUS_SUCCESS, written);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// NTSTATUS NtQueryInformationFile(HANDLE, IO_STATUS_BLOCK*, void* FileInformation, uint32 Length, uint32 FileInformationClass)
KERNEL_IMPL(NtQueryInformationFile)
{
    std::lock_guard lock(filesLock);
    auto it = files.find(ctx.r3.u32);
    uint32_t ioStatus = ctx.r4.u32, info = ctx.r5.u32, length = ctx.r6.u32, cls = ctx.r7.u32;
    if (it == files.end())
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    const GuestFile& f = it->second;
    uint64_t size = FileSize(f);
    memset(kernel::Guest<uint8_t>(info), 0, length);
    switch (cls)
    {
    case 5:    // FileStandardInformation { int64 AllocationSize, int64 EndOfFile, uint32 Links, uint8 DeletePending, uint8 Directory }
        Store64(info + 0, (size + 0xFFF) & ~0xFFFull);
        Store64(info + 8, size);
        kernel::Store32(info + 16, 1);
        *kernel::Guest<uint8_t>(info + 21) = f.directory;
        break;
    case 14:   // FilePositionInformation { int64 CurrentByteOffset }
        Store64(info, f.position);
        break;
    case 34:   // FileNetworkOpenInformation { 4x int64 tempos, int64 AllocationSize, int64 EndOfFile, uint32 FileAttributes }
        Store64(info + 32, (size + 0xFFF) & ~0xFFFull);
        Store64(info + 40, size);
        kernel::Store32(info + 48, f.directory ? AttributeDirectory : AttributeNormal);
        break;
    default:
        fprintf(stderr, "[file] NtQueryInformationFile: classe %u não tratada\n", cls);
        ctx.r3.u64 = X_STATUS_NOT_IMPLEMENTED;
        return;
    }
    SetIoStatus(ioStatus, X_STATUS_SUCCESS, length);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// NTSTATUS NtSetInformationFile(HANDLE, IO_STATUS_BLOCK*, void* FileInformation, uint32 Length, uint32 FileInformationClass)
KERNEL_IMPL(NtSetInformationFile)
{
    std::lock_guard lock(filesLock);
    auto it = files.find(ctx.r3.u32);
    if (it == files.end())
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    if (ctx.r7.u32 == 14)   // FilePositionInformation
        it->second.position = __builtin_bswap64(*kernel::Guest<uint64_t>(ctx.r5.u32));
    else
        fprintf(stderr, "[file] NtSetInformationFile: classe %u ignorada\n", ctx.r7.u32);
    SetIoStatus(ctx.r4.u32, X_STATUS_SUCCESS, 0);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// NTSTATUS NtQueryFullAttributesFile(OBJECT_ATTRIBUTES*, FILE_NETWORK_OPEN_INFORMATION*)
KERNEL_IMPL(NtQueryFullAttributesFile)
{
    std::lock_guard lock(filesLock);
    std::string guest = ObjectName(ctx.r3.u32);
    std::string host = HostPath(guest);
    std::error_code error;
    fs::file_status status = fs::status(host, error);
    bool ok = !error && fs::exists(status);
    fprintf(stderr, "[file] NtQueryFullAttributesFile \"%s\"%s\n", guest.c_str(), ok ? "" : " (não existe)");
    if (!ok)
    {
        ctx.r3.u64 = X_STATUS_OBJECT_NAME_NOT_FOUND;
        return;
    }
    uint32_t info = ctx.r4.u32;
    bool directory = fs::is_directory(status);
    uint64_t size = directory ? 0 : fs::file_size(host, error);
    memset(kernel::Guest<uint8_t>(info), 0, 56);
    Store64(info + 32, (size + 0xFFF) & ~0xFFFull);
    Store64(info + 40, size);
    kernel::Store32(info + 48, directory ? AttributeDirectory : AttributeNormal);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// NTSTATUS NtQueryDirectoryFile(HANDLE, HANDLE Event, void* ApcRoutine, void* ApcContext, IO_STATUS_BLOCK*,
//                               FILE_DIRECTORY_INFORMATION* Info, uint32 Length, ANSI_STRING* FileMask, uint8 RestartScan)
// Uma entrada por chamada: { uint32 NextEntryOffset, uint32 FileIndex, int64 Creation, LastAccess, LastWrite, Change,
//                            int64 EndOfFile, int64 AllocationSize, uint32 FileAttributes, uint32 FileNameLength, char FileName[] }
KERNEL_IMPL(NtQueryDirectoryFile)
{
    std::lock_guard lock(filesLock);
    auto it = files.find(ctx.r3.u32);
    uint32_t ioStatus = ctx.r7.u32, info = ctx.r8.u32, length = ctx.r9.u32, pMask = ctx.r10.u32;
    bool restart = *kernel::Guest<uint8_t>(ctx.r1.u32 + 0x54 + 3) != 0;
    if (it == files.end() || !it->second.directory)
    {
        ctx.r3.u64 = X_STATUS_INVALID_HANDLE;
        return;
    }
    GuestFile& f = it->second;
    if (!f.enumerated || restart)
    {
        std::string mask = "*";
        if (pMask)
        {
            uint16_t len = __builtin_bswap16(*kernel::Guest<uint16_t>(pMask));
            if (len)
                mask.assign(kernel::Guest<char>(kernel::Load32(pMask + 4)), len);
        }
        f.entries.clear();
        std::error_code error;
        for (const fs::directory_entry& d : fs::directory_iterator(f.path, error))
        {
            DirectoryEntry e;
            e.name = d.path().filename().string();
            if (!Matches(mask.c_str(), e.name.c_str()))
                continue;
            std::error_code ignored;
            e.directory = d.is_directory(ignored);
            e.size = e.directory ? 0 : d.file_size(ignored);
            e.time = FileTime(d.path());
            f.entries.push_back(e);
        }
        f.nextEntry = 0;
        f.enumerated = true;
        fprintf(stderr, "[file] NtQueryDirectoryFile %s/%s: %zu entradas\n", f.path.c_str(), mask.c_str(), f.entries.size());
    }
    if (f.nextEntry >= f.entries.size())
    {
        SetIoStatus(ioStatus, X_STATUS_NO_MORE_FILES, 0);
        ctx.r3.u64 = X_STATUS_NO_MORE_FILES;
        return;
    }
    const DirectoryEntry& e = f.entries[f.nextEntry++];
    uint32_t nameLength = uint32_t(e.name.size());
    if (64 + nameLength > length)
    {
        ctx.r3.u64 = 0x80000005;   // STATUS_BUFFER_OVERFLOW
        return;
    }
    memset(kernel::Guest<uint8_t>(info), 0, 64);
    kernel::Store32(info + 4, uint32_t(f.nextEntry - 1));
    for (uint32_t t = 8; t <= 32; t += 8)   // criação, acesso, escrita, mudança: a hora de modificação
        Store64(info + t, e.time);
    Store64(info + 40, e.size);
    Store64(info + 48, (e.size + 0xFFF) & ~0xFFFull);
    kernel::Store32(info + 56, e.directory ? AttributeDirectory : AttributeNormal);
    kernel::Store32(info + 60, nameLength);
    memcpy(kernel::Guest<char>(info + 64), e.name.data(), nameLength);
    SetIoStatus(ioStatus, X_STATUS_SUCCESS, 64 + nameLength);
    ctx.r3.u64 = X_STATUS_SUCCESS;
}

// NTSTATUS NtClose(HANDLE)
KERNEL_IMPL(NtClose)
{
    std::lock_guard lock(filesLock);
    auto it = files.find(ctx.r3.u32);
    if (it != files.end())
    {
        if (it->second.host)
            fclose(it->second.host);
        files.erase(it);
    }
    ctx.r3.u64 = X_STATUS_SUCCESS;
}
