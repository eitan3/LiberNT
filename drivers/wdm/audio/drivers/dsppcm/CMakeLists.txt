#
# PROJECT:     LiberNT Intel Smart Sound DSP endpoint driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build rules of the endpoint driver of the DSP's audio children, after the codec driver's (hdaudio)
# COPYRIGHT:   Copyright 2026 LiberNT contributors
#

list(APPEND SOURCE
    dsppcm.cpp
    adapter.cpp
    mintopo.cpp
    minwave.cpp
    stream.cpp
    prophnd.cpp
    purecall.cpp
    guid.cpp
    # the endpoint's half free of I/O, in C: the host tests build it as it is
    ksendpoint.c)

add_library(dsppcm MODULE
    ${SOURCE}
    dsppcm.rc)

set_module_type(dsppcm kernelmodedriver)
target_include_directories(dsppcm PRIVATE
    # the audio interface intcdsp.sys answers and the format rules shared with it
    ${REACTOS_SOURCE_DIR}/drivers/wdm/audio/intcdsp
    # the codec driver's sample-container rule (hdaudio_format.h)
    ${REACTOS_SOURCE_DIR}/drivers/wdm/audio/drivers/hdaudio)
target_link_libraries(dsppcm libcntpr ${PSEH_LIB})
add_importlibs(dsppcm ntoskrnl hal portcls)
add_cd_file(TARGET dsppcm DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(dsppcm dsppcm.inf)
