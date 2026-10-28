// Renderizador em OpenGL 3.3 core com SDL3. Feito para trocar por OpenGL ES 3 no Android só no
// cabeçalho dos shaders e na criação do contexto.
#include "renderer.h"
#include "input.h"
#include "xenos_texture.h"
#include "style_names.h"
#include "style_switch.h"
#include "kernel.h"
#include "video_player.h"
#include "touch_controls.h"

#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <GLES3/gl32.h>   // OpenGL ES 3.2 do NDK: funções ligadas direto (libGLESv3)
#define DMC3_GLES 1
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>

namespace renderer
{
    namespace
    {
        SDL_Window* window = nullptr;
        SDL_GLContext context = nullptr;
        GLuint vao = 0, vbo = 0, ebo = 0, whiteTexture = 0, copyFbo = 0, depthCopyFbo = 0;
        // tamanho da janela (no Android, a tela inteira) e a área 16:9 onde a imagem do jogo aparece
        int WindowWidth = 1280, WindowHeight = 720;
        int ActiveMode = -1;   // modo de tela (touch::DisplayMode), lido uma vez por quadro no Present
        int OutX = 0, OutY = 0, OutW = 1280, OutH = 720;

#ifndef DMC3_GLES
        // funções do OpenGL acima da 1.1, carregadas pelo SDL (no OpenGL ES do Android elas vêm direto da biblioteca)
#define GL_FUNCTIONS(X)                                                       \
        X(PFNGLCREATESHADERPROC, glCreateShader)                              \
        X(PFNGLSHADERSOURCEPROC, glShaderSource)                              \
        X(PFNGLCOMPILESHADERPROC, glCompileShader)                            \
        X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                \
        X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                      \
        X(PFNGLDELETESHADERPROC, glDeleteShader)                              \
        X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                            \
        X(PFNGLATTACHSHADERPROC, glAttachShader)                              \
        X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                \
        X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                              \
        X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                    \
        X(PFNGLUSEPROGRAMPROC, glUseProgram)                                  \
        X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                  \
        X(PFNGLUNIFORM1IPROC, glUniform1i)                                    \
        X(PFNGLUNIFORM1FPROC, glUniform1f)                                    \
        X(PFNGLUNIFORM2FPROC, glUniform2f)                                    \
        X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                  \
        X(PFNGLUNIFORM4FPROC, glUniform4f)                                    \
        X(PFNGLUNIFORM1UIVPROC, glUniform1uiv)                                \
        X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                        \
        X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                        \
        X(PFNGLGENBUFFERSPROC, glGenBuffers)                                  \
        X(PFNGLBINDBUFFERPROC, glBindBuffer)                                  \
        X(PFNGLBUFFERDATAPROC, glBufferData)                                  \
        X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                \
        X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)        \
        X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)      \
        X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                    \
        X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate)            \
        X(PFNGLBLENDCOLORPROC, glBlendColor)                                  \
        X(PFNGLSTENCILFUNCSEPARATEPROC, glStencilFuncSeparate)                \
        X(PFNGLSTENCILOPSEPARATEPROC, glStencilOpSeparate)                    \
        X(PFNGLSTENCILMASKSEPARATEPROC, glStencilMaskSeparate)                \
        X(PFNGLGENSAMPLERSPROC, glGenSamplers)                                \
        X(PFNGLBINDSAMPLERPROC, glBindSampler)                                \
        X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                    \
        X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                          \
        X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                            \
        X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                        \
        X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                        \
        X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)              \
        X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)        \
        X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)          \
        X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                      \
        X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                      \
        X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                \
        X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                        \
        X(PFNGLPRIMITIVERESTARTINDEXPROC, glPrimitiveRestartIndex)            \
        X(PFNGLDEPTHRANGEFPROC, glDepthRangef)                                \
        X(PFNGLCLEARDEPTHFPROC, glClearDepthf)

#define DECLARE(type, name) type name = nullptr;
        GL_FUNCTIONS(DECLARE)
#undef DECLARE
#endif

#ifdef DMC3_GLES
        const char* Header = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
#else
        const char* Header = "#version 330 core\n";
