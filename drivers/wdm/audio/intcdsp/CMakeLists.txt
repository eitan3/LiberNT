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
