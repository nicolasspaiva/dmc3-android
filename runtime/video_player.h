// Player nativo dos vídeos do jogo (ASF com VC-1 + WMA Pro) pelo FFmpeg. Uma thread decodifica; o áudio vai para
// um SDL_AudioStream e os quadros RGBA ficam numa fila que a apresentação (renderer_gl.cpp) consome pelo relógio
// do vídeo. Um vídeo de cada vez.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace videoplayer
{
    // começa a tocar o arquivo do host; loop recomeça no fim. false se o arquivo não abre
    bool Open(const std::string& hostPath, bool loop);
    void Stop();
    bool Playing();   // ainda tocando (ou com quadros/áudio por apresentar)

    // para a apresentação: true se há vídeo ativo. Quando chega a vez de um quadro novo, ele vai para rgba
    // (largura x altura x 4, linha 0 em cima) e fresh fica true; aspect = largura / altura na tela
    bool Frame(std::vector<uint8_t>& rgba, uint32_t& width, uint32_t& height, double& aspect, bool& fresh);
}
