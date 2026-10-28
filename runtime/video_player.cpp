// Player nativo dos vídeos (ver video_player.h).
#include "video_player.h"

#ifdef DMC3_NO_FFMPEG
// sem FFmpeg (Android, por enquanto): nenhum vídeo abre, e o jogo pula todos
namespace videoplayer
{
    bool Open(const std::string&, bool) { return false; }
    void Stop() {}
    bool Playing() { return false; }
    bool Frame(std::vector<uint8_t>&, uint32_t&, uint32_t&, double&, bool&) { return false; }
}
#else

#include <SDL3/SDL.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

namespace
{
    constexpr int SampleRate = 48000;
    constexpr size_t MaxQueuedFrames = 6;
    constexpr int MaxQueuedAudio = SampleRate * 2 * int(sizeof(float)) / 2;   // meio segundo de estéreo float

    struct VideoFrame
    {
        double pts = 0;   // segundos desde o início (somando as voltas do laço)
        std::vector<uint8_t> rgba;
    };

    struct Player
    {
        std::string path;
        bool loop = false;
        std::thread thread;
        std::atomic<bool> stop{ false }, decoding{ true };
        std::mutex lock;
        std::condition_variable room;
        std::deque<VideoFrame> frames;
        uint32_t width = 0, height = 0;
        double aspect = 16.0 / 9;   // largura / altura na tela
        SDL_AudioStream* audio = nullptr;
        std::atomic<bool> started{ false };   // primeiro quadro apresentado: o relógio começa e o áudio sai
        uint64_t startNs = 0;
    };

    std::mutex playerLock;
    Player* current = nullptr;

