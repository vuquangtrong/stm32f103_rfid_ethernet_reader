cmake_minimum_required(VERSION 3.22)

project(app)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fpack-struct")

add_library(app INTERFACE)

set (LWIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/LwIP)

set (LWIP_INCLUDE_DIRS
    ${LWIP_DIR}/src/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc # for include "lwipopts.h", "arch/cc.h"
)

include(${LWIP_DIR}/src/Filelists.cmake)

target_compile_definitions(app INTERFACE
)

target_include_directories(app INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${LWIP_INCLUDE_DIRS}
)

file(GLOB_RECURSE APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/*.c
)

target_sources(app INTERFACE
    ${APP_SOURCES}
)

target_link_directories(app INTERFACE
)

target_link_libraries(app INTERFACE
    stm32cubemx
    lwipcore
)
