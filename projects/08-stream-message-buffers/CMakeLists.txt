cmake_minimum_required(VERSION 3.16)
project(p08_stream_message_buffers C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(stream_message_buffers SOURCES src/main.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p08_stream_message_buffers_demo stream_message_buffers)
