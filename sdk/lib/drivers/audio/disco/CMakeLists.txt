add_library(disco disco.c)
add_dependencies(disco bugcodes xdk)
target_include_directories(disco PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
