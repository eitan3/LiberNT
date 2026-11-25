add_library(fwload sof_ri.c cl_dma.c fwboot.c fwimage.c)
add_dependencies(fwload bugcodes xdk)
target_include_directories(fwload PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fwload PUBLIC sofipc4)