#endif

        GLuint Compile(GLenum type, const std::string& body, uint64_t key)
        {
            std::string source = Header + body;
            // DMC3_FLAT_PS=1: pinta tudo de vermelho (diagnóstico de geometria)
            if (type == GL_FRAGMENT_SHADER && getenv("DMC3_FLAT_PS"))
                source.replace(source.find("    bool pass"), 0, "    color0 = vec4(1.0, 0.0, 0.0, 1.0);" + std::string(1, 10));
            const char* text = source.c_str();
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &text, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[4096];
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                fprintf(stderr, "[gl] erro no shader %016llX: %s\n", (unsigned long long)key, log);
                if (getenv("DMC3_LOG_SHADERS"))
                    fprintf(stderr, "%s\n", source.c_str());
            }
            return s;
        }

        struct Program
        {
            GLuint id = 0;
            bool ok = false;
            GLint vc = -1, pc = -1, b = -1, alphaFunc = -1, alphaRef = -1, wideScale = -1;
        };
        std::map<std::pair<uint64_t, uint64_t>, Program> programs;

        Program& GetProgram(const DrawCall& call)
        {
            Program& p = programs[{ call.vsKey, call.psKey }];
            if (p.id)
                return p;
            p.id = glCreateProgram();
            GLuint vs = Compile(GL_VERTEX_SHADER, call.vs->body, call.vsKey);
            GLuint ps = Compile(GL_FRAGMENT_SHADER, call.ps->body, call.psKey);
            glAttachShader(p.id, vs);
            glAttachShader(p.id, ps);
            glLinkProgram(p.id);
            glDeleteShader(vs);
            glDeleteShader(ps);
            GLint ok = 0;
            glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
            p.ok = ok;
            if (!ok)
            {
                char log[4096];
                glGetProgramInfoLog(p.id, sizeof(log), nullptr, log);
                fprintf(stderr, "[gl] erro ao ligar %016llX + %016llX: %s\n", (unsigned long long)call.vsKey,
                    (unsigned long long)call.psKey, log);
                return p;
            }
            glUseProgram(p.id);
            p.vc = glGetUniformLocation(p.id, "VC");
            p.pc = glGetUniformLocation(p.id, "PC");
            p.b = glGetUniformLocation(p.id, "B");
            p.alphaFunc = glGetUniformLocation(p.id, "alphaFunc");
            p.alphaRef = glGetUniformLocation(p.id, "alphaRef");
            p.wideScale = glGetUniformLocation(p.id, "wideScale");
            for (int i = 0; i < 32; i++)
            {
                std::string name = "T" + std::to_string(i);
                GLint location = glGetUniformLocation(p.id, name.c_str());
                if (location >= 0)
                    glUniform1i(location, i);
            }
            return p;
        }

        // fator de blend da Xenos -> OpenGL
        GLenum BlendFactor(uint32_t f)
        {
            switch (f)
            {
            case 0: return GL_ZERO;
            case 1: return GL_ONE;
            case 4: return GL_SRC_COLOR;
            case 5: return GL_ONE_MINUS_SRC_COLOR;
            case 6: return GL_SRC_ALPHA;
            case 7: return GL_ONE_MINUS_SRC_ALPHA;
            case 8: return GL_DST_COLOR;
            case 9: return GL_ONE_MINUS_DST_COLOR;
            case 10: return GL_DST_ALPHA;
            case 11: return GL_ONE_MINUS_DST_ALPHA;
            case 12: return GL_CONSTANT_COLOR;
            case 13: return GL_ONE_MINUS_CONSTANT_COLOR;
            case 14: return GL_CONSTANT_ALPHA;
            case 15: return GL_ONE_MINUS_CONSTANT_ALPHA;
            case 16: return GL_SRC_ALPHA_SATURATE;
            default: return GL_ONE;
            }
        }

        GLenum BlendOp(uint32_t op)
        {
            switch (op)
            {
            case 1: return GL_FUNC_SUBTRACT;
            case 2: return GL_MIN;
            case 3: return GL_MAX;
            case 4: return GL_FUNC_REVERSE_SUBTRACT;
            default: return GL_FUNC_ADD;
            }
        }

        GLenum StencilOp(uint32_t op)
        {
            static const GLenum Ops[] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP };
            return Ops[op & 7];
        }

        GLenum CompareFunc(uint32_t f)
        {
            static const GLenum Funcs[] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
            return Funcs[f & 7];
        }

        // ---- render targets: uma textura de cor por superfície do jogo e um depth/stencil por superfície ----
        struct Surface
        {
            GLuint texture = 0;
            uint32_t width = 0, height = 0;
        };
        std::unordered_map<uint32_t, Surface> colorSurfaces;
        std::map<std::pair<uint32_t, uint64_t>, GLuint> depthSurfaces;   // (superfície, tamanho) -> renderbuffer
        std::map<std::pair<uint32_t, uint32_t>, GLuint> framebuffers;

        uint64_t currentFrame = 0;
        uint64_t forcedDebugFrame = 0;   // RequestScreenshot também detalha o quadro
        std::string requestedShot;
        bool DebugFrame()
        {
            static uint64_t frame = [] { const char* e = getenv("DMC3_DEBUG_FRAME"); return e ? strtoull(e, nullptr, 10) : 0ull; }();
            return (frame && currentFrame + 1 == frame) || (forcedDebugFrame && currentFrame + 1 == forcedDebugFrame);
        }

        GLuint MakeTexture(uint32_t width, uint32_t height)
        {
            GLuint id;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            return id;
        }

        Surface& ColorSurface(const Target& t)
        {
            Surface& s = colorSurfaces[t.color];
            if (!s.texture || s.width != t.width || s.height != t.height)
            {
                if (!s.texture)
                    glGenTextures(1, &s.texture);
                glBindTexture(GL_TEXTURE_2D, s.texture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                s.width = t.width;
                s.height = t.height;
                // o mesmo objeto mudou de tamanho: os framebuffers ligados a ele ficam inválidos
                for (auto it = framebuffers.begin(); it != framebuffers.end();)
                    it = it->first.first == t.color ? framebuffers.erase(it) : std::next(it);
            }
            return s;
        }

        GLuint Framebuffer(const Target& t)
        {
            Surface& color = ColorSurface(t);
            GLuint& fbo = framebuffers[{ t.color, t.depth }];
            if (fbo)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, fbo);
                return fbo;
            }
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.texture, 0);
            if (t.depth)
            {
                // a profundidade tem o tamanho da própria superfície, para ser a mesma com ou sem render target de cor
                uint32_t width = t.depthWidth ? t.depthWidth : t.width, height = t.depthHeight ? t.depthHeight : t.height;
                GLuint& rb = depthSurfaces[{ t.depth, (uint64_t(width) << 32) | height }];
                if (!rb)
                {
                    glGenRenderbuffers(1, &rb);
                    glBindRenderbuffer(GL_RENDERBUFFER, rb);
                    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
                }
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
            }
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
                fprintf(stderr, "[gl] framebuffer incompleto (%X) para %08X/%08X\n", status, t.color, t.depth);
            return fbo;
        }

        // ---- texturas: as resolvidas ficam na GPU; as outras são convertidas da memória do console ----
        struct Resolved
        {
            GLuint texture = 0;
            uint32_t width = 0, height = 0;
            bool depth = false;
        };
        std::unordered_map<uint32_t, Resolved> resolved;   // endereço -> textura

        struct CachedTexture
        {
            GLuint id = 0;
            uint64_t sample = 0;      // hash do conteúdo, para saber quando mudou
            uint64_t checkedFrame = ~0ull;
        };
        std::unordered_map<uint64_t, CachedTexture> textures;
        std::vector<uint32_t> scratch;

        // hash do conteúdo inteiro (o jogo reescreve texturas no lugar, como a do texto dos menus)
        uint64_t Sample(const uint8_t* p, size_t bytes)
        {
            uint64_t h = 1469598103934665603ull ^ bytes;
            const uint64_t* w = reinterpret_cast<const uint64_t*>(p);
            for (size_t i = 0; i < bytes / 8; i++)
                h = (h ^ w[i]) * 0x9E3779B97F4A7C15ull;
            return h ^ (h >> 29);
        }

        GLuint Upload(const Texture& t)
        {
            auto r = resolved.find(t.address);
            if (r != resolved.end())
                return r->second.texture;
            if (!xenos::TextureSupported(t.format) || t.width == 0 || t.height == 0)
            {
                static int warned = 0;
                if (warned++ < 16)
                    fprintf(stderr, "[gl] textura não suportada ainda: formato %u, %s, %ux%u em %08X\n", t.format,
                        t.tiled ? "tiled" : "linear", t.width, t.height, t.address);
                return whiteTexture;
            }
            uint64_t key = (uint64_t(t.address) << 32) ^ (uint64_t(t.width) << 16) ^ t.height ^ (uint64_t(t.format) << 58) ^
                (uint64_t(t.tiled) << 63) ^ (uint64_t(t.swizzle) << 40);
            const uint8_t* src = kernel::Guest<uint8_t>(t.address);
            size_t bytes = xenos::TextureBytes(t);
            CachedTexture& c = textures[key];
            // texturas grandes são conferidas uma vez por quadro; as pequenas (texto) a cada uso, porque o jogo
            // reescreve e desenha várias vezes no mesmo quadro
            if (c.id && c.checkedFrame == currentFrame && bytes > 0x40000)
                return c.id;
            c.checkedFrame = currentFrame;
            uint64_t sample = Sample(src, bytes);
            if (c.id && c.sample == sample)
                return c.id;
            xenos::DecodeTexture(t, src, scratch);
            if (!c.id)
                c.id = MakeTexture(t.width, t.height);
            glBindTexture(GL_TEXTURE_2D, c.id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scratch.data());
            // mipmaps gerados a partir do nível 0 (os níveis do console ficam no endereço de mip, ainda não lido)
            glGenerateMipmap(GL_TEXTURE_2D);
            c.sample = sample;
            return c.id;
        }

        // grava um framebuffer: "RGBA" + largura + altura (u32 LE) + linhas de baixo para cima. As imagens do console
        // (render targets) têm a linha 0 em cima, então são invertidas para o mesmo formato da janela.
        void SaveFramebuffer(const char* path, GLuint fbo, int w, int h, bool topFirst)
        {
            std::vector<uint8_t> pixels(size_t(w) * h * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            if (topFirst)
                for (int y = 0; y < h / 2; y++)
                    std::swap_ranges(pixels.begin() + size_t(y) * w * 4, pixels.begin() + size_t(y + 1) * w * 4,
                        pixels.begin() + size_t(h - 1 - y) * w * 4);
            if (FILE* f = fopen(path, "wb"))
            {
                fwrite("RGBA", 1, 4, f);
                fwrite(&w, 4, 1, f);
                fwrite(&h, 4, 1, f);
                fwrite(pixels.data(), 1, pixels.size(), f);
                fclose(f);
            }
        }

        // amostradores (sampler objects) por estado do fetch constant
        std::unordered_map<uint32_t, GLuint> samplers;

        GLenum WrapMode(uint8_t clamp)
        {
            switch (clamp)
            {
            case 0: return GL_REPEAT;
            case 1: return GL_MIRRORED_REPEAT;
            default: return GL_CLAMP_TO_EDGE;   // as variantes de clamp/espelho único da Xenos
            }
        }

        // depth: textura de profundidade resolvida (o OpenGL ES só amostra profundidade com filtro "ponto")
        GLuint Sampler(const Texture& t, bool hasMips, bool depth)
        {
            uint32_t key = t.clampX | (t.clampY << 3) | (t.magFilter << 6) | (t.minFilter << 8) | (t.mipFilter << 10) |
                (uint32_t(hasMips) << 12) | (uint32_t(depth) << 13);
            GLuint& id = samplers[key];
            if (id)
                return id;
            glGenSamplers(1, &id);
            glSamplerParameteri(id, GL_TEXTURE_WRAP_S, WrapMode(t.clampX));
            glSamplerParameteri(id, GL_TEXTURE_WRAP_T, WrapMode(t.clampY));
            glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, t.magFilter == 0 ? GL_NEAREST : GL_LINEAR);
            bool linear = t.minFilter != 0;
            GLenum min = linear ? GL_LINEAR : GL_NEAREST;
            if (hasMips && t.mipFilter == 0)
                min = linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
            else if (hasMips && t.mipFilter == 1)
                min = linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
            if (depth)
            {
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                min = GL_NEAREST;
            }
            glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, min);
            return id;
        }

        uint64_t textureDumpFrame = 0;   // quadro pedido pelo F12

        // diagnóstico (F12): registra a textura e grava a versão convertida em <DMC3_SCREENSHOT_DIR>/tex_<endereço>.rgba
        void DumpTexture(const Texture& t, uint32_t slot, uint64_t psKey)
        {
            static std::unordered_map<uint32_t, uint64_t> dumped;   // endereço -> quadro
            bool isResolved = resolved.count(t.address) != 0;
            fprintf(stderr, "[tex] PS %016llX T%u: %08X formato %u %ux%u pitch %u %s endian %u swizzle %03X clamp %u/%u filtro %u/%u/%u%s\n",
                (unsigned long long)psKey, slot, t.address, t.format, t.width, t.height, t.pitch, t.tiled ? "tiled" : "linear",
                t.endian, t.swizzle, t.clampX, t.clampY, t.magFilter, t.minFilter, t.mipFilter, isResolved ? " (resolvida)" : "");
            if (isResolved || dumped[t.address] == textureDumpFrame || !xenos::TextureSupported(t.format))
                return;
            dumped[t.address] = textureDumpFrame;
            std::vector<uint32_t> rgba;
            if (!xenos::DecodeTexture(t, kernel::Guest<uint8_t>(t.address), rgba))
                return;
            char path[512];
            snprintf(path, sizeof(path), "%s/tex_%08X_f%u_%ux%u.rgba", getenv("DMC3_SCREENSHOT_DIR") ? getenv("DMC3_SCREENSHOT_DIR") : ".",
                t.address, t.format, t.width, t.height);
            if (FILE* f = fopen(path, "wb"))
            {
                int w = int(t.width), h = int(t.height);
                fwrite("RGBA", 1, 4, f);
                fwrite(&w, 4, 1, f);
                fwrite(&h, 4, 1, f);
                for (int y = h - 1; y >= 0; y--)   // linhas de baixo para cima, como as capturas
                    fwrite(rgba.data() + size_t(y) * w, 4, w, f);
                fclose(f);
            }
        }

        void SaveScreenshot(const char* path)
        {
            int w = WindowWidth, h = WindowHeight;
            std::vector<uint8_t> pixels(size_t(w) * h * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            if (FILE* f = fopen(path, "wb"))
            {
                // formato simples: "RGBA" + largura + altura (u32 LE) + linhas de baixo para cima
                fwrite("RGBA", 1, 4, f);
                fwrite(&w, 4, 1, f);
                fwrite(&h, 4, 1, f);
                fwrite(pixels.data(), 1, pixels.size(), f);
                fclose(f);
                fprintf(stderr, "[gl] captura de tela em %s\n", path);
            }
        }

        void ApplyState(const RenderState& s, const Target& t)
        {
            const BlendState& blend = s.blend;
            if (blend.enable)
            {
                uint32_t c = blend.control;
                uint32_t a = blend.separateAlpha ? c >> 16 : c;
                glEnable(GL_BLEND);
                glBlendFuncSeparate(BlendFactor(c & 0x1F), BlendFactor((c >> 8) & 0x1F), BlendFactor(a & 0x1F), BlendFactor((a >> 8) & 0x1F));
                glBlendEquationSeparate(BlendOp((c >> 5) & 7), BlendOp((a >> 5) & 7));
                glBlendColor(blend.constant[0], blend.constant[1], blend.constant[2], blend.constant[3]);
            }
            else
            {
                glDisable(GL_BLEND);
            }
            static bool noDepth = getenv("DMC3_NO_DEPTH") != nullptr;
            if (s.depthTest && t.depth && !noDepth)
            {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(CompareFunc(s.depthFunc));
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }
            glDepthMask(s.depthWrite && t.depth ? GL_TRUE : GL_FALSE);
            glColorMask(s.colorMask & 1, (s.colorMask >> 1) & 1, (s.colorMask >> 2) & 1, (s.colorMask >> 3) & 1);
            // com o y invertido, o sentido dos triângulos na tela também inverte (vale para o culling e para o stencil
            // de frente e verso)
            glFrontFace(s.frontIsCW ? GL_CCW : GL_CW);
            if (s.stencil.enable && t.depth)
            {
                glEnable(GL_STENCIL_TEST);
                for (int side = 0; side < 2; side++)
                {
                    GLenum face = side ? GL_BACK : GL_FRONT;
                    glStencilFuncSeparate(face, CompareFunc(s.stencil.func[side]), s.stencil.ref[side], s.stencil.mask[side]);
                    glStencilOpSeparate(face, StencilOp(s.stencil.fail[side]), StencilOp(s.stencil.depthFail[side]),
                        StencilOp(s.stencil.pass[side]));
                    glStencilMaskSeparate(face, s.stencil.writeMask[side]);
                }
            }
            else
            {
                glDisable(GL_STENCIL_TEST);
            }
            if (s.cullFront || s.cullBack)
            {
                glEnable(GL_CULL_FACE);
                glCullFace(s.cullFront && s.cullBack ? GL_FRONT_AND_BACK : s.cullFront ? GL_FRONT : GL_BACK);
            }
            else
            {
                glDisable(GL_CULL_FACE);
            }
            if (s.viewport && s.viewportW > 0 && s.viewportH > 0)
            {
                glViewport(GLint(s.viewportX), GLint(s.viewportY), GLsizei(s.viewportW), GLsizei(s.viewportH));
                glDepthRangef(s.depthMin, s.depthMax);
            }
            else
            {
                glViewport(0, 0, t.width, t.height);
                glDepthRangef(0, 1);
            }
        }
    }

    void EnsureWindow()
    {
        if (window)
            return;
        // DMC3_TOUCH no PC: o mouse gera toques, para testar os controles na tela
        if (getenv("DMC3_TOUCH"))
            SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        {
            fprintf(stderr, "[gl] SDL_Init falhou: %s\n", SDL_GetError());
            std::abort();
        }
#ifdef DMC3_GLES
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        // ES 3.0 basta (shaders #version 300 es) e funciona também no emulador; a tela principal não precisa de
        // profundidade (o jogo desenha em framebuffers próprios)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        window = SDL_CreateWindow("Devil May Cry 3", 0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        // DMC3_WINDOW=<largura>x<altura>: outro tamanho de janela (por exemplo, para testar o widescreen no PC)
        if (const char* size = getenv("DMC3_WINDOW"))
            sscanf(size, "%dx%d", &WindowWidth, &WindowHeight);
        window = SDL_CreateWindow("Devil May Cry 3 (recompilado)", WindowWidth, WindowHeight, SDL_WINDOW_OPENGL);
#endif
        context = window ? SDL_GL_CreateContext(window) : nullptr;
        if (!context)
        {
            fprintf(stderr, "[gl] não foi possível criar a janela/contexto: %s\n", SDL_GetError());
            std::abort();
        }
        SDL_GL_SetSwapInterval(1);
#ifndef DMC3_GLES
#define LOAD(type, name) name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));
        GL_FUNCTIONS(LOAD)
#undef LOAD
#endif
        fprintf(stderr, "[gl] %s / %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)));

        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glGenFramebuffers(1, &copyFbo);
        glGenFramebuffers(1, &depthCopyFbo);

        uint32_t white = 0xFFFFFFFF;
        glGenTextures(1, &whiteTexture);
        glBindTexture(GL_TEXTURE_2D, whiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

#ifdef DMC3_GLES
        glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);   // índice máximo do tipo: 0xFFFFFFFF nos índices de 32 bits
#else
        glEnable(GL_PRIMITIVE_RESTART);
        glPrimitiveRestartIndex(0xFFFFFFFF);
#endif
    }

    void Clear(const Target& target, uint32_t flags, const float color[4], float z, uint32_t stencil)
    {
        EnsureWindow();
        Framebuffer(target);
        GLbitfield mask = 0;
        if (flags & 0xF)
        {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(color[0], color[1], color[2], color[3]);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if ((flags & 0x10) && target.depth)
        {
            glDepthMask(GL_TRUE);
            glClearDepthf(z);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if ((flags & 0x20) && target.depth)
        {
            glStencilMask(0xFF);
            glClearStencil(GLint(stencil));
            mask |= GL_STENCIL_BUFFER_BIT;
        }
        glDisable(GL_SCISSOR_TEST);
        if (mask)
            glClear(mask);
    }

    void Draw(const DrawCall& call)
    {
        EnsureWindow();
        if (!call.vs || !call.ps || call.vertexCount == 0)
            return;
        Program& program = GetProgram(call);
        if (!program.ok)
            return;
        Framebuffer(call.target);
        glUseProgram(program.id);
        ApplyState(call.state, call.target);

        if (program.vc >= 0)
            glUniform4fv(program.vc, 256, call.vsConstants);
        if (program.pc >= 0)
            glUniform4fv(program.pc, 256, call.psConstants);
        if (program.b >= 0)
            glUniform1uiv(program.b, 8, call.bools);
        const BlendState& blend = call.state.blend;
        glUniform1i(program.alphaFunc, blend.alphaTest ? int(blend.alphaFunc) : 7);
        glUniform1f(program.alphaRef, blend.alphaRef);
        // widescreen: só nos alvos 16:9 (a cena; mapas de sombra e reflexos têm outras proporções)
        float wide = 1.0f;
        if (ActiveMode < 0)
            ActiveMode = touch::DisplayMode();
        if (ActiveMode == 3 && WindowWidth * 9 > WindowHeight * 16 &&
            std::abs(float(call.target.width) * 9 - float(call.target.height) * 16) < float(call.target.height) * 0.2f)
            wide = 16.0f * WindowHeight / (9.0f * WindowWidth);
        glUniform1f(program.wideScale, wide);

        for (uint32_t i = 0; i < 32; i++)
            if (call.ps->textureMask >> i & 1)
            {
                glActiveTexture(GL_TEXTURE0 + i);
                GLuint id = Upload(call.textures[i]);
                glBindTexture(GL_TEXTURE_2D, id);
                // só as texturas convertidas da memória têm mipmaps (as resolvidas e a branca, não)
                auto r = resolved.find(call.textures[i].address);
                bool hasMips = id != whiteTexture && r == resolved.end();
                glBindSampler(i, Sampler(call.textures[i], hasMips, r != resolved.end() && r->second.depth));
                if (textureDumpFrame && currentFrame + 1 == textureDumpFrame)
                    DumpTexture(call.textures[i], i, call.psKey);
            }
        glActiveTexture(GL_TEXTURE0);

        if (DebugFrame())
            fprintf(stderr, "[debug] desenho: prim %u, %u vértices, %zu índices, %zu atributos, RT %08X %ux%u DS %08X, viewport %s "
                "%.0f,%.0f %.0fx%.0f, blend %d %08X cte (%.2f %.2f %.2f %.2f), z %d/%d, cull %d%d, máscara %X, a0 (%.3f %.3f %.3f %.3f)\n",
                call.primitive, call.vertexCount, call.indices.size(), call.vs->fetches.size(), call.target.color,
                call.target.width, call.target.height, call.target.depth, call.state.viewport ? "sim" : "não",
                call.state.viewportX, call.state.viewportY, call.state.viewportW, call.state.viewportH, call.state.blend.enable,
                call.state.blend.control, call.state.blend.constant[0], call.state.blend.constant[1],
                call.state.blend.constant[2], call.state.blend.constant[3], call.state.depthTest, call.state.depthWrite, call.state.cullFront, call.state.cullBack,
                call.state.colorMask, call.attributes.size() >= 4 ? call.attributes[0] : 0.f,
                call.attributes.size() >= 4 ? call.attributes[1] : 0.f, call.attributes.size() >= 4 ? call.attributes[2] : 0.f,
                call.attributes.size() >= 4 ? call.attributes[3] : 0.f);
        if (DebugFrame() && call.state.stencil.enable)
        {
            const StencilState& st = call.state.stencil;
            fprintf(stderr, "[debug]   stencil: frente f%u op %u/%u/%u ref %02X máscara %02X escrita %02X; verso f%u op %u/%u/%u ref %02X "
                "máscara %02X escrita %02X; z função %u\n", st.func[0], st.fail[0], st.depthFail[0], st.pass[0], st.ref[0], st.mask[0],
                st.writeMask[0], st.func[1], st.fail[1], st.depthFail[1], st.pass[1], st.ref[1], st.mask[1], st.writeMask[1],
                call.state.depthFunc);
        }
        // DMC3_DEBUG_VERTICES: nos quadros detalhados, os atributos dos primeiros vértices de cada desenho
        static const bool debugVertices = getenv("DMC3_DEBUG_VERTICES") != nullptr;
        if (debugVertices && DebugFrame())
        {
            size_t perVertex = call.vs->fetches.size() * 4;
            for (uint32_t v = 0; v < std::min(call.vertexCount, 4u) && perVertex; v++)
            {
                std::string line;
                char buf[64];
                for (size_t k = 0; k < perVertex; k++)   // um bloco por atributo: (atributo * vértices + vértice) * 4
                {
                    size_t index = ((k / 4) * call.vertexCount + v) * 4 + k % 4;
                    if (index >= call.attributes.size())
                        break;
                    snprintf(buf, sizeof(buf), "%s%.3f", k % 4 ? " " : k ? " | " : "", call.attributes[index]);
                    line += buf;
                }
                fprintf(stderr, "[debug]   vértice %u: %s\n", v, line.c_str());
            }
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, call.attributes.size() * sizeof(float), call.attributes.data(), GL_STREAM_DRAW);
        uint32_t count = uint32_t(call.vs->fetches.size());
        for (uint32_t i = 0; i < 16; i++)
        {
            if (i < count)
            {
                glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 16, reinterpret_cast<void*>(size_t(i) * call.vertexCount * 16));
                glEnableVertexAttribArray(i);
            }
            else
            {
                glDisableVertexAttribArray(i);
            }
        }

        // quads viram pares de triângulos; o resto vai direto
        GLenum mode;
        switch (call.primitive)
        {
        case 1: mode = GL_POINTS; break;
        case 2: mode = GL_LINES; break;
        case 3: mode = GL_LINE_STRIP; break;
        case 5: mode = GL_TRIANGLE_FAN; break;
        case 6: mode = GL_TRIANGLE_STRIP; break;
        default: mode = GL_TRIANGLES; break;
        }
        std::vector<uint32_t> indices;
        const std::vector<uint32_t>* source = &call.indices;
        if (call.primitive == 13)
        {
            uint32_t n = call.indices.empty() ? call.vertexCount : uint32_t(call.indices.size());
            auto at = [&](uint32_t i) { return call.indices.empty() ? i : call.indices[i]; };
            for (uint32_t i = 0; i + 3 < n; i += 4)
                indices.insert(indices.end(), { at(i), at(i + 1), at(i + 2), at(i), at(i + 2), at(i + 3) });
            source = &indices;
        }
        else if (call.primitive != 1 && call.primitive != 2 && call.primitive != 3 && call.primitive != 4 &&
            call.primitive != 5 && call.primitive != 6)
        {
            static int warned = 0;
            if (warned++ < 8)
                fprintf(stderr, "[gl] tipo de primitiva %u ainda não suportado\n", call.primitive);
            return;
        }
        if (source->empty())
        {
            glDrawArrays(mode, 0, call.vertexCount);
        }
        else
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, source->size() * 4, source->data(), GL_STREAM_DRAW);
            glDrawElements(mode, GLsizei(source->size()), GL_UNSIGNED_INT, nullptr);
        }
    }

    void Resolve(const Target& target, uint32_t destAddress, uint32_t destWidth, uint32_t destHeight, bool depth)
    {
        EnsureWindow();
        if (depth)
        {
            // profundidade: textura depth24/stencil8 que os shaders amostram como .x = profundidade (0..1)
            if (!target.depth)
                return;
            Resolved& r = resolved[destAddress];
            if (!r.texture || r.width != destWidth || r.height != destHeight || !r.depth)
            {
                if (!r.texture)
                    glGenTextures(1, &r.texture);
                glBindTexture(GL_TEXTURE_2D, r.texture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, destWidth, destHeight, 0, GL_DEPTH_STENCIL,
                    GL_UNSIGNED_INT_24_8, nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                r.width = destWidth;
                r.height = destHeight;
                r.depth = true;
            }
            GLuint source = Framebuffer(target);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthCopyFbo);
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, r.texture, 0);
            glDisable(GL_SCISSOR_TEST);
            glDepthMask(GL_TRUE);
            glStencilMask(0xFF);
            uint32_t w = std::min(destWidth, target.width), h = std::min(destHeight, target.height);
            glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, source);
            return;
        }
        if (DebugFrame())
            fprintf(stderr, "[debug] resolve de %08X (%ux%u) para %08X (%ux%u)\n", target.color, target.width, target.height,
                destAddress, destWidth, destHeight);
        Resolved& r = resolved[destAddress];
        if (!r.texture || r.width != destWidth || r.height != destHeight || r.depth)
        {
            if (!r.texture)
                r.texture = MakeTexture(destWidth, destHeight);
            r.depth = false;
            glBindTexture(GL_TEXTURE_2D, r.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, destWidth, destHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            r.width = destWidth;
            r.height = destHeight;
        }
        GLuint source = Framebuffer(target);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copyFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.texture, 0);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        uint32_t w = std::min(destWidth, target.width), h = std::min(destHeight, target.height);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (DebugFrame() && getenv("DMC3_DUMP_RESOLVES"))
        {
            // diagnóstico: cada resolve do quadro detalhado vira um arquivo
            static uint32_t sequence = 0;
            char path[512];
            snprintf(path, sizeof(path), "%s/resolve_%03u_%08X.rgba", getenv("DMC3_SCREENSHOT_DIR") ? getenv("DMC3_SCREENSHOT_DIR") : ".",
                sequence++, destAddress);
            SaveFramebuffer(path, source, int(w), int(h), true);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, source);
    }

    // aviso da troca de estilo: o nome do estilo novo aparece acima da barra de vida, na cor do estilo, e o emblema à
    // esquerda da barra brilha nessa cor; some depois de ~2 s. Posições em frações da imagem do jogo (a HUD de 1280x720),
    // convertidas pelo retângulo onde a imagem foi desenhada (OutX/OutY/OutW/OutH), então acompanha os modos de tela
    void DrawStyleBanner()
    {
        uint32_t styleIndex;
        uint64_t when;
        if (!style::LastChange(styleIndex, when) || !style::DanteActive())
            return;
        float t = float(double(SDL_GetTicksNS() - when) / 1e9);
        constexpr float FadeIn = 0.12f, Hold = 1.4f, FadeOut = 0.5f;
        if (t < 0 || t > Hold + FadeOut)
            return;
        float alpha = t < FadeIn ? t / FadeIn : t < Hold ? 1.0f : 1.0f - (t - Hold) / FadeOut;
        static const float Colors[6][3] = {
            { 0.90f, 0.12f, 0.10f },   // Swordmaster: vermelho
            { 0.25f, 0.50f, 1.00f },   // Gunslinger: azul
            { 1.00f, 0.80f, 0.15f },   // Trickster: amarelo
            { 0.25f, 0.85f, 0.25f },   // Royalguard: verde
            { 0.70f, 0.30f, 1.00f },   // Quicksilver: roxo
            { 1.00f, 0.40f, 0.15f },   // Doppelganger: laranja
        };
        const float* color = Colors[styleIndex];

        static GLuint program = 0, textures[6] = {};
        static GLint rect = -1, tint = -1, mode = -1, sampler = -1;
        if (!program)
        {
            // um quadrilátero por desenho: rect = (x0, y0, x1, y1) em coordenadas de recorte; mode 0 texto (alfa da
            // textura), 1 brilho radial
            GLuint vs = Compile(GL_VERTEX_SHADER,
                "uniform vec4 rect;\nout vec2 uv;\n"
                "void main()\n{\n    vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                "    uv = vec2(c.x, 1.0 - c.y);\n    gl_Position = vec4(mix(rect.xy, rect.zw, c), 0.0, 1.0);\n}\n", 3);
            GLuint fs = Compile(GL_FRAGMENT_SHADER,
                "uniform vec4 tint;\nuniform int mode;\nuniform sampler2D T;\nin vec2 uv;\nlayout(location = 0) out vec4 o;\n"
                "void main()\n{\n    float a = mode == 0 ? texture(T, uv).r : pow(max(0.0, 1.0 - length(uv * 2.0 - 1.0)), 2.0);\n"
                "    o = vec4(tint.rgb, tint.a * a);\n}\n", 4);
            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            rect = glGetUniformLocation(program, "rect");
            tint = glGetUniformLocation(program, "tint");
            mode = glGetUniformLocation(program, "mode");
            sampler = glGetUniformLocation(program, "T");
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            for (int i = 0; i < 6; i++)
            {
                const stylenames::Image& image = stylenames::Images[i];
                glGenTextures(1, &textures[i]);
                glBindTexture(GL_TEXTURE_2D, textures[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, image.width, image.height, 0, GL_RED, GL_UNSIGNED_BYTE, image.alpha);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, WindowWidth, WindowHeight);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glUseProgram(program);
        glUniform1i(sampler, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindSampler(0, 0);
        glBindVertexArray(vao);
        for (GLuint i = 0; i < 16; i++)
            glDisableVertexAttribArray(i);
        // fração da imagem do jogo (x para a direita, y para baixo) -> coordenadas de recorte da janela
        auto quad = [&](float fx0, float fy0, float fx1, float fy1) {
            auto cx = [&](float f) { return (OutX + f * OutW) / float(WindowWidth) * 2.0f - 1.0f; };
            auto cy = [&](float f) { return (OutY + (1.0f - f) * OutH) / float(WindowHeight) * 2.0f - 1.0f; };
            glUniform4f(rect, cx(fx0), cy(fy1), cx(fx1), cy(fy0));
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        };
        // brilho do emblema (soma de luz): um clarão que se abre na troca e um halo que fica enquanto o nome aparece
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
        glUniform1i(mode, 1);
        constexpr float OrbX = 0.116f, OrbY = 0.105f;
        float imageAspect = float(OutW) / float(OutH);
        auto glow = [&](float radius, float a) {
            glUniform4f(tint, color[0], color[1], color[2], a);
            quad(OrbX - radius / imageAspect, OrbY - radius, OrbX + radius / imageAspect, OrbY + radius);
        };
        float burst = std::min(t / 0.35f, 1.0f);
        glow(0.05f + 0.07f * burst, (1.0f - burst) * 0.9f);
        glow(0.075f, 0.55f * alpha);
        // nome: sombra escura e o texto na cor do estilo, entrando da esquerda
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glUniform1i(mode, 0);
        const stylenames::Image& image = stylenames::Images[styleIndex];
        glBindTexture(GL_TEXTURE_2D, textures[styleIndex]);
        float height = 0.066f, width = height * float(image.width) / float(image.height) / imageAspect;
        float slide = (1.0f - std::min(t / 0.2f, 1.0f)) * 0.02f;
        float x = 0.168f - slide, y = 0.018f;
        glUniform4f(tint, 0.0f, 0.0f, 0.0f, 0.85f * alpha);
        quad(x + 0.002f, y + 0.004f, x + 0.002f + width, y + 0.004f + height);
        glUniform4f(tint, color[0], color[1], color[2], alpha);
        quad(x, y, x + width, y + height);
    }

    // controles na tela (touch_controls.cpp): triângulos em pixels da janela, com transparência
    void DrawTouchOverlay()
    {
        static std::vector<float> vertices;
        touch::BuildOverlay(WindowWidth, WindowHeight, vertices);
        if (vertices.empty())
            return;
        static GLuint program = 0;
        static GLint size = -1;
        if (!program)
        {
            GLuint vs = Compile(GL_VERTEX_SHADER,
                "layout(location = 0) in vec2 p;\nlayout(location = 1) in vec4 c;\nout vec4 color;\nuniform vec2 size;\n"
                "void main()\n{\n    color = c;\n    gl_Position = vec4(p.x / size.x * 2.0 - 1.0, 1.0 - p.y / size.y * 2.0, 0.0, 1.0);\n}\n", 1);
            GLuint fs = Compile(GL_FRAGMENT_SHADER,
                "in vec4 color;\nlayout(location = 0) out vec4 o;\nvoid main()\n{\n    o = color;\n}\n", 2);
            program = glCreateProgram();
            glAttachShader(program, vs);
            glAttachShader(program, fs);
            glLinkProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            size = glGetUniformLocation(program, "size");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, WindowWidth, WindowHeight);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glUseProgram(program);
        glUniform2f(size, float(WindowWidth), float(WindowHeight));
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STREAM_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 24, nullptr);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(8));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        for (GLuint i = 2; i < 16; i++)
            glDisableVertexAttribArray(i);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size() / 6));
    }

    void RequestScreenshot(const char* name)
    {
        requestedShot = name;
        forcedDebugFrame = currentFrame + 1;
        if (getenv("DMC3_DUMP_TEXTURES"))   // as capturas pedidas pelo código também registram as texturas
            textureDumpFrame = currentFrame + 1;
    }

    bool DetailedFrame()
    {
        return DebugFrame();
    }

    void Present(uint64_t frame, uint32_t frontBuffer)
    {
        EnsureWindow();
        // a imagem do console tem a linha 0 em cima; a janela do GL, embaixo
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_SCISSOR_TEST);
        // área 16:9 centralizada no tamanho real da janela (no celular, a tela inteira com faixas pretas)
        SDL_GetWindowSizeInPixels(window, &WindowWidth, &WindowHeight);
        // modo de tela (touch_controls): 0 preenche a janela inteira (estica), 1 16:9 com faixas, 2 zoom (16:9 na largura
        // toda, cortando em cima e embaixo)
        int mode = touch::DisplayMode();
        ActiveMode = mode;
        OutW = WindowWidth;
        OutH = WindowWidth * 9 / 16;
        if (mode == 0 || mode == 3)   // 3 = widescreen: a cena já sai na proporção da janela (ver Draw)
            OutH = WindowHeight;
        else if (mode == 1 && OutH > WindowHeight)
        {
            OutH = WindowHeight;
            OutW = WindowHeight * 16 / 9;
        }
        OutX = (WindowWidth - OutW) / 2;
        OutY = (WindowHeight - OutH) / 2;
        glViewport(0, 0, WindowWidth, WindowHeight);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        auto r = resolved.find(frontBuffer);
        if (DebugFrame())
            fprintf(stderr, "[debug] apresentação: front buffer %08X %s\n", frontBuffer, r != resolved.end() ? "resolvido" : "desconhecido");
        currentFrame = frame;
        // DMC3_SHOW_RT=<endereço da superfície>: mostra um render target direto (diagnóstico)
        if (const char* show = getenv("DMC3_SHOW_RT"))
        {
            auto c = colorSurfaces.find(uint32_t(strtoul(show, nullptr, 16)));
            if (c != colorSurfaces.end())
            {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, copyFbo);
                glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->second.texture, 0);
                glBlitFramebuffer(0, 0, c->second.width, c->second.height, OutX, OutY + OutH, OutX + OutW, OutY, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                r = resolved.end();
                goto shown;
            }
        }
        if (r != resolved.end())
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, copyFbo);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->second.texture, 0);
            glBlitFramebuffer(0, 0, r->second.width, r->second.height, OutX, OutY + OutH, OutX + OutW, OutY, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        else
        {
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    shown:
        // vídeo nativo (video_player.cpp) por cima do jogo, com faixas pretas se a proporção não for 16:9
        {
            static GLuint videoTexture = 0;
            static uint32_t videoWidth = 0, videoHeight = 0;
            static std::vector<uint8_t> videoPixels;
            uint32_t w = 0, h = 0;
            bool fresh = false;
            double aspect = 16.0 / 9;
            if (videoplayer::Frame(videoPixels, w, h, aspect, fresh))
            {
                if (fresh)
                {
                    if (!videoTexture)
                        videoTexture = MakeTexture(w, h);
                    glBindTexture(GL_TEXTURE_2D, videoTexture);
                    if (w != videoWidth || h != videoHeight)
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, videoPixels.data());
                    else
                        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, videoPixels.data());
                    videoWidth = w;
                    videoHeight = h;
                }
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glClearColor(0, 0, 0, 1);
                glClear(GL_COLOR_BUFFER_BIT);
                if (videoWidth && videoHeight)
                {
                    int dw = OutW, dh = int(OutW / aspect);
                    if (dh > OutH)
                    {
                        dh = OutH;
                        dw = int(OutH * aspect);
                    }
                    int x = OutX + (OutW - dw) / 2, y = OutY + (OutH - dh) / 2;
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, copyFbo);
                    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, videoTexture, 0);
                    glBlitFramebuffer(0, 0, videoWidth, videoHeight, x, y + dh, x + dw, y, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                }
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // DMC3_DETECT_BLACK=1: quando, com o Dante na missão, mais da metade do quadro sai preto exato, o próximo é detalhado e capturado
        // (diagnóstico da tela preta perto da câmera). Até 6 vezes, com intervalo de 5 segundos.
        if (getenv("DMC3_DETECT_BLACK") && style::DanteActive())
        {
            static uint32_t triggers = 0;
            static uint64_t lastTrigger = 0;
            static std::vector<uint8_t> row(size_t(WindowWidth) * 4);
            uint32_t dark = 0, total = 0;
            for (int y = WindowHeight / 10; y < WindowHeight; y += WindowHeight / 10)
            {
                glReadPixels(0, y, WindowWidth, 1, GL_RGBA, GL_UNSIGNED_BYTE, row.data());
                for (int x = 0; x < WindowWidth; x += 8, total++)
                    dark += (row[x * 4] | row[x * 4 + 1] | row[x * 4 + 2]) == 0;   // preto exato (cor do Clear)
            }
            if (dark * 100 > total * 50 && triggers < 6 && frame > lastTrigger + 300)
            {
                triggers++;
                lastTrigger = frame;
                char name[64];
                snprintf(name, sizeof(name), "preto_%llu", (unsigned long long)frame + 1);
                fprintf(stderr, "[gl] quadro %llu quase todo preto (%u%%): detalhando o próximo\n", (unsigned long long)frame,
                    dark * 100 / total);
                RequestScreenshot(name);
            }
        }

        DrawStyleBanner();
        DrawTouchOverlay();
        // capturas pedidas pelo código (RequestScreenshot)
        if (!requestedShot.empty())
        {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.rgba", getenv("DMC3_SCREENSHOT_DIR") ? getenv("DMC3_SCREENSHOT_DIR") : ".",
                requestedShot.c_str());
            SaveScreenshot(path);
            requestedShot.clear();
        }
        // DMC3_SCREENSHOT=<quadro>[,<quadro>...]: grava <DMC3_SCREENSHOT_DIR ou .>/shot_<quadro>.rgba
        if (const char* list = getenv("DMC3_SCREENSHOT"))
        {
            for (const char* p = list; *p;)
            {
                char* end;
                unsigned long long f = strtoull(p, &end, 10);
                if (f == frame)
                {
                    char path[512];
                    snprintf(path, sizeof(path), "%s/shot_%llu.rgba", getenv("DMC3_SCREENSHOT_DIR") ? getenv("DMC3_SCREENSHOT_DIR") : ".", f);
                    SaveScreenshot(path);
                }
                p = *end ? end + 1 : end;
            }
        }
        SDL_GL_SwapWindow(window);
        // o jogo conta o tempo em quadros de 60 Hz: com vsync num monitor mais rápido, segura o ritmo aqui
        static uint64_t deadline = 0;
        constexpr uint64_t FrameNs = 1000000000ull / 60;
        uint64_t now = SDL_GetTicksNS();
        deadline = deadline && now < deadline + FrameNs ? deadline + FrameNs : now;
        if (now < deadline)
            SDL_DelayPrecise(deadline - now);
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT)
            {
                fprintf(stderr, "[gl] janela fechada\n");
                fflush(stderr);
                std::exit(0);
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && (e.key.scancode == SDL_SCANCODE_F12 || e.key.scancode == SDL_SCANCODE_P || e.key.scancode == SDL_SCANCODE_PRINTSCREEN))
            {
                char name[64];
                snprintf(name, sizeof(name), "f12_%llu", (unsigned long long)frame + 1);
                fprintf(stderr, "[gl] F12: capturando o quadro %llu\n", (unsigned long long)frame + 1);
                RequestScreenshot(name);
                textureDumpFrame = frame + 1;
            }
            input::HandleEvent(e);
        }
        input::Update(frame);
    }
}