    void Decode(Player& p)
    {
        AVFormatContext* format = nullptr;
        if (avformat_open_input(&format, p.path.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(format, nullptr) < 0)
        {
            fprintf(stderr, "[video] FFmpeg não abriu %s\n", p.path.c_str());
            p.decoding = false;
            return;
        }
        int videoIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        int audioIndex = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        auto openCodec = [&](int index) -> AVCodecContext* {
            if (index < 0)
                return nullptr;
            const AVCodec* codec = avcodec_find_decoder(format->streams[index]->codecpar->codec_id);
            AVCodecContext* c = codec ? avcodec_alloc_context3(codec) : nullptr;
            if (!c || avcodec_parameters_to_context(c, format->streams[index]->codecpar) < 0)
                return nullptr;
            static int threads = getenv("DMC3_VIDEO_THREADS") ? atoi(getenv("DMC3_VIDEO_THREADS")) : 4;
            c->thread_count = threads;
            if (avcodec_open2(c, codec, nullptr) < 0)
            {
                avcodec_free_context(&c);
                return nullptr;
            }
            return c;
        };
        AVCodecContext* video = openCodec(videoIndex);
        AVCodecContext* audio = openCodec(audioIndex);
        // proporção de exibição: os vídeos têm resolução de PS2 (512 de largura) com pixels não quadrados
        AVRational sar = videoIndex >= 0 ? av_guess_sample_aspect_ratio(format, format->streams[videoIndex], nullptr) : AVRational{ 0, 1 };
        if (video && video->height)
        {
            p.aspect = sar.num && sar.den ? double(video->width) * sar.num / sar.den / video->height : double(video->width) / video->height;
            // os arquivos dizem 1:1, mas 512 de largura é o vídeo do PS2 (512x448 em 4:3): pixel 7:6
            if (video->width == 512 && sar.num == sar.den)
                p.aspect *= 7.0 / 6.0;
        }
        fprintf(stderr, "[video] tocando %s: vídeo %s %dx%d (pixel %d:%d, proporção %.3f), áudio %s %d Hz\n", p.path.c_str(),
            video ? video->codec->name : "-", video ? video->width : 0, video ? video->height : 0, sar.num, sar.den, p.aspect,
            audio ? audio->codec->name : "-", audio ? audio->sample_rate : 0);

        SwsContext* scaler = nullptr;
        SwrContext* resampler = nullptr;
        if (audio)
        {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_FLT, SampleRate, &audio->ch_layout, audio->sample_fmt,
                audio->sample_rate, 0, nullptr);
            if (resampler && swr_init(resampler) < 0)
                swr_free(&resampler);
        }

        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        double offset = 0, lastPts = 0;   // laço: as voltas seguintes continuam o relógio
        std::vector<float> samples;

        auto takeVideo = [&] {
            while (avcodec_receive_frame(video, frame) == 0)
            {
                AVRational base = format->streams[videoIndex]->time_base;
                int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
                double pts = offset + (ts == AV_NOPTS_VALUE ? lastPts - offset : ts * av_q2d(base));
                lastPts = pts;
                scaler = sws_getCachedContext(scaler, frame->width, frame->height, AVPixelFormat(frame->format), frame->width,
                    frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                VideoFrame out;
                out.pts = pts;
                out.rgba.resize(size_t(frame->width) * frame->height * 4);
                uint8_t* planes[1] = { out.rgba.data() };
                int strides[1] = { frame->width * 4 };
                sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, planes, strides);
                std::lock_guard guard(p.lock);
                p.width = uint32_t(frame->width);
                p.height = uint32_t(frame->height);
                p.frames.push_back(std::move(out));
            }
        };
        auto takeAudio = [&] {
            while (avcodec_receive_frame(audio, frame) == 0)
            {
                if (!resampler || !p.audio)
                    continue;
                int count = swr_get_out_samples(resampler, frame->nb_samples);
                samples.resize(size_t(count) * 2);
                uint8_t* outPlanes[1] = { reinterpret_cast<uint8_t*>(samples.data()) };
                int got = swr_convert(resampler, outPlanes, count, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
                if (got > 0)
                    SDL_PutAudioStreamData(p.audio, samples.data(), got * 2 * int(sizeof(float)));
            }
        };

        // Os pacotes lidos ficam numa fila por trilha, e cada trilha é decodificada só quando precisa (vídeo: menos de
        // MaxQueuedFrames quadros prontos; áudio: menos de meio segundo na saída). Nesses arquivos o áudio vem até 1,5 s à
        // frente do vídeo: decodificando tudo na ordem do arquivo e segurando o áudio, o vídeo ficava sem quadros e
        // atrasava. Um pacote nulo na fila marca o fim do arquivo (esvazia o decodificador; no laço, volta ao começo).
        std::deque<AVPacket*> videoPackets, audioPackets;
        bool readEnd = false, videoEnd = !video, audioEnd = !audio;
        auto drop = [](std::deque<AVPacket*>& q) {
            for (AVPacket* k : q)
                av_packet_free(&k);
            q.clear();
        };
        while (!p.stop && !(videoEnd && audioEnd))
        {
            size_t ready;
            {
                std::lock_guard guard(p.lock);
                ready = p.frames.size();
            }
            // antes do primeiro quadro o áudio está pausado e não esvazia: não conta como cheio, senão nada começaria
            bool needVideo = !videoEnd && ready < MaxQueuedFrames;
            bool needAudio = !audioEnd && (!p.audio || !p.started || SDL_GetAudioStreamQueued(p.audio) < MaxQueuedAudio);
            if (needVideo && !videoPackets.empty())
            {
                AVPacket* k = videoPackets.front();
                videoPackets.pop_front();
                if (avcodec_send_packet(video, k) == 0 || !k)
                    takeVideo();
                if (!k)
                {
                    avcodec_flush_buffers(video);
                    offset = lastPts + 1.0 / 30;
                    videoEnd = !p.loop;
                }
                av_packet_free(&k);
            }
            else if (needAudio && !audioPackets.empty())
            {
                AVPacket* k = audioPackets.front();
                audioPackets.pop_front();
                if (avcodec_send_packet(audio, k) == 0 || !k)
                    takeAudio();
                if (!k)
                {
                    avcodec_flush_buffers(audio);
                    audioEnd = !p.loop;
                }
                av_packet_free(&k);
            }
            else if (((needVideo && videoPackets.empty()) || (needAudio && audioPackets.empty())) && !readEnd)
            {
                if (av_read_frame(format, packet) < 0)
                {
                    if (video)
                        videoPackets.push_back(nullptr);
                    if (audio)
                        audioPackets.push_back(nullptr);
                    if (p.loop)
                        av_seek_frame(format, -1, 0, AVSEEK_FLAG_BACKWARD);
                    else
                        readEnd = true;
                }
                else if (packet->stream_index == videoIndex && video)
                    videoPackets.push_back(av_packet_clone(packet));
                else if (packet->stream_index == audioIndex && audio)
                    audioPackets.push_back(av_packet_clone(packet));
                av_packet_unref(packet);
            }
            else
            {
                std::unique_lock guard(p.lock);
                p.room.wait_for(guard, std::chrono::milliseconds(3));
            }
        }
        drop(videoPackets);
        drop(audioPackets);
        av_frame_free(&frame);
        av_packet_free(&packet);
        sws_freeContext(scaler);
        swr_free(&resampler);
        avcodec_free_context(&video);
        avcodec_free_context(&audio);
        avformat_close_input(&format);
        p.decoding = false;
    }

    void Destroy(Player* p)
    {
        p->stop = true;
        p->room.notify_all();
        if (p->thread.joinable())
            p->thread.join();
        if (p->audio)
            SDL_DestroyAudioStream(p->audio);
        delete p;
    }
}

namespace videoplayer
{
    bool Open(const std::string& hostPath, bool loop)
    {
        Stop();
        FILE* f = fopen(hostPath.c_str(), "rb");
        if (!f)
            return false;
        fclose(f);
        auto* p = new Player;
        p->path = hostPath;
        p->loop = loop;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            SDL_AudioSpec spec{ SDL_AUDIO_F32, 2, SampleRate };
            // abre pausado: o som começa junto com o primeiro quadro na tela
            p->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        }
        p->thread = std::thread(Decode, std::ref(*p));
        std::lock_guard guard(playerLock);
        current = p;
        return true;
    }

