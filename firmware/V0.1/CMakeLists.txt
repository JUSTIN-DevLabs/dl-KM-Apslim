cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(apslim_v0_1 LANGUAGES C)

target_include_directories(app PRIVATE
    src/hal src/drivers src/algorithms src/services src/app
)
target_sources(app PRIVATE
    src/main.c
    src/hal/app_timer.c
    src/hal/pwr_hal.c
    src/hal/led_hal.c
    src/hal/battery_adc.c
    src/hal/i2c_hal.c
    src/hal/usb_serial.c
    src/drivers/fdc1004.c
    src/drivers/lis2dh12.c
    src/algorithms/moving_average.c
    src/algorithms/capacitive_detect.c
    src/algorithms/accel_detect.c
    src/algorithms/meal_pattern.c
    src/services/button_service.c
    src/services/power_service.c
    src/services/battery_service.c
    src/services/capacitive_service.c
    src/services/accel_service.c
    src/app/device_control.c
)
