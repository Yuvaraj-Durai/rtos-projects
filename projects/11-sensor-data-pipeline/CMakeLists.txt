cmake_minimum_required(VERSION 3.16)
project(p11_sensor_pipeline C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(sensor_pipeline SOURCES src/main.c src/filter.c src/signal_gen.c
                      INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p11_sensor_pipeline_demo sensor_pipeline)

rtos_add_unit_test(p11_test_filter SOURCES tests/test_filter.c src/filter.c src/signal_gen.c
                   INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
