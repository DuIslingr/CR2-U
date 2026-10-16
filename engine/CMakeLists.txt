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
    src/script/vm.cpp
)
target_include_directories(cr2core PUBLIC src)
target_link_libraries(cr2core PUBLIC squirrel2)

add_executable(cr2tool tools/cr2tool.cpp)
target_link_libraries(cr2tool PRIVATE cr2core)
