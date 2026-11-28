/*
 * PROJECT:     LiberNT Intel BE200 Native 802.11 Miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Device registers, DMA layouts and firmware command formats
 * COPYRIGHT:   Copyright 2026 The LiberNT contributors
 */

#pragma once

#define IWL_BIT(n)                      (1UL << (n))

/* Control and status registers (BAR0) */
#define CSR_HW_IF_CONFIG                0x000
#define CSR_INT_COALESCING              0x004
#define CSR_INT                         0x008
#define CSR_INT_MASK                    0x00C
#define CSR_FH_INT_STATUS               0x010
#define CSR_GPIO_IN                     0x018
#define CSR_RESET                       0x020
#define CSR_GP_CNTRL                    0x024
#define CSR_HW_REV                      0x028
#define CSR_FUNC_SCRATCH                0x02C
#define CSR_MAC_ADDR0_OTP               0x030
#define CSR_MAC_ADDR1_OTP               0x034
#define CSR_MAC_ADDR0_STRAP             0x038
#define CSR_MAC_ADDR1_STRAP             0x03C
#define CSR_GIO                         0x03C
#define CSR_GP_UCODE                    0x048
#define CSR_GP_DRIVER                   0x050
#define CSR_UCODE_DRV_GP1               0x054
#define CSR_UCODE_DRV_GP1_SET           0x058
#define CSR_UCODE_DRV_GP1_CLR           0x05C
#define CSR_UCODE_DRV_GP2               0x060
#define CSR_MBOX_SET                    0x088
#define CSR_HW_RF_ID                    0x09C
#define CSR_GIO_CHICKEN_BITS            0x100
#define CSR_CTXT_INFO_ADDR              0x118
#define CSR_IML_DATA_ADDR               0x120
#define CSR_IML_SIZE_ADDR               0x128
#define CSR_MAC_SHADOW_REG_CTRL         0x0A8
#define CSR_DBG_HPET_MEM                0x240
#define CSR_DBG_LINK_PWR_MGMT           0x250
#define CSR_CTXT_INFO_BOOT_CTRL         0x000

#define HBUS_TARG_PRPH_WADDR            0x444
#define HBUS_TARG_PRPH_RADDR            0x448
#define HBUS_TARG_PRPH_WDAT             0x44C
#define HBUS_TARG_PRPH_RDAT             0x450
#define HBUS_TARG_WRPTR                 0x460

#define CSR_HW_IF_CONFIG_NIC_READY      IWL_BIT(22)
#define CSR_HW_IF_CONFIG_PREPARE        IWL_BIT(27)
#define CSR_HW_IF_CONFIG_HAP_WAKE       IWL_BIT(19)
#define CSR_AUTO_FUNC_BOOT_ENA          IWL_BIT(1)
#define CSR_MBOX_SET_OS_ALIVE           IWL_BIT(5)
#define CSR_DBG_LINK_PWR_MGMT_DISCON    IWL_BIT(31)
#define CSR_GIO_L0S_DISABLED            IWL_BIT(1)
#define CSR_GIO_CHICKEN_DIS_L0S_TIMER   IWL_BIT(23)
#define CSR_DBG_HPET_MEM_VALUE          0xFFFF0000
#define CSR_FUNC_SCRATCH_INIT           0x01010101
#define CSR_MAC_SHADOW_ALL              0x800FFFFF

#define CSR_GP_CNTRL_MAC_CLOCK_READY    IWL_BIT(0)
#define CSR_GP_CNTRL_INIT_DONE          IWL_BIT(2)
#define CSR_GP_CNTRL_BZ_MAC_INIT        IWL_BIT(6)
#define CSR_GP_CNTRL_BZ_ROM_START       IWL_BIT(7)
#define CSR_GP_CNTRL_MAC_STATUS         IWL_BIT(20)
#define CSR_GP_CNTRL_BZ_MAC_ACCESS_REQ  IWL_BIT(21)
#define CSR_GP_CNTRL_RFKILL             IWL_BIT(27)
#define CSR_GP_CNTRL_BZ_SW_RESET        IWL_BIT(31)

#define CSR_UCODE_SW_BIT_RFKILL         IWL_BIT(1)
#define CSR_UCODE_DRV_GP1_CMD_BLOCKED   IWL_BIT(2)

