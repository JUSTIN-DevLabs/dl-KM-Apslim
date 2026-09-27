cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(2026-09-16-000)

target_sources(app PRIVATE src/main.c)
