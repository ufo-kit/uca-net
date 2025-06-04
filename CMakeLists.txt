cmake_minimum_required(VERSION 3.10)
project(ucanet C)

set(CMAKE_C_STANDARD 99)

option(WITH_ZMQ_NETWORKING "Enable sending data over network with zmq" ON)
option(USE_FIND_PACKAGE_FOR_GLIB "Use find_package instead of pkg-config to find GLib dependencies" OFF)

if (USE_FIND_PACKAGE_FOR_GLIB)
    find_package(libuca REQUIRED)
    find_package(glib REQUIRED)

    set(UCANET_DEPS glib::glib libuca::libuca)
    set(UCAD_DEPS glib::glib libuca::libuca)

    if (WITH_ZMQ_NETWORKING)
        find_package(ZeroMQ REQUIRED)
        set(UCAD_DEPS ${UCAD_DEPS} libzmq-static)
    endif()
else()
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

    find_package(PkgConfig)

    include(FindPackageHandleStandardArgs)
    include(PkgConfigVars)
    include(GNUInstallDirs)

    pkg_check_modules(GIO gio-2.0>=2.22 REQUIRED)
    pkg_check_modules(UCA libuca>=2.1.0 REQUIRED)
    pkg_check_variable(libuca plugindir)

    if (WITH_ZMQ_NETWORKING)
        pkg_check_modules(ZMQ libzmq)
        pkg_check_modules(JSON_GLIB json-glib-1.0)
        if (NOT ZMQ_FOUND OR NOT JSON_GLIB_FOUND)
            message(FATAL "ZMQ dependencies not found")
        endif ()
    endif()

    include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
        ${UCA_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
        ${ZMQ_INCLUDE_DIRS}
        ${JSON_GLIB_INCLUDE_DIRS})

    link_directories(
        ${UCA_LIBRARY_DIRS}
        ${GIO_LIBRARY_DIRS}
        ${ZMQ_LIBRARY_DIRS}
        ${JSON_GLIB_LIBRARY_DIRS})

    set(UCANET_DEPS
        ${UCA_LIBRARIES}
        ${GIO_LIBRARIES})

    set(UCAD_DEPS 
        ${UCA_LIBRARIES}
        ${GIO_LIBRARIES}
        ${ZMQ_LIBRARIES}
        ${JSON_GLIB_LIBRARIES})
endif()

#add_definitions("-Wall -fPIC")
add_definitions(-DG_LOG_DOMAIN="Uca-Net")

set(UCA_NET_DEFAULT_PORT 8989)

if (UNIX)
    set(HAVE_UNIX 1)
endif ()

set(GENERATED_CODE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${GENERATED_CODE_DIR}/config.h)

# uca-net client camera
add_library(ucanet SHARED uca-net-camera.c)

target_link_libraries(ucanet
    PUBLIC ${UCANET_DEPS})

target_include_directories(ucanet
    PUBLIC ${GENERATED_CODE_DIR})

install(TARGETS ucanet
    LIBRARY DESTINATION ${LIBUCA_PLUGINDIR}
    RUNTIME DESTINATION ${LIBUCA_PLUGINDIR})

# uca-net server
add_executable(ucad ucad.c)

target_link_libraries(ucad
    PUBLIC ${UCAD_DEPS})

target_include_directories(ucad
    PUBLIC ${GENERATED_CODE_DIR})

install(TARGETS ucad
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT executables)
