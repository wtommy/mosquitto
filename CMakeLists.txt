# This is a cmake script.
# It is intended to be the default means of building mosquitto.

project(mosquitto)

cmake_minimum_required(VERSION 2.6)

set (VERSION 0.9.99)

if (WIN32)
	execute_process(COMMAND echo "%DATE% %TIME%" OUTPUT_VARIABLE TIMESTAMP
			OUTPUT_STRIP_TRAILING_WHITESPACE)
else (WIN32)
	execute_process(COMMAND date "+%F %T%z" OUTPUT_VARIABLE TIMESTAMP
			OUTPUT_STRIP_TRAILING_WHITESPACE)
endif (WIN32)

add_definitions (-DCMAKE -DVERSION=\"${VERSION}\" -DTIMESTAMP=\"${TIMESTAMP}\")

if (WIN32)
	set (BINDIR .)
	set (SBINDIR .)
	set (SYSCONFDIR .)
	set (LIBDIR .)
	set (INCLUDEDIR include)
	set (DATAROOTDIR share)
	set (MANDIR man)
    set (SHAREDEST .)
else (WIN32)
	set (BINDIR bin)
	set (SBINDIR sbin)
	set (SYSCONFDIR etc/mosquitto)
	set (LIBDIR lib${LIB_SUFFIX})
	set (INCLUDEDIR include)
	set (DATAROOTDIR share)
	set (MANDIR ${DATAROOTDIR}/man)
	set (SHAREDIR ${DATAROOTDIR}/mosquitto)
endif (WIN32)

# ========================================
# Include projects
# ========================================

add_subdirectory(lib)
add_subdirectory(client)
add_subdirectory(src)
add_subdirectory(man)

# ========================================
# Install config file
# ========================================

install(FILES mosquitto.conf aclfile.example pwfile.example DESTINATION ${SYSCONFDIR})
