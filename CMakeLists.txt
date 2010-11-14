# This is a cmake script.
# It is only intended for building libmosquitto and the clients and can be
# used to generate Visual Studio projects, allowing them to be compiled
# natively on Windows.

project(mosquitto)

cmake_minimum_required(VERSION 2.6)

set (VERSION 0.9)

if (WIN32)
	execute_process(COMMAND echo "%DATE% %TIME%" OUTPUT_VARIABLE TIMESTAMP
			OUTPUT_STRIP_TRAILING_WHITESPACE)
else (WIN32)
	execute_process(COMMAND date "+%F %T%z" OUTPUT_VARIABLE TIMESTAMP
			OUTPUT_STRIP_TRAILING_WHITESPACE)
endif (WIN32)

add_definitions (-DVERSION=\"${VERSION}\" -DTIMESTAMP=\"${TIMESTAMP}\")

if (WIN32)
    set (BINDEST .)
    set (LIBDEST .)
    set (SHAREDEST .)
else (WIN32)
    set (BINDEST bin)
    set (LIBDEST lib${LIB_SUFFIX})
    set (SHAREDEST share/mosquitto)
endif (WIN32)

# ========================================
# Check for required compatibility headers
# ========================================
include (CheckIncludeFile)

check_include_file(stdbool.h HAVE_STDBOOL_H)
if (HAVE_STDBOOL_H)
	set (STDBOOL_H_PATH ${mosquitto_SOURCE_DIR}/compat/stdbool)
endif (HAVE_STDBOOL_H)

check_include_file(stdint.h HAVE_STDINT_H)
if (HAVE_STDINT_H)
	set (STDINT_H_PATH ${mosquitto_SOURCE_DIR}/compat/stdint)
endif (HAVE_STDINT_H)

# ========================================
# Include projects
# ========================================

add_subdirectory(lib)
add_subdirectory(client)

if (NOT WIN32)
	add_subdirectory(src)
endif (NOT WIN32)
