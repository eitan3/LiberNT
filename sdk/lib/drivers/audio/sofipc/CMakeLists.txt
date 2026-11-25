#
# PROJECT:     LiberNT audio driver libraries
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build rules for the IPC4 transport library
# COPYRIGHT:   Copyright 2026 LiberNT contributors
#

add_library(sofipc sofipc.c)
add_dependencies(sofipc bugcodes xdk)
target_include_directories(sofipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sofipc PUBLIC fwload)
