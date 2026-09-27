cmake_minimum_required(VERSION 3.16)
project(p04_priority_inversion C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(priority_inversion SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p04_priority_inversion_demo priority_inversion)
