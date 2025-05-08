cmake_minimum_required(VERSION 3.10)

project(ucanet C)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(PkgConfig)

include(FindPackageHandleStandardArgs)
include(PkgConfigVars)
include(GNUInstallDirs)
include(cmake/options.cmake) # Include the options file

add_definitions("-std=c99 -Wall -fPIC")
add_definitions(-DG_LOG_DOMAIN="Uca-Net")

pkg_check_modules(GIO gio-2.0>=2.22 REQUIRED)
pkg_check_modules(UCA libuca>=2.1.0 REQUIRED)
pkg_check_modules(ZMQ libzmq)
pkg_check_modules(JSON_GLIB json-glib-1.0)
pkg_check_variable(libuca plugindir)

# Ensure we have a numeric value for UCA_NET_NUM_DERIVED_CAMERAS
if(NOT UCA_NET_NUM_DERIVED_CAMERAS MATCHES "^[0-9]+$")
    message(STATUS "UCA_NET_NUM_DERIVED_CAMERAS is not a number, defaulting to 10")
    set(UCA_NET_NUM_DERIVED_CAMERAS 10 CACHE STRING "Number of derived net camera classes" FORCE)
endif()

if (UNIX)
    set(HAVE_UNIX 1)
endif ()

if (ZMQ_FOUND AND JSON_GLIB_FOUND)
    option(WITH_ZMQ_NETWORKING "Enable sending data over network with zmq" ON)
endif ()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h)

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


# uca-net server
add_executable(ucad ucad.c)

target_link_libraries(ucad
    ${UCA_LIBRARIES}
    ${GIO_LIBRARIES}
    ${ZMQ_LIBRARIES}
    ${JSON_GLIB_LIBRARIES})

install(TARGETS ucad
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT executables)

# Generate and build derived camera classes
find_program(PYTHON_EXECUTABLE python3)

# Generate source files for derived classes
math(EXPR UCA_NET_DERIVED_CLASSES_MAX "${UCA_NET_NUM_DERIVED_CAMERAS} - 1")
foreach(CAMERA_IDX RANGE 0 ${UCA_NET_DERIVED_CLASSES_MAX})
    set(INPUT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/uca-net-template-camera.h.in")
    set(INPUT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/uca-net-template-camera.c.in")
    set(OUTPUT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/uca-net-${CAMERA_IDX}camera.h")
    set(OUTPUT_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/uca-net-${CAMERA_IDX}camera.c")

    add_custom_command(
        OUTPUT ${OUTPUT_HEADER} ${OUTPUT_SOURCE}
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate.py
                ${INPUT_HEADER} ${OUTPUT_HEADER} ${CAMERA_IDX}
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate.py
                ${INPUT_SOURCE} ${OUTPUT_SOURCE} ${CAMERA_IDX}
        DEPENDS ${INPUT_HEADER} ${INPUT_SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/generate.py
        COMMENT "Generating camera class ${CAMERA_IDX}"
    )


    # Build shared library for each derived camera class
    add_library(ucanet${CAMERA_IDX} SHARED
            uca-net-base-camera.c
        ${CMAKE_CURRENT_BINARY_DIR}/uca-net-${CAMERA_IDX}camera.c
    )
    
    target_link_libraries(ucanet${CAMERA_IDX}
        ${UCA_LIBRARIES}
        ${GIO_LIBRARIES}
    )
    
    install(TARGETS ucanet${CAMERA_IDX}
        LIBRARY DESTINATION ${LIBUCA_PLUGINDIR}
        RUNTIME DESTINATION ${LIBUCA_PLUGINDIR}
    )
    
    # Make sure the files are generated before building
    add_custom_target(generate_camera_${CAMERA_IDX} DEPENDS
        ${OUTPUT_HEADER}
        ${OUTPUT_SOURCE}
    )
    add_dependencies(ucanet${CAMERA_IDX} generate_camera_${CAMERA_IDX})
endforeach()

set(INPUT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/uca-net-template-camera.h.in")
set(INPUT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/uca-net-template-camera.c.in")
set(OUTPUT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/uca-net-camera.h")
set(OUTPUT_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/uca-net-camera.c")

add_custom_command(
        OUTPUT ${OUTPUT_HEADER} ${OUTPUT_SOURCE}
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate.py
        ${INPUT_HEADER} ${OUTPUT_HEADER} ''
        COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate.py
        ${INPUT_SOURCE} ${OUTPUT_SOURCE} ''
        DEPENDS ${INPUT_HEADER} ${INPUT_SOURCE} ${CMAKE_CURRENT_SOURCE_DIR}/generate.py
        COMMENT "Generating default camera class"
)


# Build shared library for each derived camera class
add_library(ucanet SHARED
        uca-net-base-camera.c
        ${CMAKE_CURRENT_BINARY_DIR}/uca-net-camera.c
)

target_link_libraries(ucanet
        ${UCA_LIBRARIES}
        ${GIO_LIBRARIES}
)

install(TARGETS ucanet
        LIBRARY DESTINATION ${LIBUCA_PLUGINDIR}
        RUNTIME DESTINATION ${LIBUCA_PLUGINDIR}
)

# Make sure the files are generated before building
add_custom_target(generate_camera DEPENDS
        ${OUTPUT_HEADER}
        ${OUTPUT_SOURCE}
)
add_dependencies(ucanet generate_camera)