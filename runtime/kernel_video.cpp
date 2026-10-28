// Funções de vídeo do kernel (Vd*, XGetVideoMode). Não há GPU Xenos: as consultas respondem como
// um console em 720p e o VdSwap é o ponto em que o quadro termina.
#include "gpu.h"

#include <cstring>

namespace
{
    // X_VIDEO_MODE { u32 width, height, interlaced, widescreen, hidef; f32 refresh; u32 standard, unk_0x8A, unk_0x01; u32 reserved[3] }
    void FillVideoMode(uint32_t mode)
    {
        float refresh = 60.0f;
        uint32_t refreshBits;
        memcpy(&refreshBits, &refresh, 4);
        const uint32_t fields[] = { 1280, 720, 0, 1, 1, refreshBits, 1, 0x4A, 0x01, 0, 0, 0 };
        for (int i = 0; i < 12; i++)
            kernel::Store32(mode + 4 * i, fields[i]);
    }

    uint32_t interruptCallback = 0, interruptContext = 0;
    uint32_t systemCommandBuffer = 0;
}

KERNEL_IMPL(XGetVideoMode)
{
    FillVideoMode(ctx.r3.u32);
}

// void VdQueryVideoMode(X_VIDEO_MODE*)
KERNEL_IMPL(VdQueryVideoMode)
{
    FillVideoMode(ctx.r3.u32);
}

// uint32 VdQueryVideoFlags(): widescreen | 720p | 1080i
KERNEL_IMPL(VdQueryVideoFlags)
{
    ctx.r3.u64 = 0x7;
}

KERNEL_IMPL(VdInitializeEngines)
{
    ctx.r3.u64 = 1;
}

KERNEL_IMPL(VdShutdownEngines)
{
}

KERNEL_IMPL(VdIsHSIOTrainingSucceeded)
{
    ctx.r3.u64 = 1;
}

KERNEL_IMPL(VdRetrainEDRAM)
{
    ctx.r3.u64 = 0;
}

KERNEL_IMPL(VdRetrainEDRAMWorker)
{
    ctx.r3.u64 = 0;
}

// void VdGetCurrentDisplayGamma(uint32* Type, float* Power)
KERNEL_IMPL(VdGetCurrentDisplayGamma)
{
    float power = 2.22222233f;
    uint32_t bits;
    memcpy(&bits, &power, 4);
    kernel::Store32(ctx.r3.u32, 2);
    kernel::Store32(ctx.r4.u32, bits);
}

// void VdSetGraphicsInterruptCallback(void (*callback)(uint32 source, void* context), void* context)
KERNEL_IMPL(VdSetGraphicsInterruptCallback)
{
    interruptCallback = ctx.r3.u32;
    interruptContext = ctx.r4.u32;
    fprintf(stderr, "[video] callback de interrupção da GPU: %08X (contexto %08X)\n", interruptCallback, interruptContext);
}

// void VdGetSystemCommandBuffer(void** Buffer, uint32* Value)
KERNEL_IMPL(VdGetSystemCommandBuffer)
{
    if (systemCommandBuffer == 0)
        systemCommandBuffer = kernel::AllocPhysical(0x94000);
    kernel::Store32(ctx.r3.u32, systemCommandBuffer);
    kernel::Store32(ctx.r4.u32, 0);
}

KERNEL_IMPL(VdCallGraphicsNotificationRoutines)
{
    ctx.r3.u64 = 0;
}

KERNEL_IMPL(VdInitializeRingBuffer)
{
}

KERNEL_IMPL(VdEnableRingBufferRPtrWriteBack)
{
}

KERNEL_IMPL(VdSetSystemCommandBufferGpuIdentifierAddress)
{
}

KERNEL_IMPL(VdEnableDisableClockGating)
{
}

KERNEL_IMPL(VdPersistDisplay)
{
    ctx.r3.u64 = 0;
}

// void VdSwap(...): o D3D chama ao fim de cada quadro (dentro de D3DDevice_Swap)
KERNEL_IMPL(VdSwap)
{
    gpu::Present();
    ctx.r3.u64 = 0;
}
