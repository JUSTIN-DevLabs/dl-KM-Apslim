cmake_minimum_required(VERSION 3.20.0)

set(DTC_OVERLAY_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/app.overlay"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(DK_Board)

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE
    src/main.c
    src/accel_app.c
    src/lis2dh.c
)