cmake_minimum_required(VERSION 3.10)
project(ucanet C)

if (NOT EXISTS ${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
  message(FATAL_ERROR "Please run 'conan install' first!")
endif()
include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

conan_basic_setup(TARGETS)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(GNUInstallDirs)

if (GNU)
    add_definitions("-std=c99 -Wall -fPIC")
endif()

add_definitions(-DG_LOG_DOMAIN="Uca-Net")


set(UCA_NET_DEFAULT_PORT 8989)

if (UNIX)
    set(HAVE_UNIX 1)
endif ()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${UCA_INCLUDE_DIRS}
    ${GIO_INCLUDE_DIRS})

link_directories(
    ${UCA_LIBRARY_DIRS}
    ${GIO_LIBRARY_DIRS})

# uca-net client camera
add_library(ucanet SHARED uca-net-camera.c)

target_link_libraries(ucanet
    PUBLIC CONAN_PKG::libuca)

install(TARGETS ucanet
        LIBRARY DESTINATION ${LIBUCA_PLUGINDIR}
        RUNTIME DESTINATION ${LIBUCA_PLUGINDIR})

# uca-net server
add_executable(ucad ucad.c)

target_link_libraries(ucad
    PUBLIC CONAN_PKG::libuca)

install(TARGETS ucad
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT executables)
