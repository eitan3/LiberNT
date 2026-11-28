# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The dsploop apitest's comparison, the same source the apitest compiles (modules/rostests/apitests/dsploop/compare.c): the offset from the first matching frame, the comparison after it, the first differing sample and the refusals, in the three formats; under ASan and UBSan
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(dsploop t_dsploop.c
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/apitests/dsploop/compare.c)

target_include_directories(dsploop PRIVATE
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/apitests/dsploop)

target_compile_features(dsploop PRIVATE c_std_99)
target_compile_options(dsploop PRIVATE -Wall -Werror -Wno-unused-function -g
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(dsploop PRIVATE -fsanitize=address,undefined)
# the gain's table check against 10^(-dB/20) (t_dsploop.c CheckGain) needs the math library
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(dsploop PRIVATE ${MATH_LIBRARY})
endif()

add_test(NAME dsploop.compare COMMAND dsploop)
set_tests_properties(dsploop.compare PROPERTIES TIMEOUT 120)
