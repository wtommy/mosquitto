# This is a cmake script.
# It is only intended for building libmosquitto and the clients and can be
# used to generate Visual Studio projects, allowing them to be compiled
# natively on Windows.

project(mosquitto)

cmake_minimum_required(VERSION 2.6)

set (VERSION 0.8.2)

add_definitions (-DVERSION=\"${VERSION}\" -DTIMESTAMP=\"\")

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

if (NOT WIN32)
	add_subdirectory(src)
endif (NOT WIN32)
