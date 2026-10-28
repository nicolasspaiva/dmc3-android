// Áudio do kernel (XAudio*): o "driver de render" do console. O XAudio2 do jogo registra um cliente; o sistema
// chama o callback dele a cada quadro de áudio (256 amostras a 48 kHz, ~5,3 ms) e o jogo devolve o quadro mixado com
// XAudioSubmitRenderDriverFrame: 6 canais (FL, FR, C, LFE, SL, SR) x 256 floats big-endian, um canal depois do outro.
// Aqui uma thread do runtime chama o callback no ritmo da placa de som (mantém poucos quadros na fila do SDL) e cada
// quadro vira estéreo num SDL_AudioStream. A música (OGG) e os efeitos (bancos do PS2) o próprio jogo decodifica.
#include "kernel.h"

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <jni.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace
{
    constexpr uint32_t FrameSamples = 256, Channels = 6, SampleRate = 48000;
    // fila alvo: no PC 6 quadros (~32 ms); no Android 16 (~85 ms), porque a saída de áudio pede blocos maiores e
    // uma fila curta esvazia entre um pedido e outro (o som "estala")
#ifdef __ANDROID__
    constexpr int QueueFrames = 16;
#else
    constexpr int QueueFrames = 6;
#endif
    constexpr int QueueTarget = QueueFrames * FrameSamples * 2 * int(sizeof(float));
    uint32_t underruns = 0;   // vezes em que a fila estava vazia ao chegar um quadro (som cortado)

    // alto-falante do celular: a trilha do DMC3 tem grave forte e mixagem alta, e o alto-falante do S25 FE satura
    // com ela (a mesma gravação estoura também no player de música do aparelho; o sinal do jogo sai idêntico ao do PC).
    // Só no alto-falante o som passa por um passa-alta de 4ª ordem (dois biquads de Butterworth) e ganho menor; com fone
    // ou Bluetooth sai inteiro. DMC3_AUDIO_HIGHPASS (Hz, 0 desliga) e DMC3_AUDIO_GAIN valem para os dois casos
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1[2] = {}, x2[2] = {}, y1[2] = {}, y2[2] = {};

        void HighPass(float cut)
        {
            if (cut <= 0)
            {
                *this = Biquad();
                return;
            }
            float w = 2.0f * 3.14159265f * cut / SampleRate, cw = std::cos(w), alpha = std::sin(w) / (2.0f * 0.7071f);
            float a0 = 1 + alpha;
            b0 = (1 + cw) / 2 / a0;
            b1 = -(1 + cw) / a0;
            b2 = (1 + cw) / 2 / a0;
            a1 = -2 * cw / a0;
            a2 = (1 - alpha) / a0;
        }

        float Process(float v, int side)
        {
            float y = b0 * v + b1 * x1[side] + b2 * x2[side] - a1 * y1[side] - a2 * y2[side];
            x2[side] = x1[side];
            x1[side] = v;
            y2[side] = y1[side];
            y1[side] = y;
            return y;
        }
    };

    // a saída atual é o alto-falante do aparelho? (Android: DMC3Activity.isSpeakerOutput pelo JNI)
    bool SpeakerOutput()
    {
#ifdef __ANDROID__
        JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (!env || !activity)
            return true;
        jclass type = env->GetObjectClass(activity);
        jmethodID method = env->GetStaticMethodID(type, "isSpeakerOutput", "()Z");
        bool speaker = method ? env->CallStaticBooleanMethod(type, method) : true;
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            speaker = true;
        }
        env->DeleteLocalRef(type);
        env->DeleteLocalRef(activity);
        return speaker;
#else
        return false;
