#include "kernel.h"

#include <map>
#include <mutex>
#include <string>

namespace kernel
{
    thread_local ImportTrace* CurrentTrace = nullptr;
    thread_local PPCContext* CurrentContext = nullptr;

    uint32_t CallGuest(const PPCContext& parent, uint32_t function, std::initializer_list<uint32_t> args)
    {
        static thread_local PPCContext call;
        call = parent;
        call.r1.u64 = (parent.r1.u32 - 0x4000) & ~0xFull;
        call.lr = 0;
        PPCRegister* regs[] = { &call.r3, &call.r4, &call.r5, &call.r6, &call.r7, &call.r8, &call.r9, &call.r10 };
        size_t i = 0;
        for (uint32_t a : args)
            if (i < 8)
                regs[i++]->u64 = a;
        (PPC_LOOKUP_FUNC(Base, function))(call, Base);
        return call.r3.u32;
    }

    void Unimplemented(const char* name, PPCContext& ctx)
    {
        Trace(name, uint32_t(ctx.lr));
        // registra as primeiras chamadas de cada função e depois só conta
        static std::mutex lock;
        static std::map<std::string, uint64_t> calls;
        std::lock_guard guard(lock);
        uint64_t n = ++calls[name];
        if (n <= 3)
            fprintf(stderr, "[kernel] não implementada: %s (r3=%08X r4=%08X r5=%08X, retorno em %08X)%s\n",
                name, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, uint32_t(ctx.lr), n == 3 ? " [silenciando]" : "");
        ctx.r3.u64 = 0;
    }
}