#define CSR_INT_BIT_FH_RX               IWL_BIT(31)
#define CSR_INT_BIT_HW_ERR              IWL_BIT(29)
#define CSR_INT_BIT_RX_PERIODIC         IWL_BIT(28)
#define CSR_INT_BIT_FH_TX               IWL_BIT(27)
#define CSR_INT_BIT_SW_ERR              IWL_BIT(25)
#define CSR_INT_BIT_RF_KILL             IWL_BIT(7)
#define CSR_INT_BIT_CT_KILL             IWL_BIT(6)
#define CSR_INT_BIT_SW_RX               IWL_BIT(3)
#define CSR_INT_BIT_RESET_DONE          IWL_BIT(2)
#define CSR_INT_BIT_WAKEUP              IWL_BIT(1)
#define CSR_INT_BIT_ALIVE               IWL_BIT(0)
#define CSR_INT_RUNTIME_MASK            (CSR_INT_BIT_FH_RX | CSR_INT_BIT_HW_ERR | CSR_INT_BIT_FH_TX | \
                                         CSR_INT_BIT_SW_ERR | CSR_INT_BIT_RF_KILL | CSR_INT_BIT_SW_RX | \
                                         CSR_INT_BIT_WAKEUP | CSR_INT_BIT_RESET_DONE | CSR_INT_BIT_ALIVE | \
                                         CSR_INT_BIT_RX_PERIODIC | CSR_INT_BIT_CT_KILL)
#define CSR_INT_LOAD_MASK               (CSR_INT_BIT_ALIVE | CSR_INT_BIT_FH_RX)
#define CSR_FH_INT_RX_MASK              (IWL_BIT(30) | IWL_BIT(17) | IWL_BIT(16))
#define IWL_HOST_INT_TIMEOUT_DEF        0x40

#define CSR_HW_REV_TYPE(v)              (((v) & 0xFFF0) >> 4)
#define CSR_HW_REV_STEP(v)              (((v) & 0x000C) >> 2)
#define CSR_HW_RFID_TYPE(v)             (((v) & 0x0FFF000) >> 12)
#define CSR_HW_RFID_STEP(v)             (((v) & 0x0000F00) >> 8)
#define IWL_MAC_TYPE_GL                 0x47
#define IWL_RF_TYPE_FM                  0x112

/* Peripheral (PRPH) registers reached through HBUS_TARG_PRPH_* */
#define IWL_UMAC_PRPH_OFFSET            0x300000
#define UREG_CHICK                      0xA05C00
#define UREG_CHICK_MSI_ENABLE           IWL_BIT(24)
#define UREG_DOORBELL_TO_ISR6           0xA05C04
#define UREG_DOORBELL_TO_ISR6_PNVM      IWL_BIT(20)
#define UMAG_SB_CPU_1_STATUS            0xA038C0
#define UMAG_SB_CPU_2_STATUS            0xA038C4

/* Firmware file */
#define IWL_UCODE_MAGIC                 0x0A4C5749
#define IWL_UCODE_TLV_START             88
#define IWL_TLV_SEC_RT                  19
#define IWL_TLV_FLAGS                   18
#define IWL_TLV_DEF_CALIB               22
#define IWL_TLV_PHY_SKU                 23
#define IWL_TLV_NUM_OF_CPU              27
#define IWL_TLV_API_CHANGES_SET         29
#define IWL_TLV_ENABLED_CAPABILITIES    30
#define IWL_TLV_N_SCAN_CHANNELS         31
#define IWL_TLV_PAGING                  32
#define IWL_TLV_CMD_VERSIONS            48
#define IWL_TLV_IML                     52
#define IWL_TLV_HW_TYPE                 58
#define IWL_TLV_PNVM_VERSION            62
#define IWL_TLV_PNVM_SKU                64
#define IWL_TLV_PNVM_DATA               74
#define IWL_TLV_MEM_DESC                7
#define IWL_SEC_CPU_SEPARATOR           0xFFFFCCCC
#define IWL_SEC_PAGING_SEPARATOR        0xAAAABBBB
#define IWL_PNVM_SKIP_SECTION           0xDDDDEEEE
#define IWL_CAPA_FRAGMENTED_PNVM        32
#define IWL_FW_PHY_CFG_TX_CHAIN_POS     16
#define IWL_FW_PHY_CFG_RX_CHAIN_POS     20
#define IWL_FW_MAX_SECTIONS             64
#define IWL_PNVM_MAX_CHUNKS             64

