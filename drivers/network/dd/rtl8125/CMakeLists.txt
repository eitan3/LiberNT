#
# PROJECT:     LiberNT Realtek RTL8125/RTL8126 Ethernet Driver
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build rules for the RTL8125/RTL8126 NDIS 6.20 miniport
# COPYRIGHT:   Copyright 2026 The LiberNT contributors
#

add_definitions(
    -DNDIS620_MINIPORT
    -DNDIS_MINIPORT_DRIVER
    -DNDIS_LEGACY_DRIVER=1
    -DNDIS_LEGACY_MINIPORT=1)

list(APPEND SOURCE
    datapath.c
    hw.c
    init.c
    interrupt.c
    oid.c
    power.c
    rtl8125.h
    rtl8125hw.h)

add_library(rtl8125 MODULE ${SOURCE} rtl8125.rc)
add_pch(rtl8125 rtl8125.h SOURCE)
set_module_type(rtl8125 kernelmodedriver)
add_importlibs(rtl8125 ndis ntoskrnl hal)
add_cd_file(TARGET rtl8125 DESTINATION reactos/system32/drivers FOR all)
add_driver_inf(rtl8125 netrtl8125.inf)
