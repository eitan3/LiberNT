add_library(sofipc4 sofipc4.c)
add_dependencies(sofipc4 bugcodes xdk)
target_include_directories(sofipc4 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
