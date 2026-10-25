cmake_minimum_required(VERSION 3.21)
project(CR2U LANGUAGES C CXX)
if(WIN32)
    enable_language(RC) # (the .exe's icon: cmake/app-icon.cmake)
endif()
if(APPLE)
    enable_language(OBJCXX) # (MetalFX's upscaler: gfx/vk_metalfx.mm)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Squirrel 2.2.4 (unmodified; zlib licence): the script machine the game embeds. Built with 32-bit
# SQInteger (no _SQ64) on every host - the compiled scripts store their counts and integers in 4
# bytes, and Squirrel 2's loader reads them as SQInteger
file(GLOB SQUIRREL_SOURCES ${THIRD_PARTY_DIR}/squirrel2/squirrel/*.cpp ${THIRD_PARTY_DIR}/squirrel2/sqstdlib/*.cpp)
add_library(squirrel2 STATIC ${SQUIRREL_SOURCES})
target_include_directories(squirrel2 PUBLIC ${THIRD_PARTY_DIR}/squirrel2/include)
if(MINGW)
    target_include_directories(squirrel2 BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/script/mingw) # (64-bit SQInteger)
endif()
target_include_directories(squirrel2 PRIVATE ${THIRD_PARTY_DIR}/squirrel2/squirrel)
target_compile_options(squirrel2 PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w -fno-strict-aliasing>
    $<IF:$<CXX_COMPILER_ID:MSVC>,/FI${CMAKE_CURRENT_SOURCE_DIR}/src/script/squirrel_prefix.h,-include ${CMAKE_CURRENT_SOURCE_DIR}/src/script/squirrel_prefix.h>)

# Core library: everything that is not an app entry point, so tools can reuse it
add_library(cr2core STATIC
    src/assets/mvgl.cpp
    src/assets/image.cpp
    src/assets/geom.cpp
    src/assets/smooth.cpp
    src/assets/skeleton.cpp
    src/assets/maptable.cpp
    src/assets/release.cpp
    src/assets/hd_texture.cpp
    src/assets/gltf_figure.cpp
    src/assets/text.cpp
    src/field/fieldmap.cpp
    src/field/collision.cpp
    src/field/navigation.cpp
    src/field/character.cpp
    src/field/gimmicks.cpp
    src/field/fieldscript.cpp
    src/event/scene.cpp
    src/event/runtime.cpp
    src/gfx/material.cpp
    src/gfx/text.cpp
    src/script/vm.cpp
    src/script/gamestate.cpp
    src/script/gamesave.cpp
    src/battle/battledata.cpp
    src/battle/rules.cpp
    src/battle/motion.cpp
    src/battle/camera.cpp
    src/battle/script.cpp
    src/battle/command.cpp
    src/battle/special.cpp
    src/battle/glass.cpp
    src/field/treasure.cpp
    src/audio/sounddata.cpp
    src/audio/streams.cpp
    third_party/stb/stb_vorbis.c
    third_party/gltf_libs.c
)
# The iPad's music (AAC in CAF): FAAD2 - the system's when pkg-config finds it, else the copy in
# third_party/faad2 (FAAD2 2.11.1's libfaad, CR1:U's). CR2_BUNDLED_FAAD2=ON forces the bundled one.
option(CR2_BUNDLED_FAAD2 "Build the bundled FAAD2 even if the system has one" OFF)
if(NOT CR2_BUNDLED_FAAD2)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FAAD2 QUIET IMPORTED_TARGET faad2)
    endif()
endif()
if(FAAD2_FOUND)
    target_link_libraries(cr2core PUBLIC PkgConfig::FAAD2)
    target_compile_definitions(cr2core PUBLIC CR2_HAVE_FAAD2=1)
    message(STATUS "iPad music: FAAD2 ${FAAD2_VERSION} (system)")
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/faad2/libfaad/decoder.c)
    file(GLOB CR2_FAAD_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/faad2/libfaad/*.c)
    add_library(cr2_faad2 STATIC ${CR2_FAAD_SOURCES})
    target_include_directories(cr2_faad2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/faad2/include
                               PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/faad2/libfaad)
    # (FAAD2's own CMakeLists' fixed configuration: floating point, DRC, no DRM)
    target_compile_definitions(cr2_faad2 PRIVATE APPLY_DRC HAVE_INTTYPES_H=1 HAVE_MEMCPY=1 HAVE_STRING_H=1
                               STDC_HEADERS=1 PACKAGE_VERSION="2.11.1")
    if(NOT MSVC)
        target_compile_definitions(cr2_faad2 PRIVATE HAVE_STRINGS_H=1 HAVE_SYS_STAT_H=1 HAVE_SYS_TYPES_H=1)
    endif()
    include(CheckSymbolExists)
    if(NOT MSVC)
        set(CMAKE_REQUIRED_LIBRARIES m)
    endif()
    check_symbol_exists(lrintf "math.h" CR2_HAVE_LRINTF)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(CR2_HAVE_LRINTF)
        target_compile_definitions(cr2_faad2 PRIVATE HAVE_LRINTF=1)
    endif()
    if(MSVC)
        target_compile_options(cr2_faad2 PRIVATE /w)
    else()
        target_compile_options(cr2_faad2 PRIVATE -w)
        find_library(CR2_MATH_LIBRARY m)
        if(CR2_MATH_LIBRARY)
            target_link_libraries(cr2_faad2 PUBLIC ${CR2_MATH_LIBRARY})
        endif()
    endif()
    set_target_properties(cr2_faad2 PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(cr2core PUBLIC cr2_faad2)
    target_compile_definitions(cr2core PUBLIC CR2_HAVE_FAAD2=1)
    message(STATUS "iPad music: FAAD2 2.11.1 (bundled)")
else()
    message(STATUS "iPad music: no FAAD2, not played")
endif()
# The Vita's music (ATRAC9 in SXD): LibAtrac9's C decoder (CR1:U's copy), built here
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libatrac9/src/libatrac9.c)
    file(GLOB CR2_ATRAC9_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libatrac9/src/*.c)
    add_library(cr2_atrac9 STATIC ${CR2_ATRAC9_SOURCES})
    target_include_directories(cr2_atrac9 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libatrac9/src)
    # (its header marks the functions dllimport under MSVC unless COMPILING_DLL: a static library here)
    target_compile_definitions(cr2_atrac9 PUBLIC COMPILING_DLL)
    if(MSVC)
        target_compile_options(cr2_atrac9 PRIVATE /w)
    else()
        target_compile_options(cr2_atrac9 PRIVATE -w)
        find_library(CR2_ATRAC9_MATH m)
        if(CR2_ATRAC9_MATH)
            target_link_libraries(cr2_atrac9 PUBLIC ${CR2_ATRAC9_MATH})
        endif()
    endif()
    set_target_properties(cr2_atrac9 PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(cr2core PUBLIC cr2_atrac9)
    target_compile_definitions(cr2core PUBLIC CR2_HAVE_ATRAC9=1)
    message(STATUS "Vita music: LibAtrac9 (bundled)")
endif()
# stb_vorbis (public domain, vendored - CR1:U's copy): the Android release's music (Ogg Vorbis)
set_source_files_properties(third_party/stb/stb_vorbis.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
# cgltf 1.13 (MIT), stb_image 2.30 and stb_image_write 1.16 (public domain / MIT), vendored: the
# model bridge's glTF reading, its pictures' decoding and the exports' PNGs (assets/gltf_figure.cpp)
set_source_files_properties(third_party/gltf_libs.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
target_include_directories(cr2core SYSTEM PRIVATE third_party)
target_include_directories(cr2core PUBLIC src)
target_include_directories(cr2core PRIVATE third_party/hevag) # (the HE-VAG filter table: vgmstream's, ISC)
target_link_libraries(cr2core PUBLIC squirrel2)
# The Android release's APK (its library: the maps' table; its fonts): zlib (the system's) inflates
# them; without it, only an unpacked APK's
# zlib and the like: the system's, else (Windows, macOS, Android - CR_FETCH_DEPS) built here from
# their sources, static
option(CR_FETCH_DEPS "Build zlib (and FreeType) from source where the system has none" ON)
find_package(ZLIB QUIET)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    set(CR_FETCH_EXCLUDE EXCLUDE_FROM_ALL)
endif()
if(NOT ZLIB_FOUND AND CR_FETCH_DEPS)
    include(FetchContent)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # (zlib's asks for CMake 2.4, which CMake 4 refuses)
    FetchContent_Declare(zlib GIT_REPOSITORY https://github.com/madler/zlib.git GIT_TAG v1.3.1 GIT_SHALLOW TRUE ${CR_FETCH_EXCLUDE})
    FetchContent_MakeAvailable(zlib)
    set_target_properties(zlibstatic PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
    set(ZLIB_FOUND TRUE)
    set(ZLIB_LIBRARY zlibstatic) # (the find module's own names too: FreeType's build reads them)
    set(ZLIB_LIBRARIES zlibstatic)
    set(ZLIB_INCLUDE_DIR "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
    set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}")
    set(ZLIB_VERSION_STRING "1.3.1 (built here)")
endif()
if(ZLIB_FOUND)
    target_link_libraries(cr2core PUBLIC ZLIB::ZLIB)
    target_compile_definitions(cr2core PUBLIC CR2_HAVE_ZLIB=1) # (public: the viewer's setup unpacks an .ipa)
    message(STATUS "APK: zlib ${ZLIB_VERSION_STRING}")
else()
    message(STATUS "APK: zlib not found - an unpacked APK only")
endif()
# The game's text: FreeType (the system's) draws the release's fonts (gfx/text.cpp); without it,
# no text
find_package(Freetype QUIET)
if(NOT FREETYPE_FOUND AND CR_FETCH_DEPS)
    include(FetchContent)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype GIT_REPOSITORY https://github.com/freetype/freetype.git GIT_TAG VER-2-13-3 GIT_SHALLOW TRUE ${CR_FETCH_EXCLUDE})
    set(SKIP_INSTALL_ALL ON)
    set(cr_zlib_found ${ZLIB_FOUND})
    set(ZLIB_FOUND FALSE) # (FreeType links zlib whenever it is found, FT_DISABLE_ZLIB or not)
    FetchContent_MakeAvailable(freetype)
    set(ZLIB_FOUND ${cr_zlib_found})
    add_library(Freetype::Freetype ALIAS freetype)
    set(FREETYPE_FOUND TRUE)
    set(FREETYPE_VERSION_STRING "2.13.3 (built here)")
endif()
if(FREETYPE_FOUND)
    target_link_libraries(cr2core PUBLIC Freetype::Freetype)
    target_compile_definitions(cr2core PRIVATE CR2_HAVE_FREETYPE=1)
    message(STATUS "Text: FreeType ${FREETYPE_VERSION_STRING}")
else()
    message(STATUS "Text: FreeType not found - no text")
endif()

add_executable(cr2tool tools/cr2tool.cpp)
target_link_libraries(cr2tool PRIVATE cr2core)

# The viewer: SDL3 (the system's when present, else built from source as CR1:U does) and OpenGL 3.3
find_package(SDL3 CONFIG QUIET)
if(NOT SDL3_FOUND)
    include(FetchContent)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.0
        GIT_SHALLOW TRUE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL3)
endif()
# Vulkan back end: the loader + headers and glslc (shaders are compiled to SPIR-V at build time
# and embedded). Without them the game builds with OpenGL only.
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND AND MINGW AND CMAKE_CROSSCOMPILING)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/mingw-vulkan.cmake) # (llvm-mingw: no Vulkan SDK)
endif()
find_program(GLSLC glslc HINTS ${Vulkan_GLSLC_EXECUTABLE})
set(CR_VULKAN OFF)
if(Vulkan_FOUND AND GLSLC AND NOT ANDROID) # (Android: GLES 3)
    set(CR_VULKAN ON)
    set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx/shaders)
    set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${SHADER_OUT})
    set(SHADER_INCS)
    foreach(s quad.vert quad.frag mesh.vert mesh.frag)
        add_custom_command(OUTPUT ${SHADER_OUT}/${s}.inc
            COMMAND ${GLSLC} -O -mfmt=c -I ${SHADER_DIR} ${SHADER_DIR}/${s} -o ${SHADER_OUT}/${s}.inc
            DEPENDS ${SHADER_DIR}/${s} ${SHADER_DIR}/common.glsl ${SHADER_DIR}/shading.glsl ${SHADER_DIR}/shade_flags.h
            COMMENT "glslc ${s}")
        list(APPEND SHADER_INCS ${SHADER_OUT}/${s}.inc)
    endforeach()
    # Scene variants (temporal upscaling): + motion vectors and a reactive mask
    foreach(s quad.vert quad.frag mesh.vert mesh.frag)
        add_custom_command(OUTPUT ${SHADER_OUT}/scene_${s}.inc
            COMMAND ${GLSLC} -O -mfmt=c -DSCENE=1 -I ${SHADER_DIR} ${SHADER_DIR}/${s} -o ${SHADER_OUT}/scene_${s}.inc
            DEPENDS ${SHADER_DIR}/${s} ${SHADER_DIR}/common.glsl ${SHADER_DIR}/shading.glsl ${SHADER_DIR}/shade_flags.h
            COMMENT "glslc ${s} (scene)")
        list(APPEND SHADER_INCS ${SHADER_OUT}/scene_${s}.inc)
    endforeach()
    # AMD FidelityFX FSR 3.1 upscaler passes (third_party/fidelityfx, MIT), as AMD's Vulkan
    # back end builds them: 32-bit maths, Lanczos approximation, render-size motion vectors
    set(FFX_DIR ${THIRD_PARTY_DIR}/fidelityfx)
    file(GLOB FFX_HEADERS ${FFX_DIR}/gpu/*.h ${FFX_DIR}/gpu/*/*.h)
    set(FFX_DEFS -DFFX_GPU=1 -DFFX_GLSL=1 -DFFX_HALF=0
        -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_SAMPLERS_USE_DATA_HALF=0
        -DFFX_FSR3UPSCALER_OPTION_ACCUMULATE_SAMPLERS_USE_DATA_HALF=0
        -DFFX_FSR3UPSCALER_OPTION_REPROJECT_SAMPLERS_USE_DATA_HALF=0
        -DFFX_FSR3UPSCALER_OPTION_POSTPROCESSLOCKSTATUS_SAMPLERS_USE_DATA_HALF=0
        -DFFX_FSR3UPSCALER_OPTION_UPSAMPLE_USE_LANCZOS_TYPE=2
        -DFFX_FSR3UPSCALER_OPTION_REPROJECT_USE_LANCZOS_TYPE=1
        -DFFX_FSR3UPSCALER_OPTION_HDR_COLOR_INPUT=0
        -DFFX_FSR3UPSCALER_OPTION_LOW_RESOLUTION_MOTION_VECTORS=1
        -DFFX_FSR3UPSCALER_OPTION_JITTERED_MOTION_VECTORS=0
        -DFFX_FSR3UPSCALER_OPTION_INVERTED_DEPTH=0)
    foreach(p prepare_inputs luma_pyramid shading_change_pyramid shading_change prepare_reactivity
              luma_instability accumulate accumulate_sharpen rcas)
        set(src ${p})
        set(sharpen 0)
        if(p STREQUAL "accumulate_sharpen")
            set(src accumulate)
            set(sharpen 1)
        endif()
        add_custom_command(OUTPUT ${SHADER_OUT}/fsr3_${p}.inc
            COMMAND ${GLSLC} -fshader-stage=comp --target-env=vulkan1.2 -O -mfmt=c ${FFX_DEFS}
                    -DFFX_FSR3UPSCALER_OPTION_APPLY_SHARPENING=${sharpen}
                    -I ${FFX_DIR}/gpu -I ${FFX_DIR}/gpu/fsr3upscaler
                    ${FFX_DIR}/passes/ffx_fsr3upscaler_${src}_pass.glsl -o ${SHADER_OUT}/fsr3_${p}.inc
            DEPENDS ${FFX_DIR}/passes/ffx_fsr3upscaler_${src}_pass.glsl ${FFX_HEADERS}
            COMMENT "glslc FSR 3.1 ${p}")
        list(APPEND SHADER_INCS ${SHADER_OUT}/fsr3_${p}.inc)
    endforeach()
    add_custom_target(cr_shaders DEPENDS ${SHADER_INCS})
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dlss.cmake) # (NVIDIA DLSS: CR_DLSS, cr_link_dlss)
    if(NOT MSVC)
        set_source_files_properties(src/gfx/vk_device.cpp src/gfx/vk_fsr3.cpp src/gfx/vk_dlss.cpp
                                    PROPERTIES COMPILE_OPTIONS -Wno-missing-field-initializers)
    endif()
    message(STATUS "Renderer: Vulkan ${Vulkan_VERSION} (the default) and OpenGL")