/* Context information handed to the boot ROM (gen3 layout) */
#define IWL_CI_DRAM_ENTRIES             64
#define IWL_CI_FSEQ_ENTRIES             8
#define IWL_PS_VERSION                  0
#define IWL_PS_CONTROL                  8
#define IWL_PS_PNVM                     16
#define IWL_PS_HWM                      32
#define IWL_PS_RBD                      48
#define IWL_PS_REDUCE_POWER             60
#define IWL_PS_STEP                     76
#define IWL_PS_FSEQ_OVERRIDE            84
#define IWL_PS_STEP_ANALOG              88
#define IWL_PS_DRAM                     124
#define IWL_PS_DRAM_UMAC                (IWL_PS_DRAM)
#define IWL_PS_DRAM_LMAC                (IWL_PS_DRAM + 8 * IWL_CI_DRAM_ENTRIES)
#define IWL_PS_DRAM_VIRTUAL             (IWL_PS_DRAM + 16 * IWL_CI_DRAM_ENTRIES)
#define IWL_PS_SIZE_COMMON              (IWL_PS_DRAM + 24 * IWL_CI_DRAM_ENTRIES)
#define IWL_PS_SIZE                     (IWL_PS_SIZE_COMMON + 8 * IWL_CI_FSEQ_ENTRIES)
#define IWL_PS_RB_SIZE_4K               IWL_BIT(16)
#define IWL_PS_MTR_MODE                 IWL_BIT(17)
#define IWL_PS_MTR_FORMAT_256B          0x000C0000

#define IWL_CI2_VERSION                 0
#define IWL_CI2_CONFIG                  4
#define IWL_CI2_PRPH_INFO               8
#define IWL_CI2_CR_HEAD                 16
#define IWL_CI2_TR_TAIL                 24
#define IWL_CI2_CR_TAIL                 32
#define IWL_CI2_MTR_BASE                52
#define IWL_CI2_MCR_BASE                60
#define IWL_CI2_MTR_SIZE                68
#define IWL_CI2_MCR_SIZE                70
#define IWL_CI2_PRPH_SCRATCH            88
#define IWL_CI2_PRPH_SCRATCH_SIZE       96
#define IWL_CI2_SIZE                    104
#define IWL_PNVM_DESC_SIZE              (8 * IWL_PNVM_MAX_CHUNKS)

/* Transmit frame descriptors */
#define IWL_TFD_SIZE                    256
#define IWL_TFD_MAX_TBS                 25
#define IWL_TFD_TB_SIZE                 10
#define IWL_FIRST_TB_SIZE               20
#define IWL_BC_TABLE_ENTRIES            4096
#define IWL_BC_TABLE_SIZE               (IWL_BC_TABLE_ENTRIES * 2)

/* Receive descriptors */
#define IWL_RX_FREE_BD_SIZE             16
#define IWL_RX_USED_BD_SIZE             4
#define IWL_RX_CD_FRAGMENTED            0x01
#define IWL_RB_SIZE                     4096
#define IWL_RX_FRAME_SIZE_MASK          0x3FFF
#define IWL_RX_FRAME_INVALID            0x55550000
#define IWL_SEQ_RX_FRAME                0x8000
#define IWL_RX_QUEUE_WRPTR(q)           (((ULONG)(q) + 512) << 16)

/* Command groups and identifiers */
#define IWL_GRP_LEGACY                  0x0
#define IWL_GRP_LONG                    0x1
#define IWL_GRP_SYSTEM                  0x2
#define IWL_GRP_MAC_CONF                0x3
#define IWL_GRP_DATA_PATH               0x5
#define IWL_GRP_REGULATORY              0xC

