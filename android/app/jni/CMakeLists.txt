# Android: SDL3 (das fontes em tools/SDL3-3.4.18) e o jogo recompilado + runtime como libmain.so.
cmake_minimum_required(VERSION 3.22)
project(DMC3Android C CXX)

set(DMC3_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(CMAKE_CXX_STANDARD 20)

# SDL3 compartilhada (libSDL3.so), como o SDLActivity espera
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(${DMC3_ROOT}/tools/SDL3-3.4.18 SDL)

add_subdirectory(src)
