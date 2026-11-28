#
# PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Intel BE200 PCIe Native Wi-Fi miniport build rules
# COPYRIGHT:   Copyright 2026 The LiberNT contributors
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1)

list(APPEND SOURCE
    iwlbe.c
    trans.c
    fwparse.c
    ucode.c
    mac.c
    rx.c
    mlme.c
    oid.c
    iwlbe.h
    iwlfw.h
    iwlhw.h)

include_directories(${REACTOS_SOURCE_DIR}/sdk/lib/drivers/rsna_supplicant)

add_library(iwlbe MODULE ${SOURCE} iwlbe.rc)
set_module_type(iwlbe kernelmodedriver)
target_link_libraries(iwlbe rsna_supplicant)
add_importlibs(iwlbe ndis ntoskrnl hal)
add_cd_file(TARGET iwlbe DESTINATION reactos/system32/drivers NO_CAB FOR all)
add_driver_inf(iwlbe netiwlbe.inf)

add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/iwlwifi-gl-c0-fm-c0-c109.ucode DESTINATION reactos/system32/drivers/intel NO_CAB FOR all)
add_cd_file(FILE ${CMAKE_CURRENT_SOURCE_DIR}/fw/LICENCE.iwlwifi_firmware.txt DESTINATION reactos/3rdParty NO_CAB FOR all)