#define IWL_CMD_ALIVE                   0x01
#define IWL_CMD_INIT_COMPLETE           0x04
#define IWL_CMD_PHY_CONTEXT             0x08
#define IWL_CMD_SCAN_COMPLETE_UMAC      0x0F
#define IWL_CMD_TX                      0x1C
#define IWL_CMD_PHY_CONFIGURATION       0x6A
#define IWL_CMD_POWER_TABLE             0x77
#define IWL_CMD_TX_ANT_CONFIGURATION    0x98
#define IWL_CMD_BT_CONFIG               0x9B
#define IWL_CMD_SCAN_ITERATION_COMPLETE 0xB5
#define IWL_CMD_RX_MPDU                 0xC1
#define IWL_CMD_MCC_UPDATE              0xC8
#define IWL_CMD_SCAN_CFG                0x0C
#define IWL_CMD_SCAN_REQ_UMAC           0x0D
#define IWL_CMD_SOC_CONFIGURATION       0x01
#define IWL_CMD_INIT_EXTENDED_CFG       0x03
#define IWL_CMD_SESSION_PROTECTION      0x05
#define IWL_CMD_MAC_CONFIG              0x08
#define IWL_CMD_LINK_CONFIG             0x09
#define IWL_CMD_STA_CONFIG              0x0A
#define IWL_CMD_STA_REMOVE              0x0C
#define IWL_CMD_MISSED_BEACONS          0xF6
#define IWL_CMD_SESSION_PROTECTION_NOTIF 0xFB
#define IWL_CMD_SCD_QUEUE_CONFIG        0x17
#define IWL_CMD_PNVM_INIT_COMPLETE      0xFE

#define IWL_CMD_HEADER_WIDE             8
#define IWL_TX_CMD_HEADER               4
#define IWL_CMD_QUEUE                   0
#define IWL_CMD_QUEUE_SIZE              128
#define IWL_CMD_SEQ(q, i)               ((USHORT)((((q) & 0x1F) << 8) | ((i) & 0xFF)))

#define IWL_ALIVE_STATUS_OK             0xCAFE
#define IWL_ALIVE_SIZE                  152
#define IWL_ALIVE_SKU_ID                108

#define IWL_FW_CTXT_ACTION_ADD          1
#define IWL_FW_CTXT_ACTION_MODIFY       2
#define IWL_FW_CTXT_ACTION_REMOVE       3
#define IWL_FW_CTXT_INVALID             0xFFFFFFFF

/* INIT_EXTENDED_CFG */
#define IWL_INIT_PHY                    IWL_BIT(2)

/* PHY_CONFIGURATION, 28 bytes */
#define IWL_PHY_CFG_CMD_SIZE            28

/* SOC_CONFIGURATION */
#define IWL_SOC_CONFIG_DISCRETE         IWL_BIT(0)
#define IWL_SOC_CONFIG_LOW_LATENCY      IWL_BIT(1)
#define IWL_SOC_LATENCY_US              12000

/* BT_CONFIG */
#define IWL_BT_COEX_NW                  1
#define IWL_BT_COEX_MODULES             (IWL_BIT(0) | IWL_BIT(4))

/* MCC_UPDATE */
#define IWL_MCC_CMD_SIZE                28
#define IWL_MCC_RESP_HEADER             24
#define IWL_MCC_RESP_N_CHANNELS         20
#define IWL_NVM_CHANNEL_VALID           IWL_BIT(0)
#define IWL_NVM_CHANNEL_ACTIVE          IWL_BIT(3)
#define IWL_NVM_CHANNEL_RADAR           IWL_BIT(4)

/* MAC_CONFIG, 56 bytes */
#define IWL_MAC_CMD_SIZE                56
#define IWL_MAC_ID_AND_COLOR            0
#define IWL_MAC_ACTION                  4
#define IWL_MAC_TYPE                    8
#define IWL_MAC_ADDRESS                 12
#define IWL_MAC_FILTER_FLAGS            20
#define IWL_MAC_NIC_NOT_ACK             32
#define IWL_MAC_CLIENT_IS_ASSOC         36
#define IWL_MAC_CLIENT_ASSOC_ID         40
#define IWL_MAC_TYPE_BSS_STA            5
#define IWL_MAC_FILTER_ACCEPT_GRP       IWL_BIT(2)
#define IWL_MAC_FILTER_IN_BEACON        IWL_BIT(3)