else()
    message(STATUS "Renderer: OpenGL only (Vulkan loader or glslc not found)")
endif()

# The shaders' shared GLSL as C++ strings, for the GL device (Vulkan's are compiled by glslc)
set(GLSL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx/shaders)
set(GLSL_OUT ${CMAKE_CURRENT_BINARY_DIR}/glsl)
file(MAKE_DIRECTORY ${GLSL_OUT})
set(GLSL_INCS)
foreach(f shade_flags.h shading.glsl)
    add_custom_command(OUTPUT ${GLSL_OUT}/${f}.inc
        COMMAND ${CMAKE_COMMAND} -DIN=${GLSL_DIR}/${f} -DOUT=${GLSL_OUT}/${f}.inc -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS ${GLSL_DIR}/${f} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
        COMMENT "embed ${f}")
    list(APPEND GLSL_INCS ${GLSL_OUT}/${f}.inc)
endforeach()
add_custom_target(cr_glsl DEPENDS ${GLSL_INCS})

set(VIEWER_SOURCES src/viewer/main.cpp src/viewer/audio.cpp src/viewer/setup.cpp src/gfx/gl.cpp src/gfx/gl_device.cpp src/gfx/vk_device.cpp)
if(CR_VULKAN)
    list(APPEND VIEWER_SOURCES src/gfx/vk_fsr3.cpp src/gfx/vk_dlss.cpp)
    if(APPLE) # (MetalFX's upscaler: Objective-C++)
        list(APPEND VIEWER_SOURCES src/gfx/vk_metalfx.mm)
        set_source_files_properties(src/gfx/vk_metalfx.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/apple-vulkan.cmake)
    endif()
