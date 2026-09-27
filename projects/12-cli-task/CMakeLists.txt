cmake_minimum_required(VERSION 3.16)
project(p12_cli_task C)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/rtos_common.cmake)

rtos_add_freertos_app(cli_task SOURCES src/main.c src/cli.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
rtos_add_demo_test(p12_cli_task_demo cli_task
  INPUT ${CMAKE_CURRENT_LIST_DIR}/tests/session.txt
  # Patterns must appear in the same order as the commands in session.txt.
  # "> led off" comes from the line "lex<DEL>d off", so it proves DEL editing.
  PASS_REGEX "LED is ON.*sum = 60.*unknown command 'reboot'.*gain = 7.*unknown command 'lef'.*> led off.LED is OFF.*bye.*RESULT: PASS")

rtos_add_unit_test(p12_test_cli SOURCES tests/test_cli.c src/cli.c INCLUDES ${CMAKE_CURRENT_LIST_DIR}/src)
