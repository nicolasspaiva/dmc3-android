// Preparação no Android antes de o jogo começar (runtime/android_setup.cpp):
//   1. pede o "acesso a todos os arquivos", para usar a pasta Memória interna/DMC3;
//   2. move para lá o que estiver na pasta antiga do app (Android/data/org.dmc3port/files: x360/ e saves/);
//   3. confere os arquivos do jogo e, se só houver o DMC3.xex, gera o DMC3.pe (xex_convert);
//   4. se faltar algo, mostra o que falta e onde colocar, com "Verificar de novo".
#pragma once

#include <string>

namespace androidsetup
{
    // devolve false se o usuário escolheu sair; root recebe a pasta do jogo
    bool Prepare(std::string& root);
}
