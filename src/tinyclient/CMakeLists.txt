file(GLOB SOURCES "*.cpp")
add_executable(tinyclient ${SOURCES})
target_link_libraries(tinyclient PRIVATE base)