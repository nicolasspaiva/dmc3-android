// Ponto de entrada do runtime: monta a memória do console, carrega a imagem do DMC3 e
// chama o ponto de entrada do XEX no código recompilado.
#include "kernel.h"
#include "android_setup.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>   // no Android, o main vira SDL_main, chamado pela atividade Java do SDL

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
namespace kernel
{
    bool HandleWatch(EXCEPTION_POINTERS* info);   // runtime/watch.cpp
}
#endif

namespace
{
    constexpr uint32_t EntryPoint = 0x8246A120;   // cabeçalho do XEX (0x00010100)

    // chamada indireta para um endereço que não é início de função recompilada
    PPC_FUNC(UnknownFunction)
    {
        fprintf(stderr, "\n[crash] chamada indireta para 0x%08X, que não é uma função recompilada (ctr=%08X, retorno em %08X)\n",
            ctx.ctr.u32, ctx.ctr.u32, uint32_t(ctx.lr));
        fflush(stderr);
        std::_Exit(4);
    }

    // tabela de funções que o PPC_LOOKUP_FUNC lê: um ponteiro para cada instrução do código
    void FillFunctionTable()
    {
        const uint32_t table = uint32_t(PPC_IMAGE_BASE + PPC_IMAGE_SIZE);
        kernel::Commit(table, uint32_t(PPC_CODE_SIZE * 2));
        for (uint32_t a = uint32_t(PPC_CODE_BASE); a < PPC_CODE_BASE + PPC_CODE_SIZE; a += 4)
            PPC_LOOKUP_FUNC(kernel::Base, a) = UnknownFunction;
        size_t count = 0;
        for (auto* m = PPCFuncMappings; m->host != nullptr; m++, count++)
            PPC_LOOKUP_FUNC(kernel::Base, m->guest) = m->host;
        fprintf(stderr, "[boot] %zu funções na tabela\n", count);
    }

    bool LoadImage(const char* path)
    {
        FILE* f = fopen(path, "rb");
        if (f == nullptr)
            return false;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        kernel::Commit(uint32_t(PPC_IMAGE_BASE), uint32_t(PPC_IMAGE_SIZE));
        size_t read = fread(kernel::Base + PPC_IMAGE_BASE, 1, std::min<size_t>(size, PPC_IMAGE_SIZE), f);
        fclose(f);
        fprintf(stderr, "[boot] imagem: %zu bytes em 0x%08llX\n", read, PPC_IMAGE_BASE);
        return read > 0;
    }

    // variáveis exportadas pelo kernel: cada uma ganha um bloco zerado na memória do console
    void FillImports()
    {
        for (auto* v = kernel::ImportedVariables; v->name != nullptr; v++)
        {
            uint32_t block = kernel::AllocPhysical(0x100, 0x10);
            kernel::Store32(v->slot, block);
            if (strcmp(v->name, "XboxKrnlVersion") == 0)
            {
                // 2.0.17559.0, a última versão do dashboard
                *kernel::Guest<uint16_t>(block + 0) = __builtin_bswap16(2);
                *kernel::Guest<uint16_t>(block + 4) = __builtin_bswap16(17559);
            }
            else if (strcmp(v->name, "ExLoadedCommandLine") == 0)
            {
                strcpy(kernel::Guest<char>(block), "\"DMC3.xex\"");
            }
        }
        size_t pointers = 0;
        for (auto* p = kernel::ImportedPointers; p->slot != 0; p++, pointers++)
            kernel::Store32(p->slot, p->thunk);
        fprintf(stderr, "[boot] importações: variáveis e %zu ponteiros de função preenchidos\n", pointers);
    }

    // pilha do jogo e última chamada ao kernel da thread que travou (comum aos dois sistemas)
    void ReportGuestState()
    {
        if (PPCContext* c = kernel::CurrentContext)
        {
            fprintf(stderr, "[crash] r1=%08X lr=%08X r3=%08X r4=%08X r5=%08X r30=%08X r31=%08X\n",
                c->r1.u32, uint32_t(c->lr), c->r3.u32, c->r4.u32, c->r5.u32, c->r30.u32, c->r31.u32);
            // cadeia de quadros do PowerPC: 0(r1) aponta para o quadro anterior, e o LR salvo fica em -8 dele
            fprintf(stderr, "[crash] pilha do jogo:");
            uint32_t sp = c->r1.u32;
            for (int i = 0; i < 16 && sp >= 0x40000000 && sp < 0x80000000; i++)
            {
                uint32_t previous = kernel::Load32(sp);
                if (previous <= sp || previous >= 0x80000000)
                    break;
                fprintf(stderr, " %08X", kernel::Load32(previous - 8));
                sp = previous;
            }
            fprintf(stderr, "\n");
        }
        if (kernel::CurrentTrace && kernel::CurrentTrace->name)
            fprintf(stderr, "[crash] última chamada ao kernel: %s, retorno em %08X\n", kernel::CurrentTrace->name, kernel::CurrentTrace->lr);
        fflush(stderr);
    }

#ifdef _WIN32
    LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
    {
        if (kernel::HandleWatch(info))
            return EXCEPTION_CONTINUE_EXECUTION;
        auto* record = info->ExceptionRecord;
        if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION && record->ExceptionCode != EXCEPTION_ILLEGAL_INSTRUCTION
            && record->ExceptionCode != EXCEPTION_BREAKPOINT)
            return EXCEPTION_CONTINUE_SEARCH;
        uintptr_t pc = info->ContextRecord->Rip;
        fprintf(stderr, "\n[crash] exceção 0x%08lX em host %p (perto de sub_%08X)\n",
            record->ExceptionCode, reinterpret_cast<void*>(pc), kernel::GuestFunctionAt(pc));
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        {
            uintptr_t address = record->ExceptionInformation[1];
            const char* kind = record->ExceptionInformation[0] ? "escrita" : "leitura";
            if (address >= uintptr_t(kernel::Base) && address < uintptr_t(kernel::Base) + PPC_MEMORY_SIZE)
                fprintf(stderr, "[crash] %s no endereço do console 0x%08llX\n", kind, address - uintptr_t(kernel::Base));
            else
                fprintf(stderr, "[crash] %s fora da memória do console: %p\n", kind, reinterpret_cast<void*>(address));
        }
        uintptr_t module = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        fprintf(stderr, "[crash] rva no executável: %llX\n", (unsigned long long)(pc - module));
        ReportGuestState();
        TerminateProcess(GetCurrentProcess(), 3);
        return EXCEPTION_CONTINUE_SEARCH;
    }
