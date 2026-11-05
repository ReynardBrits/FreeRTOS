cmake_minimum_required(VERSION 3.21)
project(FreeRTOSAssignment LANGUAGES C)

add_library(freertos_config INTERFACE)

target_include_directories(freertos_config INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(FREERTOS_PORT "MSVC_MINGW" CACHE STRING "FreeRTOS Windows port")
set(FREERTOS_HEAP "4" CACHE STRING "FreeRTOS heap implementation")

add_subdirectory(third_party/FreeRTOS-Kernel)

add_executable(parking parking.c)
target_link_libraries(parking PRIVATE freertos_kernel)