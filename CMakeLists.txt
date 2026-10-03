cmake_minimum_required(VERSION 3.20)
project(SyncWave VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Windows system libraries for Core Audio / WASAPI / COM
set(SYNCWAVE_SYSTEM_LIBS
    ole32
    oleaut32
    propsys
    ksuser
    avrt
    mfplat
)

# Core engine library
add_library(syncwave_core STATIC
    src/windows/DeviceManager.h
    src/windows/DeviceManager.cpp
    src/windows/DeviceNotification.h
    src/windows/DeviceNotification.cpp
    src/windows/WasapiOutput.h
    src/windows/WasapiOutput.cpp
    src/windows/ComHelper.h
    src/audio/AudioFormat.h
    src/audio/ToneGenerator.h
    src/audio/ToneGenerator.cpp
    src/audio/AudioEngine.h
    src/audio/AudioEngine.cpp
)

target_include_directories(syncwave_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(syncwave_core PUBLIC
    ${SYNCWAVE_SYSTEM_LIBS}
)

# Main CLI Executable
add_executable(syncwave
    src/app/main.cpp
    src/app/CommandInterface.h
    src/app/CommandInterface.cpp
)

target_include_directories(syncwave PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(syncwave PRIVATE
    syncwave_core
)

# Test runner
enable_testing()

add_executable(syncwave_tests
    tests/test_main.cpp
    src/app/CommandInterface.h
    src/app/CommandInterface.cpp
)

target_include_directories(syncwave_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(syncwave_tests PRIVATE
    syncwave_core
)

add_test(NAME Phase1Tests COMMAND syncwave_tests)