#else
    void OnSignal(int signal, siginfo_t* info, void*)
    {
        uintptr_t address = reinterpret_cast<uintptr_t>(info->si_addr);
        fprintf(stderr, "\n[crash] sinal %d", signal);
        if (address >= uintptr_t(kernel::Base) && address < uintptr_t(kernel::Base) + PPC_MEMORY_SIZE)
            fprintf(stderr, " no endereço do console 0x%08llX\n", (unsigned long long)(address - uintptr_t(kernel::Base)));
        else
            fprintf(stderr, " em %p\n", info->si_addr);
        ReportGuestState();
        _exit(3);
    }
#endif

    // Android: dados do jogo, saves e log na pasta Memória interna/DMC3 (android_setup.cpp: permissão, migração da pasta
    // antiga do app, conferência dos arquivos e conversão do DMC3.xex). Devolve false se o usuário escolheu sair
    bool SetupPaths(const char*& imagePath)
    {
#ifdef SDL_PLATFORM_ANDROID
        static std::string root;
        if (!androidsetup::Prepare(root))
            return false;
        static std::string image = root + "/DMC3.pe";
        imagePath = image.c_str();
        setenv("DMC3_DISC", root.c_str(), 0);
        setenv("DMC3_SAVE_DIR", (root + "/saves").c_str(), 0);
        setenv("DMC3_SCREENSHOT_DIR", root.c_str(), 0);
        freopen((root + "/dmc3_log.txt").c_str(), "w", stderr);
        setvbuf(stderr, nullptr, _IOLBF, 0);
        // env.txt na pasta do app: uma variável por linha (NOME=valor), para ligar diagnósticos sem recompilar
        if (FILE* f = fopen((root + "/env.txt").c_str(), "r"))
        {
            char line[512];
            while (fgets(line, sizeof(line), f))
            {
                std::string s = line;
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
                    s.pop_back();
                size_t eq = s.find('=');
                if (eq != std::string::npos && eq > 0 && s[0] != '#')
                {
                    setenv(s.substr(0, eq).c_str(), s.substr(eq + 1).c_str(), 1);
                    fprintf(stderr, "[boot] env.txt: %s\n", s.c_str());
                }
            }
            fclose(f);
        }
#else
        (void)imagePath;
#endif
        return true;
    }
}

uint32_t kernel::GuestFunctionAt(uintptr_t hostAddress)
    {
    static std::vector<std::pair<uintptr_t, uint32_t>> sorted;
    if (sorted.empty())
    {
        for (auto* m = PPCFuncMappings; m->host != nullptr; m++)
            sorted.emplace_back(reinterpret_cast<uintptr_t>(m->host), uint32_t(m->guest));
        std::sort(sorted.begin(), sorted.end());
    }
    auto it = std::upper_bound(sorted.begin(), sorted.end(), std::make_pair(hostAddress, UINT32_MAX));
    return it == sorted.begin() ? 0 : std::prev(it)->second;
}

int main(int argc, char** argv)
{
    const char* imagePath = argc > 1 ? argv[1] : "x360/DMC3.pe";
    if (!SetupPaths(imagePath))
        return 0;
#ifdef _WIN32
    AddVectoredExceptionHandler(1, OnException);
#else
    struct sigaction action{};
    action.sa_sigaction = OnSignal;
    action.sa_flags = SA_SIGINFO;
    for (int signal : { SIGSEGV, SIGBUS, SIGILL, SIGFPE })
        sigaction(signal, &action, nullptr);
#endif

    kernel::InitMemory();
    if (!LoadImage(imagePath))
    {
        fprintf(stderr, "[boot] não consegui ler a imagem %s\n", imagePath);
        return 1;
    }
    FillFunctionTable();
    FillImports();
    // registradores da GPU Xenos (0x7FC80000): por enquanto só memória comum, sem GPU por trás
    kernel::Commit(0x7FC80000, 0x10000);
    // registradores do decodificador de áudio XMA (0x7FEA0000): idem, sem hardware de áudio por trás
    kernel::Commit(0x7FEA0000, 0x10000);

    // DMC3_WATCHDOG=N: depois de N segundos, mostra onde cada thread do jogo está e encerra
    if (const char* watchdog = getenv("DMC3_WATCHDOG"))
    {
        static uint32_t seconds = uint32_t(atoi(watchdog));
        platform::StartThread([](void*) {
            platform::SleepMs(seconds * 1000);
            kernel::DumpThreads();
            fflush(stderr);
            std::_Exit(5);
        }, nullptr, 1024 * 1024);
    }

    fprintf(stderr, "[boot] chamando o ponto de entrada 0x%08X\n", EntryPoint);
    kernel::RunMainThread(EntryPoint);
    return 0;
}
