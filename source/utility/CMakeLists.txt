###########################################################
## SOURCES
###########################################################

set(TSSX_UTILITY_SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/utility.c
	${CMAKE_CURRENT_SOURCE_DIR}/benchmarks.c
	${CMAKE_CURRENT_SOURCE_DIR}/signals.c
	${CMAKE_CURRENT_SOURCE_DIR}/arguments.c
	${CMAKE_CURRENT_SOURCE_DIR}/process.c
	${CMAKE_CURRENT_SOURCE_DIR}/sockets.c
	${CMAKE_CURRENT_SOURCE_DIR}/parent.c
)

###########################################################
## TARGETS
###########################################################

add_library(tssx-utility STATIC ${TSSX_UTILITY_SOURCES})
target_compile_options(tssx-utility PUBLIC -fPIC)
