add_library(nhlt nhlt_parse.c)
add_dependencies(nhlt bugcodes xdk)
target_include_directories(nhlt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
