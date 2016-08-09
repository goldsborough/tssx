###########################################################
## SOURCES
###########################################################

set(TSSX_COMMON_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/buffer.c
	${CMAKE_CURRENT_SOURCE_DIR}/connection.c
  ${CMAKE_CURRENT_SOURCE_DIR}/connection-options.c
	${CMAKE_CURRENT_SOURCE_DIR}/hashtable.c
	${CMAKE_CURRENT_SOURCE_DIR}/common-overrides.c
  ${CMAKE_CURRENT_SOURCE_DIR}/socket-overrides.c
  ${CMAKE_CURRENT_SOURCE_DIR}/poll-overrides.c
  ${CMAKE_CURRENT_SOURCE_DIR}/select-overrides.c
	${CMAKE_CURRENT_SOURCE_DIR}/shared-memory.c
	${CMAKE_CURRENT_SOURCE_DIR}/timeouts.c
	${CMAKE_CURRENT_SOURCE_DIR}/string-set.c
	${CMAKE_CURRENT_SOURCE_DIR}/selective.c
	${CMAKE_CURRENT_SOURCE_DIR}/session-table.c
  ${CMAKE_CURRENT_SOURCE_DIR}/session.c
	${CMAKE_CURRENT_SOURCE_DIR}/free-list.c
	${CMAKE_CURRENT_SOURCE_DIR}/bridge.c
  ${CMAKE_CURRENT_SOURCE_DIR}/reverse-map.c
  ${CMAKE_CURRENT_SOURCE_DIR}/common-poll-overrides.c
)

if(APPLE)
  set(TSSX_COMMON_SOURCES
    ${TSSX_COMMON_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/epoll-overrides.c
  )
endif()

set(TSSX_SERVER_SOURCES
  ${TSSX_COMMON_SOURCES}
  ${CMAKE_CURRENT_SOURCE_DIR}/server-overrides.c
)

set(TSSX_CLIENT_SOURCES
  ${TSSX_COMMON_SOURCES}
  ${CMAKE_CURRENT_SOURCE_DIR}/client-overrides.c
)

###########################################################
## TARGETS
###########################################################

add_library(tssx-server SHARED ${TSSX_SERVER_SOURCES})
add_library(tssx-client SHARED ${TSSX_CLIENT_SOURCES})

###########################################################
## COMPILER FLAGS
###########################################################

set(TSSX_DEPENDENCIES tssx-utility dl)

target_link_libraries(tssx-server ${TSSX_DEPENDENCIES})
target_link_libraries(tssx-client ${TSSX_DEPENDENCIES})
