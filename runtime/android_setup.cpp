// Preparação no Android (ver android_setup.h).
#include "android_setup.h"

#ifdef __ANDROID__
#include "xex_convert.h"

#include <SDL3/SDL.h>
#include <jni.h>

#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // métodos estáticos de DMC3Activity pelo JNI
    jclass ActivityClass(JNIEnv*& env)
    {
        env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
        if (!env || !activity)
            return nullptr;
        jclass type = env->GetObjectClass(activity);
        env->DeleteLocalRef(activity);
        return type;
    }

    bool CallBool(const char* name)
    {
        JNIEnv* env;
        jclass type = ActivityClass(env);
        if (!type)
            return false;
        jmethodID method = env->GetStaticMethodID(type, name, "()Z");
        bool result = method && env->CallStaticBooleanMethod(type, method);
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            result = false;
        }
        env->DeleteLocalRef(type);
        return result;
    }

    void CallVoid(const char* name)
    {
        JNIEnv* env;
        jclass type = ActivityClass(env);
        if (!type)
            return;
        if (jmethodID method = env->GetStaticMethodID(type, name, "()V"))
            env->CallStaticVoidMethod(type, method);
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(type);
    }

    std::string CallString(const char* name)
    {
        JNIEnv* env;
        jclass type = ActivityClass(env);
        std::string result;
        if (!type)
            return result;
        jmethodID method = env->GetStaticMethodID(type, name, "()Ljava/lang/String;");
        jstring value = method ? static_cast<jstring>(env->CallStaticObjectMethod(type, method)) : nullptr;
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (value)
        {
            const char* chars = env->GetStringUTFChars(value, nullptr);
            result = chars;
            env->ReleaseStringUTFChars(value, chars);
            env->DeleteLocalRef(value);
        }
        env->DeleteLocalRef(type);
        return result;
    }

    // caixa de mensagem com até 3 botões; devolve o índice do botão escolhido (ou -1)
    int Ask(const std::string& message, std::initializer_list<const char*> buttons)
    {
        std::vector<SDL_MessageBoxButtonData> data;
        int id = 0;
        for (const char* text : buttons)
            data.push_back({ id == 0 ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0u, id++, text });
        SDL_MessageBoxData box{ SDL_MESSAGEBOX_INFORMATION, nullptr, "Devil May Cry 3", message.c_str(), int(data.size()),
            data.data(), nullptr };
        int chosen = -1;
        if (!SDL_ShowMessageBox(&box, &chosen))
            return -1;
        return chosen;
    }

    // move src para dst (renomeando ou, se não der, copiando e apagando); não sobrescreve o que já existe em dst
    void Move(const fs::path& src, const fs::path& dst)
    {
        std::error_code ec;
        if (!fs::exists(src, ec))
            return;
        if (fs::is_directory(src, ec))
        {
            fs::create_directories(dst, ec);
            for (const auto& entry : fs::directory_iterator(src, ec))
                Move(entry.path(), dst / entry.path().filename());
            fs::remove(src, ec);   // só sai se ficou vazia
            return;
        }
        if (fs::exists(dst, ec))
            return;
        fs::rename(src, dst, ec);
        if (ec)
        {
            ec.clear();
            if (fs::copy_file(src, dst, ec) && !ec)
                fs::remove(src, ec);
        }
    }

    // traz os arquivos da pasta antiga do app (versões anteriores) para a pasta do jogo
    void Migrate(const fs::path& root)
    {
        const char* external = SDL_GetAndroidExternalStoragePath();
        if (!external)
            return;
        fs::path old = external;
        std::error_code ec;
        if (fs::exists(old / "x360", ec) || fs::exists(old / "saves", ec))
        {
            fprintf(stderr, "[setup] movendo os arquivos de %s para %s\n", old.c_str(), root.c_str());
            Move(old / "x360", root);
            Move(old / "saves", root / "saves");
        }
    }

    std::vector<std::string> Missing(const fs::path& root)
    {
        std::vector<std::string> missing;
        std::error_code ec;
        if (!fs::exists(root / "DMC3.pe", ec) && !fs::exists(root / "DMC3.xex", ec))
            missing.push_back("DMC3.xex");
        for (const char* file : { "Media/DMC3-0.zip", "Media/DMC3-1.zip", "Media/DMC3-2.zip" })
            if (!fs::exists(root / file, ec))
                missing.push_back(file);
        return missing;
    }
}

namespace androidsetup
{
    bool Prepare(std::string& rootOut)
    {
        // 1. permissão
        while (!CallBool("hasAllFilesAccess"))
        {
            int answer = Ask("O DMC3 guarda os arquivos do jogo e os saves na pasta DMC3 da memória interna, que não é "
                             "apagada se o app for desinstalado.\n\nPara isso, permita o \"acesso a todos os arquivos\" "
                             "na tela que vai abrir e depois volte para o jogo.",
                { "Abrir configurações", "Sair" });
            if (answer != 0)
                return false;
            CallVoid("requestAllFilesAccess");
            if (Ask("Depois de permitir o acesso, toque em Continuar.", { "Continuar", "Sair" }) != 0)
                return false;
        }

        fs::path root = CallString("getGameFolder");
        if (root.empty())
            root = "/storage/emulated/0/DMC3";
        std::error_code ec;
        fs::create_directories(root / "saves", ec);
        rootOut = root.string();
        Migrate(root);

        // 2. arquivos do jogo
        for (;;)
        {
            std::vector<std::string> missing = Missing(root);
            if (missing.empty())
            {
                fs::path pe = root / "DMC3.pe";
                if (fs::exists(pe, ec) && xex::IsExpectedImage(pe.string()))
                    return true;
                // gera o DMC3.pe a partir do DMC3.xex (uma vez só)
                xex::Result r = xex::Convert((root / "DMC3.xex").string(), pe.string());
                fprintf(stderr, "[setup] conversão do DMC3.xex: %d\n", int(r));
                if (r == xex::Result::Ok)
                    return true;
                const char* why = r == xex::Result::WrongVersion
                    ? "O DMC3.xex é de outra versão do jogo. Este port funciona com o DMC3.xex da Devil May Cry HD "
                      "Collection do Xbox 360."
                    : r == xex::Result::WriteFailed ? "Não foi possível gravar o DMC3.pe na pasta DMC3 (memória cheia?)."
                                                     : "O DMC3.xex não pôde ser lido (arquivo incompleto ou de outro jogo?).";
                if (Ask(why, { "Verificar de novo", "Sair" }) != 0)
                    return false;
                fs::remove(pe, ec);
                continue;
            }
            std::string message = "Arquivos do jogo não encontrados.\n\nCopie da sua cópia do Devil May Cry 3 (Devil May "
                                  "Cry HD Collection, Xbox 360) para a pasta:\n  Memória interna/DMC3/\n\nFaltam:\n";
            for (const std::string& m : missing)
                message += "  " + m + "\n";
#ifndef DMC3_NO_FFMPEG
            message += "\nOpcional, para as cutscenes: a pasta Media/wmv/ (vídeos).";
#endif
            if (Ask(message, { "Verificar de novo", "Sair" }) != 0)
                return false;
        }
    }
}
#endif
