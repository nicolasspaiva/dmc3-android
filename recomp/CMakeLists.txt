# Código do DMC3 (Xbox 360) recompilado pelo XenonRecomp.
# DMC3Ppc: o código gerado em ppc/. DMC3: o executável, com o runtime de ../runtime
# (memória, carga da imagem e as funções do kernel do Xbox 360).
# Depois de implementar uma função do kernel, rode scripts/x360_imports.py para
# regerar runtime/imports_gen.cpp.
cmake_minimum_required(VERSION 3.20)
project(DMC3Recomp C CXX)

set(CMAKE_CXX_STANDARD 20)
set(XENONRECOMP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tools/XenonRecomp)

file(GLOB DMC3_PPC_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_recomp.*.cpp)

add_library(DMC3Ppc OBJECT ${DMC3_PPC_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/ppc/ppc_func_mapping.cpp)
target_include_directories(DMC3Ppc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ppc ${XENONRECOMP_DIR}/thirdparty/simde)
target_compile_options(DMC3Ppc PRIVATE -mavx2 -Wno-unused-label -Wno-unused-variable)

file(GLOB DMC3_RUNTIME_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/*.cpp)
add_executable(DMC3 ${DMC3_RUNTIME_SOURCES} $<TARGET_OBJECTS:DMC3Ppc>
    ${CMAKE_CURRENT_SOURCE_DIR}/../tools/XenonRecomp/thirdparty/tiny-AES-c/aes.c)
set(SDL3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tools/SDL3)
target_include_directories(DMC3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ppc ${XENONRECOMP_DIR}/thirdparty/simde ${SDL3_DIR}/include)
target_link_libraries(DMC3 PRIVATE ${SDL3_DIR}/lib/x64/SDL3.lib opengl32)
add_custom_command(TARGET DMC3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${SDL3_DIR}/lib/x64/SDL3.dll ${CMAKE_CURRENT_SOURCE_DIR}/bin/SDL3.dll)
# FFmpeg 8.1 (build LGPL compartilhado do BtbN) para os vídeos WMV (VC-1 + WMA Pro)
set(FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tools/ffmpeg)
target_include_directories(DMC3 PRIVATE ${FFMPEG_DIR}/include)
target_link_libraries(DMC3 PRIVATE ${FFMPEG_DIR}/lib/avformat.lib ${FFMPEG_DIR}/lib/avcodec.lib ${FFMPEG_DIR}/lib/avutil.lib
    ${FFMPEG_DIR}/lib/swscale.lib ${FFMPEG_DIR}/lib/swresample.lib)
foreach(dll avformat-62 avcodec-62 avutil-60 swscale-9 swresample-6)
    add_custom_command(TARGET DMC3 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FFMPEG_DIR}/bin/${dll}.dll ${CMAKE_CURRENT_SOURCE_DIR}/bin/${dll}.dll)
endforeach()
target_compile_options(DMC3 PRIVATE -mavx2)
set_target_properties(DMC3 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
# map do linker: o watchdog imprime o offset de cada thread no executável e
# scripts/host_where.py traduz para a função
target_link_options(DMC3 PRIVATE /MAP:${CMAKE_CURRENT_SOURCE_DIR}/bin/DMC3.map)
