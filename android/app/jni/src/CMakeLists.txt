# libmain.so: o código do DMC3 recompilado pelo XenonRecomp (recomp/ppc) e o runtime (runtime/).
# Os vídeos usam o FFmpeg mínimo compilado por tools/build_ffmpeg_android.sh (bibliotecas compartilhadas, LGPL).
set(XENONRECOMP_DIR ${DMC3_ROOT}/tools/XenonRecomp)

file(GLOB DMC3_PPC_SOURCES ${DMC3_ROOT}/recomp/ppc/ppc_recomp.*.cpp)
file(GLOB DMC3_RUNTIME_SOURCES ${DMC3_ROOT}/runtime/*.cpp)

add_library(main SHARED ${DMC3_PPC_SOURCES} ${DMC3_ROOT}/recomp/ppc/ppc_func_mapping.cpp ${DMC3_RUNTIME_SOURCES}
    ${XENONRECOMP_DIR}/thirdparty/tiny-AES-c/aes.c)
target_include_directories(main PRIVATE ${DMC3_ROOT}/recomp/ppc ${DMC3_ROOT}/runtime ${XENONRECOMP_DIR}/thirdparty/simde)
target_compile_options(main PRIVATE -O2 -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable
    -Wno-unused-function -Wno-switch)
# DMC3_VIDEO=OFF (versão leve): sem FFmpeg; as cutscenes em vídeo são puladas
option(DMC3_VIDEO "player de vídeo (FFmpeg) para as cutscenes" ON)
if(DMC3_VIDEO)
    set(FFMPEG_DIR ${DMC3_ROOT}/tools/ffmpeg-android/${ANDROID_ABI})
    target_include_directories(main PRIVATE ${FFMPEG_DIR}/include)
    foreach(lib avutil swresample swscale avcodec avformat)
        add_library(ffmpeg_${lib} SHARED IMPORTED)
        set_target_properties(ffmpeg_${lib} PROPERTIES IMPORTED_LOCATION ${FFMPEG_DIR}/lib/lib${lib}.so)
        target_link_libraries(main PRIVATE ffmpeg_${lib})
    endforeach()
else()
    target_compile_definitions(main PRIVATE DMC3_NO_FFMPEG)
endif()
target_link_libraries(main PRIVATE SDL3::SDL3 GLESv3 EGL android log)
