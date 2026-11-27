# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The device enumerator's notification clients and the diff of a device set (dll/win32/mmdevapi/devenum_notif.h, the code mmdevapi.dll compiles): the three device callbacks per verdict on every registered client and none for an unchanged set, a client unregistering itself or another under a callback, a walk under a callback, the dead wrappers freed when the last walk ends; under ASan and UBSan
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(mmdevapi t_mmdevapi.c)

target_include_directories(mmdevapi PRIVATE
    ${REACTOS_HOST_SOURCE_ROOT}/sdk/include
    ${REACTOS_HOST_SOURCE_ROOT}/dll/win32/mmdevapi)

target_compile_features(mmdevapi PRIVATE c_std_99)
target_compile_options(mmdevapi PRIVATE -Wall -Werror -Wno-unused-function -g
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(mmdevapi PRIVATE -fsanitize=address,undefined)

add_test(NAME mmdevapi.devenum COMMAND mmdevapi)
set_tests_properties(mmdevapi.devenum PROPERTIES TIMEOUT 120)
