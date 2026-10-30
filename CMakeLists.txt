# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rocket_demo)

# Test-only: scripted LED requests, so the arbitration can be observed without
# flight logic. Never enabled for a release build.
option(APP_LED_SELFTEST "Run the LED request self-test sequence" OFF)

target_sources(app PRIVATE
    src/main.cpp
    src/shared_data.cpp
    src/altitude.cpp
    src/led_logic.cpp
    src/apogee.cpp
    src/heat.cpp
    src/sensor_task.cpp
    src/altitude_task.cpp
    src/apogee_task.cpp
    src/heat_task.cpp
    src/led_manager.cpp
    src/telemetry_task.cpp
    src/diag_task.cpp
)

if(APP_LED_SELFTEST)
    target_sources(app PRIVATE src/led_selftest.cpp)
    target_compile_definitions(app PRIVATE APP_LED_SELFTEST=1)
endif()
