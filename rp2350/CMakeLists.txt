# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD pico2 CACHE STRING "Board type" FORCE)
set(PICO_PLATFORM rp2350 CACHE STRING "Platform" FORCE)

include(pico_sdk_import.cmake)

project(river-forecast C CXX ASM)

set(CMAKE_BUILD_TYPE Release)

pico_sdk_init()

# --- Waveshare BSP (reused from the QE-Mini project) ---
add_subdirectory(lib/QSPI_PIO)
add_subdirectory(lib/Config)
add_subdirectory(lib/AMOLED)
add_subdirectory(lib/Touch)
add_subdirectory(lib/lvgl_build)   # LVGL v8.1.0 submodule in lib/lvgl

target_include_directories(LVGL PUBLIC ${CMAKE_SOURCE_DIR})   # lv_conf.h

# Forecast window/peak logic, Pacific time, protocol and self-tests are shared
# with the T4-S3 Wi-Fi build (one copy, in the Arduino sketch's src/core).
set(CORE_DIR ${CMAKE_SOURCE_DIR}/../river_forecast_t4s3/src/core)
file(GLOB CORE_SOURCES ${CORE_DIR}/*.c)

file(GLOB APP_SOURCES ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/fonts/*.c)

add_executable(${PROJECT_NAME} main.c ${APP_SOURCES} ${CORE_SOURCES})

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/lib/QSPI_PIO/qspi.pio)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CORE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/Config
    ${CMAKE_SOURCE_DIR}/lib/QSPI_PIO
    ${CMAKE_SOURCE_DIR}/lib/AMOLED
    ${CMAKE_SOURCE_DIR}/lib/Touch
    ${CMAKE_SOURCE_DIR}/lib/lvgl
)

target_compile_options(${PROJECT_NAME} PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(${PROJECT_NAME}
    Config
    QSPI_PIO
    AMOLED
    Touch
    LVGL
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_i2c
    hardware_gpio
)

pico_set_program_name(${PROJECT_NAME} "river-forecast")
pico_set_program_version(${PROJECT_NAME} "0.1.0")

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
