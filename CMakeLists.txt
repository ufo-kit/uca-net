cmake_minimum_required(VERSION 2.6)
project(ucanet C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(FindPackageHandleStandardArgs)
include(PkgConfigVars)
include(ConfigurePaths)

find_package(PkgConfig)

add_definitions("-std=c99 -Wall")
add_definitions(-DG_LOG_DOMAIN="Uca-Net")

pkg_check_modules(GIO gio-2.0>=2.22 REQUIRED)
pkg_check_modules(UCA libuca>=2.1.0 REQUIRED)
pkg_check_variable(libuca plugindir)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${UCA_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS} ${MSGPACK_INCLUDE_DIRS})
link_directories(${UCA_LIBRARY_DIRS} ${GIO_LIBRARY_DIRS})

# uca-net client camera
add_library(ucanet SHARED uca-net-camera.c uca-net-client.c)
target_link_libraries(ucanet ${UCA_LIBRARIES} ${MSGPACK_LIBRARIES})

install(TARGETS ucanet LIBRARY DESTINATION ${LIBUCA_PLUGINDIR})

# uca-net server
configure_paths(UCAD)
add_executable(ucad ucad.c uca-net-server.c)

target_link_libraries(ucad ${UCA_LIBRARIES} ${GIO_LIBRARIES})

install(TARGETS ucad RUNTIME DESTINATION ${UCAD_BINDIR} COMPONENT executables)
