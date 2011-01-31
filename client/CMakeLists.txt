include_directories(${mosquitto_SOURCE_DIR}/lib
			${STDBOOL_H_PATH} ${STDINT_H_PATH})
link_directories(${mosquitto_BINARY_DIR}/lib)

add_executable(mosquitto_pub pub_client.c)
add_executable(mosquitto_sub sub_client.c)

target_link_libraries(mosquitto_pub libmosquitto)
target_link_libraries(mosquitto_sub libmosquitto)

set_target_properties(mosquitto_pub PROPERTIES
	VERSION ${VERSION})
set_target_properties(mosquitto_sub PROPERTIES
	VERSION ${VERSION})
install(TARGETS mosquitto_pub RUNTIME DESTINATION ${BINDEST} LIBRARY DESTINATION ${LIBDEST})
install(TARGETS mosquitto_sub RUNTIME DESTINATION ${BINDEST} LIBRARY DESTINATION ${LIBDEST})
