cmake_minimum_required(VERSION 3.10)

set(TARGET_NAME xmlconfigs)

find_package(Boost REQUIRED COMPONENTS system)

set(xml2cpp "${CMAKE_SOURCE_DIR}/../tinytools/xml2cpp.py")
message("xml2cpp: ${xml2cpp}")
message("CMAKE_CURRENT_SOURCE_DIR : ${CMAKE_CURRENT_SOURCE_DIR}")

file(GLOB XML_FILES "${CMAKE_CURRENT_SOURCE_DIR}/xmlfiles/*.xml")

set(GENERATED_FILES "")

foreach(XML_FILE ${XML_FILES})
    get_filename_component(XML_NAME ${XML_FILE} NAME_WE)
    message("XML_FILE : ${XML_FILE}")
    
    set(GENERATED_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/xmlfiles/${XML_NAME}.h")
    set(GENERATED_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/xmlfiles/${XML_NAME}.cpp")
    
    add_custom_command(
        OUTPUT ${GENERATED_HEADER} ${GENERATED_SOURCE}
        COMMAND python3 "${xml2cpp}" "${XML_FILE}" --outdir "${CMAKE_CURRENT_SOURCE_DIR}/xmlfiles/"
        MAIN_DEPENDENCY ${XML_FILE}
        DEPENDS "${xml2cpp}"
        COMMENT "Generating C++ code from ${XML_FILE}"
    )
    
    list(APPEND GENERATED_FILES ${GENERATED_HEADER} ${GENERATED_SOURCE})
endforeach()

set(SOURCES
    xmlloader.cpp
    ${GENERATED_FILES}
)

set(HEADERS
    xmlloader.h
)

add_library(${TARGET_NAME} STATIC ${SOURCES} ${HEADERS})

target_link_libraries(${TARGET_NAME}
    PRIVATE
        ${Boost_LIBRARIES}
        base
)

target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${Boost_INCLUDE_DIRS}
)

target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)

install(TARGETS ${TARGET_NAME}
    ARCHIVE DESTINATION lib
)