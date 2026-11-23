# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bit-exact checks of the shared audio format rules
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(audiofmt t_audiofmt.c)
target_include_directories(audiofmt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/include)
add_test(NAME audiofmt.rules COMMAND audiofmt)
