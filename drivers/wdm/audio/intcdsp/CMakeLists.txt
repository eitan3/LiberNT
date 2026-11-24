#
# PROJECT:     LiberNT Intel Smart Sound DSP driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Intel audio DSP driver build rules
# COPYRIGHT:   Copyright 2026 LiberNT contributors
#

list(APPEND SOURCE
    intcdsp.c
    hwops.c
    fwfile.c)

add_library(intcdsp MODULE ${SOURCE} intcdsp.rc)
set_module_type(intcdsp kmdfdriver)
target_link_libraries(intcdsp libcntpr ${PSEH_LIB} fwload nhlt)
add_importlibs(intcdsp wdfldr ntoskrnl hal)
add_cd_file(TARGET intcdsp DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(intcdsp intcdsp.inf)

# Sound Open Firmware for Arrow Lake-S/HX from sof-firmware 2026.09.2-1 (intel-signed/sof-arl-s.ri, sha256 f37a055cefef26bbe14906ec6cd5fe5532c2a4419de87612d8b45b45dc647c6e) with its licence
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/sof-arl-s.ri DESTINATION reactos/system32/drivers/intel/sof NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/LICENCE.Intel DESTINATION reactos/system32/drivers/intel/sof NO_CAB FOR all)
