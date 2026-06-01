cmake_minimum_required(VERSION 3.22.1)
project(loopera_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    oboe
    GIT_REPOSITORY https://github.com/google/oboe.git
    GIT_TAG        1.8.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(oboe)

file(GLOB LAME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/lame/libmp3lame/*.c"
)
add_library(mp3lame STATIC ${LAME_SOURCES})
target_include_directories(mp3lame PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/lame/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/lame/libmp3lame"
)
target_compile_options(mp3lame PRIVATE
    -Wno-unused-result
    -Wno-implicit-function-declaration
    -Wno-unused-variable
)

add_library(loopera_engine SHARED
    LooperEngine.cpp
    LooperBridge.cpp
    LameEncoder.cpp
)

target_include_directories(loopera_engine PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/lame/include"
)

target_link_libraries(loopera_engine
    oboe
    mp3lame
    android
    log
    OpenSLES
)

target_compile_options(loopera_engine PRIVATE
    -O2
    -ffast-math
    -Wall
    -Wno-unused-parameter
)