endif()
if(ANDROID)
    # SDL's Android Java glue loads libmain.so (android/: the Gradle project)
    add_library(cr2view SHARED ${VIEWER_SOURCES})
    set_target_properties(cr2view PROPERTIES OUTPUT_NAME main)
    target_link_libraries(cr2view PRIVATE log android)
else()
    add_executable(cr2view WIN32 MACOSX_BUNDLE ${VIEWER_SOURCES})
endif()
# The GL device on OpenGL ES 3.0: Android's; elsewhere (Mesa's GLES) a test of that path
option(CR2_GLES "OpenGL ES 3.0 for the GL device" ${ANDROID})
if(CR2_GLES)
    find_library(CR2_GLES_LIBRARY NAMES GLESv3 GLESv2 REQUIRED)
    target_link_libraries(cr2view PRIVATE ${CR2_GLES_LIBRARY})
    target_compile_definitions(cr2view PRIVATE CR_GLES=1)
    message(STATUS "GL device: OpenGL ES 3.0 (${CR2_GLES_LIBRARY})")
endif()
target_link_libraries(cr2view PRIVATE cr2core SDL3::SDL3)
# The first start's setup (viewer/setup.cpp), on a desktop: a Vita NoNpDrm dump's files decrypted
# (third_party/psvpfsparser - Vita3K's, GPL-2.0) and a dump's archive - .rar, .7z, .zip, .vpk -
# unpacked (libarchive: the system's, else built here - CR_FETCH_DEPS; without it, a folder only)
if(NOT ANDROID AND NOT IOS)
    file(GLOB CR_PSVPFS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/psvpfsparser/*.cpp)
    add_library(psvpfs STATIC ${CR_PSVPFS_SOURCES})
    target_include_directories(psvpfs SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/psvpfsparser)
    target_compile_features(psvpfs PUBLIC cxx_std_17)
    if(NOT MSVC)
        target_compile_options(psvpfs PRIVATE -w)
    endif()
    target_link_libraries(cr2view PRIVATE psvpfs)
    target_compile_definitions(cr2view PRIVATE CR_HAVE_PSVPFS=1)
    find_package(LibArchive QUIET)
    if(NOT LibArchive_FOUND AND CR_FETCH_DEPS)
        include(FetchContent)
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        foreach(off ENABLE_OPENSSL ENABLE_MBEDTLS ENABLE_NETTLE ENABLE_LIBB2 ENABLE_LZ4 ENABLE_LZO ENABLE_ZSTD ENABLE_BZip2 ENABLE_LIBXML2
                    ENABLE_EXPAT ENABLE_PCREPOSIX ENABLE_PCRE2POSIX ENABLE_LIBGCC ENABLE_CNG ENABLE_TAR ENABLE_CPIO ENABLE_CAT ENABLE_UNZIP
                    ENABLE_XATTR ENABLE_ACL ENABLE_ICONV ENABLE_TEST ENABLE_INSTALL ENABLE_WERROR)
            set(${off} OFF CACHE BOOL "" FORCE)
        endforeach()
        set(POSIX_REGEX_LIB NONE CACHE STRING "" FORCE) # (no regular expressions: only its readers are used)
        FetchContent_Declare(libarchive GIT_REPOSITORY https://github.com/libarchive/libarchive.git GIT_TAG v3.7.7 GIT_SHALLOW TRUE ${CR_FETCH_EXCLUDE})
        FetchContent_MakeAvailable(libarchive)
        target_include_directories(archive_static INTERFACE ${libarchive_SOURCE_DIR}/libarchive)
        target_compile_definitions(archive_static INTERFACE LIBARCHIVE_STATIC)
        add_library(LibArchive::LibArchive ALIAS archive_static)
        set(LibArchive_FOUND TRUE)
        set(LibArchive_VERSION "3.7.7 (built here)")
    endif()
    if(LibArchive_FOUND)
        target_link_libraries(cr2view PRIVATE LibArchive::LibArchive)
        target_compile_definitions(cr2view PRIVATE CR_HAVE_LIBARCHIVE=1)
        message(STATUS "Setup: Vita dumps decrypted; archives through libarchive ${LibArchive_VERSION}")
    else()
        message(STATUS "Setup: Vita dumps decrypted; no libarchive - a dump's archive unpacked first")
    endif()
endif()
set_target_properties(cr2view PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Chaos Rings II"
    MACOSX_BUNDLE_GUI_IDENTIFIER "local.preservation.chaosrings2")
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/app-icon.cmake)
cr_app_icon(cr2view chaosrings2)
add_dependencies(cr2view cr_glsl)
target_include_directories(cr2view PRIVATE ${GLSL_OUT})
if(CR_VULKAN)
    add_dependencies(cr2view cr_shaders)
    target_include_directories(cr2view PRIVATE ${SHADER_OUT})
    target_link_libraries(cr2view PRIVATE Vulkan::Vulkan)
    if(APPLE)
        cr_apple_vulkan(cr2view)
    endif()
    target_compile_definitions(cr2view PRIVATE CR_HAVE_VULKAN=1)
    if(CR_DLSS)
        cr_link_dlss(cr2view)
    endif()
endif()
