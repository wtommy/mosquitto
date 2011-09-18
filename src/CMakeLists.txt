include_directories(${mosquitto_SOURCE_DIR} ${mosquitto_SOURCE_DIR}/src
		${mosquitto_SOURCE_DIR}/lib ${OPENSSL_INCLUDE_DIR}
		${STDBOOL_H_PATH} ${STDINT_H_PATH})

set (MOSQ_SRCS
	conf.c
	context.c
	database.c
	logging.c
	loop.c
	../lib/memory_mosq.c ../lib/memory_mosq.h
	mosquitto.c
	mqtt3.h
	net.c
	../lib/net_mosq.c ../lib/net_mosq.h
	persist.c persist.h
	raw_send.c raw_send_server.c
	read_handle.c read_handle_client.c read_handle_server.c
	subs.c
	security.c security_external.c
	../lib/send_base_mosq.c
	../lib/send_client_mosq.c ../lib/send_mosq.h
	util.c
	../lib/util_mosq.c ../lib/util_mosq.h
	../lib/will_mosq.c ../lib/will_mosq.h)

set (MOSQ_LIBS ${OPENSSL_LIBRARIES})

option(INC_BRIDGE_SUPPORT
	"Include bridge support for connecting to other brokers?" ON)
if (${INC_BRIDGE_SUPPORT} STREQUAL ON)
	set (MOSQ_SRCS ${MOSQ_SRCS} bridge.c)
	add_definitions("-DWITH_BRIDGE")
endif (${INC_BRIDGE_SUPPORT} STREQUAL ON)


option(USE_LIBWRAP
	"Include tcp-wrappers support?" OFF)

if (${USE_LIBWRAP} STREQUAL ON)
	set (MOSQ_LIBS ${MOSQ_LIBS} wrap)
	add_definitions("-DWITH_WRAP")
endif (${USE_LIBWRAP} STREQUAL ON)

option(INC_DB_UPGRADE
	"Include database upgrade support? (recommended)" ON)

option(INC_MEMTRACK
	"Include memory tracking support?" ON)
if (${INC_MEMTRACK} STREQUAL ON)
	add_definitions("-DWITH_MEMORY_TRACKING")
endif (${INC_MEMTRACK} STREQUAL ON)

option(WITH_PERSISTENCE
	"Include persistence support?" ON)
if (${WITH_PERSISTENCE} STREQUAL ON)
	add_definitions("-DWITH_PERSISTENCE")
endif (${WITH_PERSISTENCE} STREQUAL ON)


option(WITH_EXTERNAL_SECURITY_CHECKS
	"Use external security checks (needs additional code)?" OFF)
if (${WITH_EXTERNAL_SECURITY_CHECKS} STREQUAL ON)
	add_definitions("-DWITH_EXTERNAL_SECURITY_CHECKS")
endif (${WITH_EXTERNAL_SECURITY_CHECKS} STREQUAL ON)

add_definitions (-DWITH_BROKER)

add_executable(mosquitto WIN32 ${MOSQ_SRCS})

if (NOT WIN32)
	target_link_libraries(mosquitto ${MOSQ_LIBS})
else (NOT WIN32)
	target_link_libraries(mosquitto ws2_32 ${MOSQ_LIBS})
endif (NOT WIN32)
set_target_properties(mosquitto PROPERTIES
	VERSION ${VERSION})

install(TARGETS mosquitto RUNTIME DESTINATION ${SBINDIR} LIBRARY DESTINATION ${LIBDIR})

if (UNIX)
	install(CODE "EXEC_PROGRAM(/sbin/ldconfig)")
endif (UNIX)

