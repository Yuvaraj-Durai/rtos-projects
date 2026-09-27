cmake_minimum_required(VERSION 3.16)
project(p05_event_groups C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(event_groups_demo SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p05_event_groups_demo event_groups_demo)
