add_library(softplg softplg.c ipc4model.c)
add_dependencies(softplg bugcodes xdk)
target_include_directories(softplg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
