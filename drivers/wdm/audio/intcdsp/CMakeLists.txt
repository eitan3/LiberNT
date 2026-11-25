#
# PROJECT:     LiberNT Intel Smart Sound DSP driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Intel audio DSP driver build rules
# COPYRIGHT:   Copyright 2026 LiberNT contributors
#

list(APPEND SOURCE
    intcdsp.c
    adspbus.c
    hwops.c
    ipclock.c
    fwfile.c
    topology.c
    # the topology file selection decides through the bus's policy, so the decision compiles into this driver too
    ${REACTOS_SOURCE_DIR}/drivers/wdm/audio/hdaudbus/policy.c)

add_library(intcdsp MODULE ${SOURCE} intcdsp.rc)
set_module_type(intcdsp kmdfdriver)
target_include_directories(intcdsp PRIVATE ${REACTOS_SOURCE_DIR}/drivers/wdm/audio/hdaudbus)
target_link_libraries(intcdsp libcntpr ${PSEH_LIB} fwload sofipc nhlt softplg)
add_importlibs(intcdsp wdfldr ntoskrnl hal)
add_cd_file(TARGET intcdsp DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(intcdsp intcdsp.inf)

# Sound Open Firmware for Arrow Lake-S/HX from sof-firmware 2026.09.2-1 (intel-signed/sof-arl-s.ri, sha256 f37a055cefef26bbe14906ec6cd5fe5532c2a4419de87612d8b45b45dc647c6e) with its licence
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/sof-arl-s.ri DESTINATION reactos/system32/drivers/intel/sof NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/LICENCE.Intel DESTINATION reactos/system32/drivers/intel/sof NO_CAB FOR all)
# The loopback topology the DSP driver runs by the registry override (loopback/build.sh builds it; sha256 0e002de8d92e94f9f961a94a72654750f42d21afbdea09a2f6f6ee7196965601), with the licence and the notice beside it
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/sof-ipc4-loopback.tplg DESTINATION reactos/system32/drivers/intel/sof-tplg NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/LICENCE.Intel DESTINATION reactos/system32/drivers/intel/sof-tplg NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/firmware/loopback/NOTICE DESTINATION reactos/system32/drivers/intel/sof-tplg NO_CAB FOR all)
