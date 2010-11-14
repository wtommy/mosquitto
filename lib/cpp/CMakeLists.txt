include_directories(${mosquitto_SOURCE_DIR}/lib ${mosquitto_SOURCE_DIR}/lib/cpp
			${STDBOOL_H_PATH} ${STDINT_H_PATH})
link_directories(${mosquitto_BINARY_DIR}/lib)

add_library(mosquittopp SHARED
	mosquittopp.cpp mosquittopp.h)

target_link_libraries(mosquittopp mosquitto)
set_target_properties(mosquittopp PROPERTIES
	VERSION ${VERSION}
	SOVERSION 0
)
install(TARGETS mosquittopp RUNTIME DESTINATION ${BINDEST} LIBRARY DESTINATION ${LIBDEST})

if (UNIX)
	install(CODE "EXEC_PROGRAM(/sbin/ldconfig)")
endif (UNIX)
