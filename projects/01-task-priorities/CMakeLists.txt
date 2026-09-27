cmake_minimum_required(VERSION 3.16)
project(p01_task_priorities C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(task_priorities SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p01_task_priorities_demo task_priorities)
