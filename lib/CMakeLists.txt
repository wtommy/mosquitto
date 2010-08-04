add_subdirectory(cpp)

include_directories(${mosquitto_SOURCE_DIR}/lib)
link_directories(${mosquitto_SOURCE_DIR}/lib)

add_library(mosquitto SHARED
	logging_mosq.c logging_mosq.h
	messages_mosq.c messages_mosq.h
	mosquitto.c mosquitto.h
	mqtt3_protocol.h
	net_mosq.c net_mosq.h
	read_handle.c read_handle.h
	read_handle_client.c
	send_client_mosq.c
	send_mosq.c send_mosq.h
	util_mosq.c util_mosq.h)

target_link_libraries(mosquitto ws2_32)

install(TARGETS mosquitto RUNTIME DESTINATION ${BINDEST} LIBRARY DESTINATION ${LIBDEST})

if (UNIX)
	install(CODE "EXEC_PROGRAM(/sbin/ldconfig)")
endif (UNIX)