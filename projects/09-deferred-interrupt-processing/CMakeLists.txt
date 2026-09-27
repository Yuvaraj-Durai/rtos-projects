cmake_minimum_required(VERSION 3.16)
project(p09_deferred_interrupt C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(deferred_interrupt SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p09_deferred_interrupt_demo deferred_interrupt)
