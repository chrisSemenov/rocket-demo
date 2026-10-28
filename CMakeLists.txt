# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rocket_demo)

target_sources(app PRIVATE
    src/main.cpp
    src/shared_data.cpp
    src/sensor_task.cpp
    src/telemetry_task.cpp
    src/diag_task.cpp
)
