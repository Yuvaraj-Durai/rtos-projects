cmake_minimum_required(VERSION 3.16)
project(p07_task_notifications C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(task_notifications SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p07_task_notifications_demo task_notifications)
