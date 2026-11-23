/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The IPC4 codec reproduces the ABI's message words and never reads past a short register window or extended manifest
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <kmt_test.h>
#include <sofipc4.h>

#define NDEBUG
#include <debug.h>

#include "ipc4_cases.h"

static
VOID
CheckVectors(VOID)
{
    ULONG Index;

    for (Index = 0; Index < ARRAYSIZE(Ipc4VectorCases); Index++)
    {
        const IPC4_VECTOR_CASE* Case = &Ipc4VectorCases[Index];
        SOFIPC4_MSG Msg;

        RtlFillMemory(&Msg, sizeof(Msg), 0xA5);
        Case->Encode(&Msg);
        ok_eq_hex(Msg.Primary, Case->Primary);
        ok_eq_hex(Msg.Extension, Case->Extension);
        ok_eq_uint(SofIpc4MsgType(&Msg), Case->Type);
        ok_eq_uint(SofIpc4MsgTarget(&Msg), Case->Target);
        ok_eq_uint(SofIpc4MsgIsReply(&Msg), SOFIPC4_DIR_MSG_REQUEST);
        ok_eq_uint(Msg.Primary >> 31, 0);
        ok_eq_uint(Msg.Extension >> 30, 0);
    }
}

static
VOID
CheckReplies(VOID)
{
    ULONG Index;
    UINT32 Status;
    SOFIPC4_MSG Reply;

    for (Index = 0; Index < ARRAYSIZE(Ipc4ReplyCases); Index++)
    {
        const IPC4_REPLY_CASE* Case = &Ipc4ReplyCases[Index];

        Reply.Primary = Case->Primary;
        Reply.Extension = Case->Extension;
        ok(SofIpc4ParseReply(&Reply, Case->Type, Case->Target, &Status), "reply case %lu\n", Index);
        ok_eq_uint(Status, Case->Status);
        ok_eq_uint(SofIpc4MsgIsReply(&Reply), SOFIPC4_DIR_MSG_REPLY);
        ok(!SofIpc4ParseReply(&Reply, Case->Type + 1, Case->Target, &Status), "wrong type accepted\n");
        ok(!SofIpc4ParseReply(&Reply, Case->Type, Case->Target ^ 1, &Status), "wrong target accepted\n");
    }

    Reply.Primary = Ipc4ReplyCases[0].Primary;
    Reply.Extension = Ipc4ReplyCases[0].Extension;
    ok_eq_uint(SofIpc4PipelineSetStateReplyFailedId(&Reply), Ipc4ReplyCases[0].ExtensionValue);
    Reply.Primary = Ipc4ReplyCases[1].Primary;
    Reply.Extension = Ipc4ReplyCases[1].Extension;
    ok_eq_uint(SofIpc4PipelineStateReply(&Reply), Ipc4ReplyCases[1].ExtensionValue);
    Reply.Primary = Ipc4ReplyCases[2].Primary;
    Reply.Extension = Ipc4ReplyCases[2].Extension;
    ok_eq_uint(SofIpc4PipelineContextSizeReply(&Reply), Ipc4ReplyCases[2].ExtensionValue);
}

static
VOID
CheckNotifications(VOID)
{
    SOFIPC4_MSG Msg;

    Msg.Primary = SOFIPC4_FW_READY_PRIMARY;
    Msg.Extension = 0;
    ok(SofIpc4IsNotification(&Msg), "FW_READY is a notification\n");
    ok(SofIpc4IsFwReady(&Msg), "FW_READY recognised\n");
    ok_eq_uint(SofIpc4NotificationType(&Msg), SOFIPC4_NOTIFY_FW_READY);

    Msg.Primary |= SOFIPC4_RSP_BIT;
    ok(!SofIpc4IsNotification(&Msg), "a reply is not a notification\n");
    ok(!SofIpc4IsFwReady(&Msg), "a reply is not FW_READY\n");

    Msg.Primary = (SOFIPC4_NOTIFY_EXCEPTION_CAUGHT << SOFIPC4_NOTIFICATION_TYPE_SHIFT) |
        (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT);
    ok(SofIpc4IsNotification(&Msg), "exception is a notification\n");
    ok_eq_uint(SofIpc4NotificationType(&Msg), SOFIPC4_NOTIFY_EXCEPTION_CAUGHT);

    ok_eq_str(SofIpc4NotificationName(SOFIPC4_NOTIFY_FW_READY), "FW_READY");
    ok_eq_str(SofIpc4NotificationName(0xFF), "UNKNOWN");
}

