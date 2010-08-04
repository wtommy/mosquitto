include_directories(${mosquitto_SOURCE_DIR}/lib ${mosquitto_SOURCE_DIR}/lib/cpp)
link_directories(${mosquitto_SOURCE_DIR}/lib)

add_library(mosquittopp SHARED
	mosquittopp.cpp mosquittopp.h)

target_link_libraries(mosquittopp mosquitto)

install(TARGETS mosquittopp RUNTIME DESTINATION ${BINDEST} LIBRARY DESTINATION ${LIBDEST})

if (UNIX)
	install(CODE "EXEC_PROGRAM(/sbin/ldconfig)")
endif (UNIX)