/* LINK_CONFIG, 208 bytes */
#define IWL_LINK_CMD_SIZE               208
#define IWL_LINK_ACTION                 0
#define IWL_LINK_ID                     4
#define IWL_LINK_MAC_ID                 8
#define IWL_LINK_PHY_ID                 12
#define IWL_LINK_ADDRESS                16
#define IWL_LINK_MODIFY_MASK            24
#define IWL_LINK_ACTIVE                 28
#define IWL_LINK_CCK_RATES              36
#define IWL_LINK_OFDM_RATES             40
#define IWL_LINK_SHORT_PREAMBLE         44
#define IWL_LINK_SHORT_SLOT             48
#define IWL_LINK_PROTECTION             52
#define IWL_LINK_QOS_FLAGS              56
#define IWL_LINK_AC                     60
#define IWL_LINK_AC_SIZE                8
#define IWL_LINK_BI                     136
#define IWL_LINK_DTIM                   140
#define IWL_LINK_FLAGS                  148
#define IWL_LINK_SPEC_ID                166
#define IWL_LINK_MODIFY_ACTIVE          IWL_BIT(0)
#define IWL_LINK_MODIFY_RATES           IWL_BIT(1)
#define IWL_LINK_MODIFY_PROTECT         IWL_BIT(2)
#define IWL_LINK_MODIFY_QOS             IWL_BIT(3)
#define IWL_LINK_MODIFY_BEACON          IWL_BIT(4)
#define IWL_LINK_FLAG_BSS_COLOR_DIS     IWL_BIT(1)

/* PHY_CONTEXT, 32 bytes */
#define IWL_PHY_CMD_SIZE                32
#define IWL_PHY_ID_AND_COLOR            0
#define IWL_PHY_ACTION                  4
#define IWL_PHY_CHANNEL                 8
#define IWL_PHY_BAND                    12
#define IWL_PHY_WIDTH                   13
#define IWL_PHY_CTRL_POS                14
#define IWL_PHY_SECONDARY_CTRL          28
#define IWL_PHY_CTRL_POS_ABOVE          0x4
#define IWL_PHY_BAND_5                  0
#define IWL_PHY_BAND_24                 1

/* STA_CONFIG, 112 bytes */
#define IWL_STA_CMD_SIZE                112
#define IWL_STA_ID                      0
#define IWL_STA_LINK_MASK               4
#define IWL_STA_PEER_MLD                8
#define IWL_STA_PEER_LINK               16
#define IWL_STA_TYPE                    24
#define IWL_STA_ASSOC_ID                28
#define IWL_STA_MFP                     36
#define IWL_STA_TYPE_PEER               0

/* SESSION_PROTECTION, 24 bytes */
#define IWL_SESSION_CMD_SIZE            24
#define IWL_SESSION_CONF_ASSOC          0

/* SCD_QUEUE_CONFIG, 36 bytes */
#define IWL_SCD_CMD_SIZE                36
#define IWL_SCD_ADD                     0
#define IWL_SCD_REMOVE                  1
#define IWL_SCD_RESP_SIZE               8
#define IWL_MGMT_TID                    15

/* TX command, 28 bytes after the 4-byte header */
#define IWL_TX_CMD_SIZE                 28
#define IWL_TX_FLAGS_CMD_RATE           IWL_BIT(0)
#define IWL_TX_FLAGS_ENCRYPT_DIS        IWL_BIT(1)
#define IWL_TX_FLAGS_HIGH_PRI           IWL_BIT(2)
#define IWL_TX_OFFLOAD_MH_SHIFT         8
#define IWL_TX_OFFLOAD_PAD              IWL_BIT(13)
#define IWL_RATE_MOD_CCK                (0UL << 8)
#define IWL_RATE_MOD_OFDM               (1UL << 8)
#define IWL_RATE_ANT_A                  IWL_BIT(14)
#define IWL_TX_RESP_FRAME_COUNT         0
#define IWL_TX_RESP_QUEUE               36
#define IWL_TX_RESP_STATUS              40
#define IWL_TX_STATUS_SUCCESS           1
#define IWL_TX_STATUS_MASK              0xFF

