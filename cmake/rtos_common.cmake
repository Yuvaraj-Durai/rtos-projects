# Shared build logic for every project in this repository.
#
# Each project's CMakeLists.txt includes this file, so a project can be built
# either on its own (cmake -S projects/NN-foo -B build) or as part of the
# top-level build (cmake -S . -B build).
include_guard(GLOBAL)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # POSIX / GNU extensions (ucontext, clock_gettime, ...)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

enable_testing()
find_package(Threads REQUIRED)

set(RTOS_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")
option(RTOS_WERROR "Treat warnings in our own code as errors" ON)

# Warning flags applied to *our* code only (never to the FreeRTOS kernel).
function(rtos_own_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow -Wpointer-arith
                         -Wstrict-prototypes -Wmissing-prototypes)
  if(RTOS_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# FreeRTOS kernel (fetched once, pinned tag). Override with
#   -DFREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel  for offline builds.
# ---------------------------------------------------------------------------
set(FREERTOS_KERNEL_TAG "V11.1.0" CACHE STRING "FreeRTOS-Kernel git tag")
set(FREERTOS_KERNEL_PATH "" CACHE PATH "Local FreeRTOS-Kernel checkout (optional)")

macro(rtos_fetch_freertos)
  if(NOT RTOS_FREERTOS_DIR)
    if(FREERTOS_KERNEL_PATH)
      set(RTOS_FREERTOS_DIR "${FREERTOS_KERNEL_PATH}")
    else()
      include(FetchContent)
      FetchContent_Declare(freertos_kernel_src
        GIT_REPOSITORY https://github.com/FreeRTOS/FreeRTOS-Kernel.git
        GIT_TAG        ${FREERTOS_KERNEL_TAG}
        GIT_SHALLOW    TRUE
        # Do not add_subdirectory() the kernel's own CMake project; we compile
        # the sources ourselves so each demo can have its own configuration.
        SOURCE_SUBDIR  do-not-add)
      FetchContent_MakeAvailable(freertos_kernel_src)
      set(RTOS_FREERTOS_DIR "${freertos_kernel_src_SOURCE_DIR}")
    endif()
    set(RTOS_FREERTOS_DIR "${RTOS_FREERTOS_DIR}" CACHE INTERNAL "")
  endif()
endmacro()

# rtos_add_freertos_app(<target> SOURCES a.c b.c [INCLUDES dir...] [HEAP 4])
#
# Builds <target> as an executable linked against a private copy of the
# FreeRTOS kernel compiled for the GCC/POSIX (Linux simulator) port, using
# common/freertos/FreeRTOSConfig.h plus an optional app_config.h found in the
# project's include directories.
function(rtos_add_freertos_app target)
  cmake_parse_arguments(ARG "" "HEAP" "SOURCES;INCLUDES" ${ARGN})
  if(NOT ARG_HEAP)
    set(ARG_HEAP 4)
  endif()
  rtos_fetch_freertos()
  set(K "${RTOS_FREERTOS_DIR}")
  set(P "${K}/portable/ThirdParty/GCC/Posix")

  add_library(${target}_kernel STATIC
    ${K}/tasks.c ${K}/queue.c ${K}/list.c ${K}/timers.c
    ${K}/event_groups.c ${K}/stream_buffer.c
    ${K}/portable/MemMang/heap_${ARG_HEAP}.c
    ${P}/port.c ${P}/utils/wait_for_event.c)
  target_include_directories(${target}_kernel PUBLIC
    ${ARG_INCLUDES}
    ${RTOS_REPO_ROOT}/common/freertos
    ${K}/include ${P} ${P}/utils)
  target_link_libraries(${target}_kernel PUBLIC Threads::Threads)
  # Third-party code: silence its warnings instead of patching it.
  target_compile_options(${target}_kernel PRIVATE -w)

  add_executable(${target} ${ARG_SOURCES} ${RTOS_REPO_ROOT}/common/freertos/freertos_hooks.c)
  target_include_directories(${target} PRIVATE ${ARG_INCLUDES})
  target_link_libraries(${target} PRIVATE ${target}_kernel)
  rtos_own_warnings(${target})
endfunction()

# rtos_add_host_exe(<target> SOURCES ... [INCLUDES ...]) - plain C, no RTOS.
function(rtos_add_host_exe target)
  cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES;LIBS" ${ARGN})
  add_executable(${target} ${ARG_SOURCES})
  target_include_directories(${target} PRIVATE ${ARG_INCLUDES} ${RTOS_REPO_ROOT}/common/test)
  target_link_libraries(${target} PRIVATE ${ARG_LIBS} m)
  rtos_own_warnings(${target})
endfunction()

# rtos_add_demo_test(<name> <target> [ARGS ...] [TIMEOUT s] [PASS_REGEX re] [INPUT file])
# Registers a ctest that runs a demo and requires BOTH exit status 0 AND the
# expected output (default: the line "RESULT: PASS"). Demos are deterministic
# in outcome: they run a fixed number of iterations, self-check, then end the
# scheduler.
function(rtos_add_demo_test name target)
  cmake_parse_arguments(ARG "" "TIMEOUT;PASS_REGEX;INPUT" "ARGS" ${ARGN})
  if(NOT ARG_TIMEOUT)
    set(ARG_TIMEOUT 60)
  endif()
  if(NOT ARG_PASS_REGEX)
    set(ARG_PASS_REGEX "RESULT: PASS")
  endif()
  string(REPLACE ";" "\\;" args_escaped "${ARG_ARGS}")
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:${target}> "-DARGS=${args_escaped}"
            -DINPUT=${ARG_INPUT} "-DREGEX=${ARG_PASS_REGEX}" -DTIMEOUT=${ARG_TIMEOUT}
            -P ${RTOS_REPO_ROOT}/cmake/run_demo.cmake)
  math(EXPR outer "${ARG_TIMEOUT} + 10")
  set_tests_properties(${name} PROPERTIES TIMEOUT ${outer} LABELS demo)
endfunction()

# rtos_add_unit_test(<target> SOURCES ...) - unit test executable + ctest entry.
function(rtos_add_unit_test target)
  cmake_parse_arguments(ARG "" "" "SOURCES;INCLUDES;LIBS" ${ARGN})
  rtos_add_host_exe(${target} SOURCES ${ARG_SOURCES} INCLUDES ${ARG_INCLUDES} LIBS ${ARG_LIBS})
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES TIMEOUT 60 LABELS unit)
endfunction()