static
VOID
CheckFwRegisters(VOID)
{
    SOFIPC4_FW_REGS Regs;
    SIZE_T Length;

    RtlFillMemory(&Regs, sizeof(Regs), 0x5A);
    ok_eq_int(SofIpc4ParseFwRegisters(Ipc4FwRegisterWindow, sizeof(Ipc4FwRegisterWindow), &Regs), SofIpc4ParseOk);
    ok_eq_hex(Regs.State, 0x0ABCDEF1);
    ok_eq_uint(Regs.Module, 5);
    ok_eq_uint(Regs.Running, 1);
    ok_eq_uint(Regs.LastError, 0x1234);
    ok_eq_uint(Regs.CurrentAState, 9);
    ok_eq_uint(Regs.CachedImrUsage, 1);
    ok_eq_uint(Regs.CurrentFState, 6);
    ok_eq_uint(Regs.WakeTickPeriod, 19);
    ok_eq_uint(Regs.ActivePipelines, 41);
    ok_eq_uint(Regs.LastNativeError, 0x55);
    ok_eq_uint(Regs.Ltr, 0x66);
    ok_eq_uint(Regs.RomFuseValues, 0xA5);
    ok_eq_uint(Regs.RomLoadMethod, 1);
    ok_eq_uint(Regs.RomDownlinkIpcUseDma, 1);
    ok_eq_uint(Regs.RomImplementationRevisionMinor, 3);
    ok_eq_uint(Regs.RomImplementationRevisionMajor, 4);
    ok_eq_uint(Regs.RomImplementationVersionMinor, 5);
    ok_eq_uint(Regs.RomImplementationVersionMajor, 6);
    ok_eq_uint(Regs.RomPlatformType, 0x654);
    ok_eq_uint(Regs.AbiVersion, SOFIPC4_FW_REGS_ABI_VER);
    ok_eq_uint(Regs.SlaveCoreStatus[3], 0x77);
    ok_eq_uint(Regs.SlaveCoreStatus[0], 0);

    for (Length = 0; Length < sizeof(Ipc4FwRegisterWindow); Length++)
    {
        RtlFillMemory(&Regs, sizeof(Regs), 0x5A);
        ok_eq_int(SofIpc4ParseFwRegisters(Ipc4FwRegisterWindow, Length, &Regs),
                  Length == 0 ? SofIpc4ParseNoData : SofIpc4ParseTruncated);
        ok_eq_uint(Regs.AbiVersion, 0);
    }
}

static
VOID
CheckExtManifest(VOID)
{
    UCHAR Buffer[sizeof(Ipc4ExtManifest)];
    SOFIPC4_EXT_MAN ExtMan;
    SIZE_T Length;
    ULONG Index;

    RtlFillMemory(&ExtMan, sizeof(ExtMan), 0x5A);
    ok_eq_int(SofIpc4ParseExtManifest(Ipc4ExtManifest, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseOk);
    ok_eq_hex(ExtMan.HeaderVersion, 0x01000000);
    ok_eq_uint(ExtMan.FullSize, 112);
    ok_eq_uint(ExtMan.ElementCount, 2);
    ok(ExtMan.HasVersion, "version element found\n");
    ok_eq_uint(ExtMan.Major, 2);
    ok_eq_uint(ExtMan.Minor, 12);
    ok_eq_uint(ExtMan.Micro, 3);
    ok_eq_uint(ExtMan.Build, 45);
    ok_eq_str(ExtMan.Date, "Jan 02 2026");
    ok_eq_str(ExtMan.Time, "12:34:56");
    ok_eq_str(ExtMan.Tag, "sof");
    ok_eq_hex(ExtMan.AbiVersion, 0x03000C03);
    ok_eq_hex(ExtMan.SrcHash, 0xDEADBEEF);
    ok_eq_uint(ExtMan.Flags, 3);
    ok(ExtMan.HasIpcMessageSize, "IPC message size found\n");
    ok_eq_uint(ExtMan.IpcMessageSize, 512);

    for (Length = 0; Length < sizeof(Ipc4ExtManifest); Length++)
    {
        RtlZeroMemory(&ExtMan, sizeof(ExtMan));
        ok_eq_int(SofIpc4ParseExtManifest(Ipc4ExtManifest, Length, &ExtMan),
                  Length == 0 ? SofIpc4ParseNoData : (Length < 16 ? SofIpc4ParseTruncated : SofIpc4ParseBadLength));
        ok(!ExtMan.HasVersion && !ExtMan.HasIpcMessageSize, "nothing taken from a short manifest\n");
    }

    for (Index = 0; Index < sizeof(Ipc4ExtManifest); Index++)
    {
        RtlCopyMemory(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
        Buffer[Index] ^= 0x5A;
        RtlZeroMemory(&ExtMan, sizeof(ExtMan));
        ok(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan) <= SofIpc4ParseTruncated,
           "one flipped byte stays within the status codes\n");
    }
}

static
VOID
CheckNames(VOID)
{
    ok_eq_str(SofIpc4StatusName(SofIpc4Success), "success");
    ok_eq_str(SofIpc4StatusName(SofIpc4InvalidChainStateTransition), "invalid chain DMA state transition");
    ok_eq_str(SofIpc4StatusName(8), "UNKNOWN");
    ok_eq_str(SofIpc4GlobalTypeName(SofIpc4GlbCreatePipeline), "CREATE_PIPELINE");
    ok_eq_str(SofIpc4GlobalTypeName(3), "UNKNOWN");
    ok_eq_str(SofIpc4GlobalTypeName(32), "UNKNOWN");
    ok_eq_str(SofIpc4ModuleName(SofIpc4ModBind), "MOD_BIND");
    ok_eq_str(SofIpc4ModuleName(12), "UNKNOWN");
    ok_eq_str(SofIpc4ParseStatusName(SofIpc4ParseOk), "ok");
    ok_eq_str(SofIpc4ParseStatusName(SofIpc4ParseTruncated), "truncated");
    ok_eq_str(SofIpc4ParseStatusName((SOFIPC4_PARSE_STATUS)9), "unknown");
    ok_eq_hex(SOFIPC4_FW_READY_PRIMARY,
        (SOFIPC4_NOTIFY_FW_READY << SOFIPC4_NOTIFICATION_TYPE_SHIFT) | (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT));
}

START_TEST(SofIpc4)
{
    CheckVectors();
    CheckReplies();
    CheckNotifications();
    CheckFwRegisters();
    CheckExtManifest();
    CheckNames();
}
