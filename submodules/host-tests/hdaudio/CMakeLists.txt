# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
# PURPOSE:     The codec driver's selection of the Audio Input converters whose input amplifier makes a jack's capture gain (capture_amps.c), over widgets the cases build; under ASan and UBSan
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(hdaudio t_hdaudio.c
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/drivers/hdaudio/capture_amps.c)

target_include_directories(hdaudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../fwload/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/../fwload
    ${REACTOS_HOST_SOURCE_ROOT}/drivers/wdm/audio/drivers/hdaudio)

target_compile_features(hdaudio PRIVATE c_std_99)
target_compile_options(hdaudio PRIVATE -Wall -Werror -Wno-unused-function -g
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(hdaudio PRIVATE -fsanitize=address,undefined)

add_test(NAME hdaudio.capture COMMAND hdaudio)
set_tests_properties(hdaudio.capture PROPERTIES TIMEOUT 60)
