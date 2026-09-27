cmake_minimum_required(VERSION 3.16)
project(p10_watchdog_health_monitor C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(watchdog_monitor SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p10_watchdog_monitor_demo watchdog_monitor)
