add_executable(switch switch.c config.c physical_layer.c)
target_link_libraries(switch pcap json-c)

add_executable(router router.c config.c physical_layer.c ether_layer.c)
target_link_libraries(router pcap json-c)
