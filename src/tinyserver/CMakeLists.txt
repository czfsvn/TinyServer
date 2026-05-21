


file(GLOB SOURCES "*.cpp")
add_executable(tinyserver ${SOURCES})
target_link_libraries(tinyserver PRIVATE base)

