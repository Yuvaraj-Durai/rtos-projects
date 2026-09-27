# Top-level build: configures every project under projects/ in one tree so the
# FreeRTOS kernel is fetched once. Each project can also be built on its own.
cmake_minimum_required(VERSION 3.16)
project(rtos_projects C)

include(cmake/rtos_common.cmake)

file(GLOB project_dirs LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/projects/[0-9][0-9]-*)
list(SORT project_dirs)
foreach(dir ${project_dirs})
  if(EXISTS ${dir}/CMakeLists.txt)
    add_subdirectory(${dir})
  endif()
endforeach()
