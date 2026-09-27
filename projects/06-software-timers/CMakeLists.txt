cmake_minimum_required(VERSION 3.16)
project(p06_software_timers C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(software_timers SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p06_software_timers_demo software_timers)
