# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     The expected switch of the apitests (modules/rostests/apitests/include/apitest_required.h, the decision the apitests run): the five values of R8-09 and the boundary of the bytes, each with the kind it is read as and whether a missing precondition fails; under ASan and UBSan
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(apitest t_apitest.c)

target_include_directories(apitest PRIVATE
    ${REACTOS_HOST_SOURCE_ROOT}/modules/rostests/apitests/include)

target_compile_features(apitest PRIVATE c_std_99)
target_compile_options(apitest PRIVATE -Wall -Werror -Wno-unused-function -g
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(apitest PRIVATE -fsanitize=address,undefined)

add_test(NAME apitest.required COMMAND apitest)
set_tests_properties(apitest.required PROPERTIES TIMEOUT 120)
