# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Bit-exact checks of the Core Audio engine's PCM sample conversion and dither (pcm_sample.h): the seed is not the xorshift's fixed point, the dither moves a sample by at most one LSB, and a sample at or past the full scale stays at it; under ASan and UBSan
# COPYRIGHT:   Copyright 2026 LiberNT contributors

add_executable(pcmsample t_pcmsample.c)

target_include_directories(pcmsample PRIVATE
    ${REACTOS_HOST_SOURCE_ROOT}/dll/win32/mmdevapi)

target_compile_features(pcmsample PRIVATE c_std_99)
target_compile_options(pcmsample PRIVATE -Wall -Werror -Wno-unused-function -g
    -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(pcmsample PRIVATE -fsanitize=address,undefined)

add_test(NAME pcmsample.dither COMMAND pcmsample)
