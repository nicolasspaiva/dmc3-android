// Conversão do DMC3.xex (o executável do disco do Xbox 360) na imagem PE que o runtime carrega (DMC3.pe), feita pelo
// próprio app na primeira execução: o mesmo que scripts/xex_extract.py faz no PC (descriptografa com AES-128 e desfaz a
// compressão "basic"). O resultado é conferido contra o PE para o qual o código foi recompilado.
#pragma once

#include <string>

namespace xex
{
    enum class Result
    {
        Ok,
        Unreadable,        // não abriu ou não é um XEX2
        Unsupported,       // compressão LZX ou chave desconhecida
        WrongVersion,      // virou um PE, mas não é a versão do DMC3 que foi recompilada
        WriteFailed,
    };

    Result Convert(const std::string& xexPath, const std::string& pePath);

    // confere se um PE já existente é o esperado
    bool IsExpectedImage(const std::string& pePath);
}