/* RX MPDU descriptor, 64 bytes */
#define IWL_RX_MPDU_DESC_SIZE           64
#define IWL_RX_MPDU_LEN                 0
#define IWL_RX_MPDU_FLAGS1              2
#define IWL_RX_MPDU_FLAGS2              3
#define IWL_RX_MPDU_PHY_BAND            7
#define IWL_RX_MPDU_STATUS              12
#define IWL_RX_MPDU_ENERGY_A            40
#define IWL_RX_MPDU_ENERGY_B            41
#define IWL_RX_MPDU_CHANNEL             42
#define IWL_RX_MPDU_FLAGS2_PAD          0x20
#define IWL_RX_MPDU_STATUS_CRC_OK       IWL_BIT(0)
#define IWL_RX_MPDU_STATUS_OVERRUN_OK   IWL_BIT(1)

/* SCAN_REQ_UMAC version 18, 1948 bytes */
#define IWL_SCAN_CMD_SIZE               1948
#define IWL_SCAN_UID                    0
#define IWL_SCAN_OOC_PRIORITY           4
#define IWL_SCAN_GEN                    8
#define IWL_SCAN_GEN_FLAGS              (IWL_SCAN_GEN + 0)
#define IWL_SCAN_GEN_START_LINK         (IWL_SCAN_GEN + 3)
#define IWL_SCAN_GEN_ACTIVE_DWELL       (IWL_SCAN_GEN + 4)
#define IWL_SCAN_GEN_ADWELL_2G          (IWL_SCAN_GEN + 6)
#define IWL_SCAN_GEN_ADWELL_5G          (IWL_SCAN_GEN + 7)
#define IWL_SCAN_GEN_ADWELL_SOCIAL      (IWL_SCAN_GEN + 8)
#define IWL_SCAN_GEN_MAX_BUDGET         (IWL_SCAN_GEN + 10)
#define IWL_SCAN_GEN_PRIORITY           (IWL_SCAN_GEN + 28)
#define IWL_SCAN_GEN_PASSIVE_DWELL      (IWL_SCAN_GEN + 32)
#define IWL_SCAN_CHAN                   44
#define IWL_SCAN_CHAN_FLAGS             (IWL_SCAN_CHAN + 0)
#define IWL_SCAN_CHAN_COUNT             (IWL_SCAN_CHAN + 1)
#define IWL_SCAN_CHAN_N_APS             (IWL_SCAN_CHAN + 2)
#define IWL_SCAN_CHAN_CFG               (IWL_SCAN_CHAN + 4)
#define IWL_SCAN_CHAN_CFG_SIZE          8
#define IWL_SCAN_MAX_CHANNELS           68
#define IWL_SCAN_PERIODIC               592
#define IWL_SCAN_PROBE                  604
#define IWL_SCAN_PREQ_MAC_HEADER        (IWL_SCAN_PROBE + 0)
#define IWL_SCAN_PREQ_BAND_DATA         (IWL_SCAN_PROBE + 4)
#define IWL_SCAN_PREQ_COMMON            (IWL_SCAN_PROBE + 16)
#define IWL_SCAN_PREQ_BUF               (IWL_SCAN_PROBE + 20)
#define IWL_SCAN_PREQ_BUF_SIZE          512
#define IWL_SCAN_DIRECT                 (IWL_SCAN_PROBE + 536)
#define IWL_SCAN_DIRECT_SIZE            34
#define IWL_SCAN_GEN_FLAGS_PASS_ALL     IWL_BIT(1)
#define IWL_SCAN_GEN_FLAGS_ALL_RX       IWL_BIT(6)
#define IWL_SCAN_GEN_FLAGS_ADWELL       IWL_BIT(7)
#define IWL_SCAN_GEN_FLAGS_PASSIVE      IWL_BIT(11)
#define IWL_SCAN_CHAN_FLAG_ORDER        IWL_BIT(5)
#define IWL_SCAN_CHAN_BAND_SHIFT        30
#define IWL_SCAN_PRIORITY_EXT_6         6
#define IWL_SCAN_DWELL_ACTIVE           10
#define IWL_SCAN_DWELL_PASSIVE          110
#define IWL_SCAN_COMPLETE_STATUS        6
#define IWL_SCAN_CONFIG_SIZE            12

/* SEC_KEY and power */
#define IWL_POWER_CMD_SIZE              4
