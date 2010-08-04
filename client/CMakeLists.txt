include_directories(${mosquitto_SOURCE_DIR}/lib)
link_directories(${mosquitto_SOURCE_DIR}/lib)

add_executable(mosquitto_pub pub_client.c)
add_executable(mosquitto_sub sub_client.c)

target_link_libraries(mosquitto_pub mosquitto)
target_link_libraries(mosquitto_sub mosquitto)

install(TARGETS mosquitto_pub RUNTIME DESTINATION ${BINDEST} LIBRARY DESTINATION ${LIBDEST})
install(TARGETS mosquitto_sub RUNTIME DESTINATION ${BINDEST} LIBRARY DESTINATION ${LIBDEST})