    void Stop()
    {
        Player* p;
        {
            std::lock_guard guard(playerLock);
            p = current;
            current = nullptr;
        }
        if (p)
            Destroy(p);
    }

    bool Playing()
    {
        std::lock_guard guard(playerLock);
        if (!current)
            return false;
        std::lock_guard frames(current->lock);
        bool audioLeft = current->audio && current->started && SDL_GetAudioStreamQueued(current->audio) > 0;
        return current->decoding || !current->frames.empty() || audioLeft;
    }

    bool Frame(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height, double& aspect, bool& fresh)
    {
        fresh = false;
        std::lock_guard guard(playerLock);
        Player* p = current;
        if (!p)
            return false;
        std::lock_guard frames(p->lock);
        aspect = p->aspect;
        if (p->frames.empty() && !p->started)
            return true;
        uint64_t now = SDL_GetTicksNS();
        if (!p->started)
        {
            p->started = true;
            p->startNs = now - uint64_t(p->frames.front().pts * 1e9);
            if (p->audio)
                SDL_ResumeAudioStreamDevice(p->audio);
        }
        double clock = double(now - p->startNs) / 1e9;
        // o último quadro cuja hora já chegou (os anteriores são descartados)
        int taken = 0;
        double shownPts = 0;
        while (!p->frames.empty() && p->frames.front().pts <= clock)
        {
            rgba = std::move(p->frames.front().rgba);
            shownPts = p->frames.front().pts;
            fresh = true;
            taken++;
            p->frames.pop_front();
        }
        // DMC3_LOG_VIDEO_SYNC: a cada segundo, quantos quadros saíram, quantos foram descartados por atraso e o atraso
        static bool logSync = getenv("DMC3_LOG_VIDEO_SYNC") != nullptr;
        if (logSync)
        {
            static int shown = 0, dropped = 0, presents = 0, empty = 0;
            static double worst = 0, nextLog = 1;
            presents++;
            if (taken)
            {
                shown++;
                dropped += taken - 1;
                worst = std::max(worst, clock - shownPts);
            }
            if (!taken && p->frames.empty())
                empty++;
            if (clock >= nextLog)
            {
                fprintf(stderr, "[video] %.0fs: %d quadros, %d descartados, atraso máx %.0f ms, sem quadro pronto em %d/%d apresentações, áudio %d bytes\n",
                    clock, shown, dropped, worst * 1000, empty, presents, p->audio ? SDL_GetAudioStreamQueued(p->audio) : 0);
                shown = dropped = presents = empty = 0;
                worst = 0;
                nextLog = clock + 1;
            }
        }
        if (fresh)
        {
            width = p->width;
            height = p->height;
            p->room.notify_all();
        }
        return true;
    }
}
#endif
