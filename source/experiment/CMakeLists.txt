###########################################################
## TARGETS
###########################################################

add_executable(experiment-client client.c)
add_executable(experiment-server server.c)

###########################################################
## DEPENDENCIES
###########################################################

target_link_libraries(experiment-client tssx-utility)
target_link_libraries(experiment-server tssx-utility)
