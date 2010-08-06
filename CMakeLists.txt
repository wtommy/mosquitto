# This is a cmake script.
# It is only intended for building libmosquitto and the clients and can be
# used to generate Visual Studio projects, allowing them to be compiled
# natively on Windows.

project(mosquitto)

cmake_minimum_required(VERSION 2.4)
cmake_policy(SET CMP0003 NEW)

if (WIN32)
    set (BINDEST .)
    set (LIBDEST .)
    set (SHAREDEST .)
else (WIN32)
    set (BINDEST bin)
    set (LIBDEST lib${LIB_SUFFIX})
    set (SHAREDEST share/mosquitto)
endif (WIN32)

add_subdirectory(lib)
add_subdirectory(client)

