cmake_minimum_required(VERSION 2.6)

if (POLICY CMP0053)
cmake_policy(SET CMP0053 OLD)
endif ()

if (POLICY CMP0054)
    cmake_policy(SET CMP0054 OLD)
endif ()

project(ucanet C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(FindPackageHandleStandardArgs)
include(PkgConfigVars)
include(ConfigurePaths)

find_package(PkgConfig)

add_definitions("-std=c99 -Wall -fPIC")
add_definitions(-DG_LOG_DOMAIN="Uca-Net")

pkg_check_modules(GIO gio-2.0>=2.22 REQUIRED)
pkg_check_modules(UCA libuca>=2.1.0 REQUIRED)
pkg_check_variable(libuca plugindir)

set(UCA_NET_DEFAULT_PORT 8989)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} ${UCA_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS})
link_directories(${UCA_LIBRARY_DIRS} ${GIO_LIBRARY_DIRS})

# uca-net client camera
configure_paths(UCANET)
set(UCANET_PLUGINDIR "${UCANET_LIBDIR}/uca") # TODO: Maybe move this to ConfigurePaths?
add_library(ucanet SHARED uca-net-camera.c)
target_link_libraries(ucanet ${UCA_LIBRARIES} ${GIO_LIBRARIES})

install(TARGETS ucanet LIBRARY DESTINATION ${UCANET_PLUGINDIR} RUNTIME DESTINATION ${UCANET_PLUGINDIR})

# uca-net server
configure_paths(UCAD)
add_executable(ucad ucad.c)

target_link_libraries(ucad ${UCA_LIBRARIES} ${GIO_LIBRARIES})

install(TARGETS ucad RUNTIME DESTINATION ${UCAD_BINDIR} COMPONENT executables)