#endif
    }

    struct Output
    {
        Biquad stages[2];
        float gain = 1.0f;
        int mode = -1;   // 1 alto-falante, 0 fone/Bluetooth/PC
        uint32_t frames = 0;

        void Configure(bool speaker)
        {
            const char* g = getenv("DMC3_AUDIO_GAIN");
            const char* c = getenv("DMC3_AUDIO_HIGHPASS");
            // sem tratamento por padrão: o corte de grave no alto-falante piorou música e efeitos (teste no S25 FE);
            // só o env.txt liga o filtro
            float cut = c ? float(atof(c)) : 0.0f;
            gain = g ? float(atof(g)) : 1.0f;
            for (Biquad& stage : stages)
                stage.HighPass(cut);
            mode = speaker;
            fprintf(stderr, "[audio] saída: %s; ganho %.2f, passa-alta %.0f Hz\n", speaker ? "alto-falante" : "fone/Bluetooth/PC",
                gain, cut);
        }

        // a cada quadro de 256 amostras; a saída é conferida a cada ~2 s (fone plugado ou desconectado no meio do jogo)
        void Update()
        {
            if (frames++ % 375 == 0)
            {
                bool speaker = SpeakerOutput();
                if (int(speaker) != mode)
                    Configure(speaker);
            }
        }

        float Process(float v, int side)
        {
            for (Biquad& stage : stages)
                v = stage.Process(v, side);
            return v * gain;
        }
    };

    // limitador suave: abaixo de 0,8 o som passa igual; acima, a curva encosta em 1,0 sem cortar seco
    float Limit(float v)
    {
        float a = std::fabs(v);
        if (a <= 0.8f)
            return v;
        float limited = 0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f);
        return v < 0 ? -limited : limited;
    }

    SDL_AudioStream* stream = nullptr;
    uint32_t callback = 0, callbackArg = 0;
    std::atomic<bool> running{ false };

    bool OpenDevice()
    {
        if (stream)
            return true;
        // buffer do dispositivo: no Android o SDL abre em baixa latência, com blocos pequenos; com o jogo ocupando os
        // núcleos, a thread de áudio do sistema perde prazos e a música sai picotada. DMC3_AUDIO_FRAMES muda o tamanho
        // e DMC3_AUDIO_DRIVER escolhe o sistema (aaudio, opensles), também pelo env.txt
#ifdef __ANDROID__
        const char* frames = getenv("DMC3_AUDIO_FRAMES") ? getenv("DMC3_AUDIO_FRAMES") : "2048";
#else
        const char* frames = getenv("DMC3_AUDIO_FRAMES");
#endif
        if (frames)
            SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, frames);
        if (const char* driver = getenv("DMC3_AUDIO_DRIVER"))
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, driver);
        // sem o modo de baixa latência do AAudio: nele o S25 FE usa o caminho MMAP, que pula o mixer do Android (e o
        // processamento do alto-falante) e aplica o volume por fora, e a música alta sai distorcida.
        // DMC3_AUDIO_LOW_LATENCY=1 volta ao modo de baixa latência
        SDL_SetHint(SDL_HINT_ANDROID_LOW_LATENCY_AUDIO, getenv("DMC3_AUDIO_LOW_LATENCY") ? getenv("DMC3_AUDIO_LOW_LATENCY") : "0");
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            fprintf(stderr, "[audio] SDL_INIT_AUDIO falhou: %s\n", SDL_GetError());
            return false;
        }
        SDL_AudioSpec spec{ SDL_AUDIO_F32, 2, int(SampleRate) };
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream)
        {
            fprintf(stderr, "[audio] dispositivo de áudio indisponível: %s\n", SDL_GetError());
            return false;
        }
        SDL_ResumeAudioStreamDevice(stream);
        // o que o aparelho abriu de fato (formato, taxa, canais, tamanho do bloco)
        SDL_AudioSpec device{};
        int deviceFrames = 0;
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &device, &deviceFrames);
        fprintf(stderr, "[audio] saída aberta: estéreo, %u Hz; dispositivo (%s): formato %X, %d Hz, %d canais, blocos de %d amostras\n",
            SampleRate, SDL_GetCurrentAudioDriver(), unsigned(device.format), device.freq, device.channels, deviceFrames);
        return true;
    }

    // laço do driver: pede um quadro ao jogo no ritmo do relógio, um a cada 256 amostras (5,33 ms), como o hardware
    // do 360 faz, e só quando a fila do SDL está abaixo do alvo. Em rajada (o Android pede o som em blocos grandes) o
    // callback do FMOD só acorda a thread de mixagem, que não tem tempo de preparar os quadros seguintes: o jogo
    // entregava quadros fora de ordem e o som estalava
    void DriverLoop(PPCContext& ctx)
    {
        constexpr uint64_t Period = uint64_t(FrameSamples) * 1000000000ull / SampleRate;
        uint64_t next = platform::NowNs();
        while (running)
        {
            uint64_t now = platform::NowNs();
            if (stream && now >= next && SDL_GetAudioStreamQueued(stream) < QueueTarget)
            {
                kernel::CallGuest(ctx, callback, { callbackArg });
                // depois de uma pausa longa não tenta recuperar tudo de uma vez: no máximo 2 quadros de atraso
                next = std::max(next + Period, now - 2 * Period);
            }
            else
            {
                SDL_DelayNS(500000);
            }
        }
    }
}

