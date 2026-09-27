cmake_minimum_required(VERSION 3.16)
project(p02_producer_consumer_queue C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(producer_consumer SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p02_producer_consumer_demo producer_consumer)
