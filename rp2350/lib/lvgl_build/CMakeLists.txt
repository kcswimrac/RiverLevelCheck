# Builds LVGL v8.1.0 (git submodule at lib/lvgl) as the static library "LVGL",
# the same way Waveshare's RP2350 examples do. lv_conf.h comes from the project root.
set(LVGL_DIR ${CMAKE_CURRENT_LIST_DIR}/../lvgl)
file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)
add_library(LVGL ${LVGL_SOURCES})
target_include_directories(LVGL PUBLIC ${LVGL_DIR})
