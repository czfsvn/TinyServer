file(GLOB DATASERVER_SOURCES "*.cpp")
add_executable(dataserver ${DATASERVER_SOURCES})
target_link_libraries(dataserver PRIVATE base)