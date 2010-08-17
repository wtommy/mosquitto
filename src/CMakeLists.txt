include_directories(${mosquitto_SOURCE_DIR} ${mosquitto_SOURCE_DIR}/src
		${mosquitto_SOURCE_DIR}/lib)

add_executable(mosquitto 
	bridge.c
	conf.c
	context.c
	database.c
	logging.c
	../lib/memory_mosq.c ../lib/memory_mosq.h
	mosquitto.c
	mqtt3.h
	net.c
	../lib/net_mosq.c ../lib/net_mosq.h
	raw_send.c raw_send_client.c raw_send_server.c
	read_handle.c read_handle_client.c read_handle_server.c
	util.c
	../lib/util_mosq.c ../lib/util_mosq.h)

add_definitions (-DWITH_BROKER)
target_link_libraries(mosquitto sqlite3)

install(TARGETS mosquitto RUNTIME DESTINATION ${BINDEST} LIBRARY DESTINATION ${LIBDEST})

if (UNIX)
	install(CODE "EXEC_PROGRAM(/sbin/ldconfig)")
endif (UNIX)

