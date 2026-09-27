cmake_minimum_required(VERSION 3.16)
project(p03_semaphores C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(semaphores SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p03_semaphores_demo semaphores)
