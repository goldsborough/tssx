###########################################################
## SOURCES
###########################################################

set(TSSX_TRY_COMMON_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/try-common.c
  ${CMAKE_CURRENT_SOURCE_DIR}/try-select.c
  ${CMAKE_CURRENT_SOURCE_DIR}/try-poll.c
  ${CMAKE_CURRENT_SOURCE_DIR}/try-epoll.c
)

set(TSSX_TRY_SERVER_SOURCES
 ${TSSX_TRY_COMMON_SOURCES}
 ${CMAKE_CURRENT_SOURCE_DIR}/try-server.c
)

set(TSSX_TRY_CLIENT_SOURCES
 ${TSSX_TRY_COMMON_SOURCES}
 ${CMAKE_CURRENT_SOURCE_DIR}/try-client.c
)

###########################################################
## TARGET
###########################################################

add_executable(try-server ${TSSX_TRY_SERVER_SOURCES})
add_executable(try-client ${TSSX_TRY_CLIENT_SOURCES})