// uint32 XAudioRegisterRenderDriverClient(const struct { u32 callback, callback_arg; }* client, u32* driver)
KERNEL_IMPL(XAudioRegisterRenderDriverClient)
{
    uint32_t client = ctx.r3.u32, driverOut = ctx.r4.u32;
    callback = kernel::Load32(client);
    // o callback recebe um ponteiro para uma palavra com o argumento (como o Xenia faz), não o argumento
    callbackArg = kernel::AllocPhysical(0x1000);
    kernel::Store32(callbackArg, kernel::Load32(client + 4));
    kernel::Store32(driverOut, 0x41550000);
    fprintf(stderr, "[audio] cliente de render: callback %08X (argumento %08X)\n", callback, callbackArg);
    if (OpenDevice() && !running.exchange(true))
        kernel::CreateHostedThread(DriverLoop, 0, 0, 0, 256 * 1024, false, nullptr, nullptr);
    ctx.r3.u64 = 0;
}

// uint32 XAudioUnregisterRenderDriverClient(u32 driver)
KERNEL_IMPL(XAudioUnregisterRenderDriverClient)
{
    running = false;
    ctx.r3.u64 = 0;
}

// uint32 XAudioSubmitRenderDriverFrame(u32 driver, const float* samples)
KERNEL_IMPL(XAudioSubmitRenderDriverFrame)
{
    // o FMOD passa um endereço virtual (buffer na imagem do jogo, 0x82xxxxxx); só um endereço físico puro
    // (abaixo de 0x20000000) precisa ir para o espelho da memória física em 0xA0000000
    uint32_t samples = ctx.r4.u32 && ctx.r4.u32 < 0x20000000u ? 0xA0000000u | ctx.r4.u32 : ctx.r4.u32;
    if (stream && samples)
    {
        auto channel = [&](uint32_t c, uint32_t i) {
            uint32_t bits = kernel::Load32(samples + 4 * (c * FrameSamples + i));
            float v;
            memcpy(&v, &bits, 4);
            return v;
        };
        // 5.1 -> estéreo: centro e traseiros a -3 dB em cada lado; o LFE fica de fora
        float stereo[FrameSamples * 2];
        constexpr float Side = 0.7071f;
        static Output output;
        output.Update();
        for (uint32_t i = 0; i < FrameSamples; i++)
        {
            float center = channel(2, i) * Side;
            stereo[2 * i] = Limit(output.Process(channel(0, i) + center + channel(4, i) * Side, 0));
            stereo[2 * i + 1] = Limit(output.Process(channel(1, i) + center + channel(5, i) * Side, 1));
        }
        static uint32_t submitted = 0;
        if (++submitted > 200 && SDL_GetAudioStreamQueued(stream) == 0)
            underruns++;
        SDL_PutAudioStreamData(stream, stereo, sizeof(stereo));
        // diagnóstico: DMC3_AUDIO_DUMP=<arquivo> grava os 6 canais crus (floats do host, 256 por canal por quadro)
        static FILE* dump = getenv("DMC3_AUDIO_DUMP") ? fopen(getenv("DMC3_AUDIO_DUMP"), "wb") : nullptr;
        if (dump)
            for (uint32_t i = 0; i < FrameSamples * Channels; i++)
            {
                float v = channel(i / FrameSamples, i % FrameSamples);
                fwrite(&v, 4, 1, dump);
            }
        // diagnóstico: a cada ~10 s, quadros recebidos e o pico do período
        static uint32_t frames = 0;
        static float peak = 0;
        for (float v : stereo)
            peak = std::max(peak, std::fabs(v));
        if (++frames % 1875 == 0)
        {
            uint32_t nonzero = 0;
            for (uint32_t i = 0; i < FrameSamples * Channels; i++)
                nonzero += kernel::Load32(samples + 4 * i) != 0;
            fprintf(stderr, "[audio] %u quadros, pico %.3f, %u falhas de fila (driver %08X, amostras %08X -> %08X, %u palavras não nulas)\n",
                frames, peak, underruns, ctx.r3.u32, ctx.r4.u32, samples, nonzero);
            peak = 0;
        }
    }
    ctx.r3.u64 = 0;
}

// uint32 XAudioGetSpeakerConfig(u32* config): 5.1 digital, como o Xenia responde
KERNEL_IMPL(XAudioGetSpeakerConfig)
{
    kernel::Store32(ctx.r3.u32, 0x00010001);
    ctx.r3.u64 = 0;
}

// uint32 XAudioGetVoiceCategoryVolume(u32 category, float* volume)
KERNEL_IMPL(XAudioGetVoiceCategoryVolume)
{
    float one = 1.0f;
    uint32_t bits;
    memcpy(&bits, &one, 4);
    kernel::Store32(ctx.r4.u32, bits);
    ctx.r3.u64 = 0;
}
