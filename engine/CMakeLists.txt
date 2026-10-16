cmake_minimum_required(VERSION 3.21)
project(CR2U LANGUAGES C CXX)

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
target_include_directories(squirrel2 PRIVATE ${THIRD_PARTY_DIR}/squirrel2/squirrel)
target_compile_options(squirrel2 PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w -fno-strict-aliasing>
    $<IF:$<CXX_COMPILER_ID:MSVC>,/FI${CMAKE_CURRENT_SOURCE_DIR}/src/script/squirrel_prefix.h,-include ${CMAKE_CURRENT_SOURCE_DIR}/src/script/squirrel_prefix.h>)

# Core library: everything that is not an app entry point, so tools can reuse it
add_library(cr2core STATIC
    src/assets/mvgl.cpp
    src/assets/image.cpp
    src/assets/geom.cpp
    src/assets/skeleton.cpp
    src/gfx/material.cpp
    src/script/vm.cpp
)
target_include_directories(cr2core PUBLIC src)
target_link_libraries(cr2core PUBLIC squirrel2)

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
find_program(GLSLC glslc HINTS ${Vulkan_GLSLC_EXECUTABLE})
set(CR_VULKAN OFF)
if(Vulkan_FOUND AND GLSLC)
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
    # NVIDIA DLSS (third_party/dlss: NGX headers + static library, Linux x86-64; not vendored)
    set(DLSS_DIR ${THIRD_PARTY_DIR}/dlss)
    set(CR_DLSS OFF)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND
       EXISTS ${DLSS_DIR}/lib/libnvsdk_ngx.a)
        set(CR_DLSS ON)
    endif()
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

set(VIEWER_SOURCES src/viewer/main.cpp src/gfx/gl.cpp src/gfx/gl_device.cpp src/gfx/vk_device.cpp)
if(CR_VULKAN)
    list(APPEND VIEWER_SOURCES src/gfx/vk_fsr3.cpp src/gfx/vk_dlss.cpp)
endif()
add_executable(cr2view ${VIEWER_SOURCES})
target_link_libraries(cr2view PRIVATE cr2core SDL3::SDL3)
add_dependencies(cr2view cr_glsl)
target_include_directories(cr2view PRIVATE ${GLSL_OUT})
if(CR_VULKAN)
    add_dependencies(cr2view cr_shaders)
    target_include_directories(cr2view PRIVATE ${SHADER_OUT})
    target_link_libraries(cr2view PRIVATE Vulkan::Vulkan)
    target_compile_definitions(cr2view PRIVATE CR_HAVE_VULKAN=1)
    if(CR_DLSS)
        target_include_directories(cr2view SYSTEM PRIVATE ${DLSS_DIR}/include)
        target_link_libraries(cr2view PRIVATE ${DLSS_DIR}/lib/libnvsdk_ngx.a ${CMAKE_DL_LIBS})
        target_compile_definitions(cr2view PRIVATE CR_HAVE_DLSS=1)
    endif()
endif()
