/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The IPC4 codec reproduces the ABI's message words and never reads past a short register window or extended manifest
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdio.h>
#include <string.h>

#include "shim.h"

#include <sofipc4.h>

#include "ipc4_cases.h"

static int Checks;
static int Failures;

#define CHECK(cond) \
    do { \
        ++Checks; \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond); \
            ++Failures; \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
        ++Checks; \
        if (_a != _b) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: %llx != %llx\n", __FILE__, __LINE__, #a, #b, _a, _b); \
            ++Failures; \
        } \
    } while (0)

#define CHECK_STR(a, b) \
    do { \
        ++Checks; \
        if (strcmp((a), (b)) != 0) { \
            fprintf(stderr, "%s:%d: FAIL: %s == %s: \"%s\" != \"%s\"\n", __FILE__, __LINE__, #a, #b, (a), (b)); \
            ++Failures; \
        } \
    } while (0)

static
void
check_vectors(void)
{
    unsigned int Index;

    for (Index = 0; Index < sizeof(Ipc4VectorCases) / sizeof(Ipc4VectorCases[0]); Index++)
    {
        const IPC4_VECTOR_CASE* Case = &Ipc4VectorCases[Index];
        SOFIPC4_MSG Msg;

        memset(&Msg, 0xA5, sizeof(Msg));
        Case->Encode(&Msg);
        if (Msg.Primary != Case->Primary || Msg.Extension != Case->Extension)
            fprintf(stderr, "  in vector case \"%s\"\n", Case->Name);
        CHECK_EQ(Msg.Primary, Case->Primary);
        CHECK_EQ(Msg.Extension, Case->Extension);
        CHECK_EQ(SofIpc4MsgType(&Msg), Case->Type);
        CHECK_EQ(SofIpc4MsgTarget(&Msg), Case->Target);
        CHECK_EQ(SofIpc4MsgIsReply(&Msg), SOFIPC4_DIR_MSG_REQUEST);
        CHECK_EQ(Msg.Primary >> 31, 0);
        CHECK_EQ(Msg.Extension >> 30, 0);
        CHECK_EQ(Msg.Extension & ~SOFIPC4_EXTENSION_DATA_MASK, 0);
        CHECK(!SofIpc4IsNotification(&Msg) || Case->Type == SofIpc4GlbNotification);
    }
}

static
void
check_replies(void)
{
    unsigned int Index;
    UINT32 Status;

    for (Index = 0; Index < sizeof(Ipc4ReplyCases) / sizeof(Ipc4ReplyCases[0]); Index++)
    {
        const IPC4_REPLY_CASE* Case = &Ipc4ReplyCases[Index];
        SOFIPC4_MSG Reply;

        Reply.Primary = Case->Primary;
        Reply.Extension = Case->Extension;
        if (!SofIpc4ParseReply(&Reply, Case->Type, Case->Target, &Status))
            fprintf(stderr, "  in reply case \"%s\"\n", Case->Name);
        CHECK(SofIpc4ParseReply(&Reply, Case->Type, Case->Target, &Status));
        CHECK_EQ(Status, Case->Status);
        CHECK_EQ(SofIpc4MsgIsReply(&Reply), SOFIPC4_DIR_MSG_REPLY);
        CHECK(!SofIpc4ParseReply(&Reply, Case->Type + 1, Case->Target, &Status));
        CHECK(!SofIpc4ParseReply(&Reply, Case->Type, Case->Target ^ 1, &Status));
    }

    {
        SOFIPC4_MSG Reply;

        Reply.Primary = Ipc4ReplyCases[0].Primary & ~SOFIPC4_RSP_BIT;
        Reply.Extension = 0;
        CHECK(!SofIpc4ParseReply(&Reply, SofIpc4GlbSetPipelineState, SOFIPC4_TARGET_FW_GEN_MSG, &Status));

        Reply.Primary = Ipc4ReplyCases[0].Primary;
        Reply.Extension = Ipc4ReplyCases[0].Extension;
        CHECK_EQ(SofIpc4PipelineSetStateReplyFailedId(&Reply), Ipc4ReplyCases[0].ExtensionValue);

        Reply.Primary = Ipc4ReplyCases[1].Primary;
        Reply.Extension = Ipc4ReplyCases[1].Extension;
        CHECK_EQ(SofIpc4PipelineStateReply(&Reply), Ipc4ReplyCases[1].ExtensionValue);

        Reply.Primary = Ipc4ReplyCases[2].Primary;
        Reply.Extension = Ipc4ReplyCases[2].Extension;
        CHECK_EQ(SofIpc4PipelineContextSizeReply(&Reply), Ipc4ReplyCases[2].ExtensionValue);

        Reply.Extension = 0xFFFFFFFF;
        CHECK_EQ(SofIpc4PipelineStateReply(&Reply), 0x1F);
        CHECK_EQ(SofIpc4PipelineContextSizeReply(&Reply), 0xFFFF);
        CHECK_EQ(SofIpc4PipelineSetStateReplyFailedId(&Reply), 0x3FFFFFFF);
    }
}

static
void
check_notifications(void)
{
    SOFIPC4_MSG Msg;

    Msg.Primary = SOFIPC4_FW_READY_PRIMARY;
    Msg.Extension = 0;
    CHECK(SofIpc4IsNotification(&Msg));
    CHECK(SofIpc4IsFwReady(&Msg));
    CHECK_EQ(SofIpc4NotificationType(&Msg), SOFIPC4_NOTIFY_FW_READY);

    Msg.Primary |= SOFIPC4_RSP_BIT;
    CHECK(!SofIpc4IsNotification(&Msg));
    CHECK(!SofIpc4IsFwReady(&Msg));

    Msg.Primary = (SOFIPC4_NOTIFY_EXCEPTION_CAUGHT << SOFIPC4_NOTIFICATION_TYPE_SHIFT) |
        (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT);
    CHECK(SofIpc4IsNotification(&Msg));
    CHECK(!SofIpc4IsFwReady(&Msg));
    CHECK_EQ(SofIpc4NotificationType(&Msg), SOFIPC4_NOTIFY_EXCEPTION_CAUGHT);
    CHECK_EQ(SofIpc4MsgTarget(&Msg), SOFIPC4_TARGET_FW_GEN_MSG);

    Msg.Primary |= SOFIPC4_MSG_TGT_BIT;
    CHECK(SofIpc4IsNotification(&Msg));
    CHECK_EQ(SofIpc4MsgTarget(&Msg), SOFIPC4_TARGET_MODULE_MSG);

    CHECK_STR(SofIpc4NotificationName(SOFIPC4_NOTIFY_FW_READY), "FW_READY");
    CHECK_STR(SofIpc4NotificationName(SOFIPC4_NOTIFY_WATCHDOG_TIMEOUT), "WATCHDOG_TIMEOUT");
    CHECK_STR(SofIpc4NotificationName(0), "UNKNOWN");
    CHECK_STR(SofIpc4NotificationName(0xFF), "UNKNOWN");
}

static
void
check_fw_registers(void)
{
    SOFIPC4_FW_REGS Regs;
    unsigned char Full[2656];

    memset(&Regs, 0x5A, sizeof(Regs));
    CHECK_EQ(SofIpc4ParseFwRegisters(Ipc4FwRegisterWindow, sizeof(Ipc4FwRegisterWindow), &Regs), SofIpc4ParseOk);
    CHECK_EQ(Regs.State, 0x0ABCDEF1);
    CHECK_EQ(Regs.Module, 5);
    CHECK_EQ(Regs.Running, 1);
    CHECK_EQ(Regs.LastError, 0x1234);
    CHECK_EQ(Regs.CurrentAState, 9);
    CHECK_EQ(Regs.CachedImrUsage, 1);
    CHECK_EQ(Regs.CurrentFState, 6);
    CHECK_EQ(Regs.WakeTickPeriod, 19);
    CHECK_EQ(Regs.ActivePipelines, 41);
    CHECK_EQ(Regs.LastNativeError, 0x55);
    CHECK_EQ(Regs.Ltr, 0x66);
    CHECK_EQ(Regs.RomFuseValues, 0xA5);
    CHECK_EQ(Regs.RomLoadMethod, 1);
    CHECK_EQ(Regs.RomDownlinkIpcUseDma, 1);
    CHECK_EQ(Regs.RomImplementationRevisionMinor, 3);
    CHECK_EQ(Regs.RomImplementationRevisionMajor, 4);
    CHECK_EQ(Regs.RomImplementationVersionMinor, 5);
    CHECK_EQ(Regs.RomImplementationVersionMajor, 6);
    CHECK_EQ(Regs.RomPlatformType, 0x654);
    CHECK_EQ(Regs.AbiVersion, SOFIPC4_FW_REGS_ABI_VER);
    CHECK_EQ(Regs.SlaveCoreStatus[3], 0x77);
    CHECK_EQ(Regs.SlaveCoreStatus[0], 0);
    CHECK_EQ(Regs.SlaveCoreStatus[7], 0);

    memset(Full, 0, sizeof(Full));
    memcpy(Full, Ipc4FwRegisterWindow, sizeof(Ipc4FwRegisterWindow));
    Full[sizeof(Full) - 1] = 0xEE;
    CHECK_EQ(SofIpc4ParseFwRegisters(Full, sizeof(Full), &Regs), SofIpc4ParseOk);
    CHECK_EQ(Regs.AbiVersion, SOFIPC4_FW_REGS_ABI_VER);
}

static
void
check_ext_manifest(void)
{
    SOFIPC4_EXT_MAN ExtMan;

    memset(&ExtMan, 0x5A, sizeof(ExtMan));
    CHECK_EQ(SofIpc4ParseExtManifest(Ipc4ExtManifest, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseOk);
    CHECK_EQ(ExtMan.HeaderVersion, 0x01000000);
    CHECK_EQ(ExtMan.FullSize, 112);
    CHECK_EQ(ExtMan.ElementCount, 2);
    CHECK(ExtMan.HasVersion);
    CHECK_EQ(ExtMan.Major, 2);
    CHECK_EQ(ExtMan.Minor, 12);
    CHECK_EQ(ExtMan.Micro, 3);
    CHECK_EQ(ExtMan.Build, 45);
    CHECK_STR(ExtMan.Date, "Jan 02 2026");
    CHECK_STR(ExtMan.Time, "12:34:56");
    CHECK_STR(ExtMan.Tag, "sof");
    CHECK_EQ(ExtMan.AbiVersion, 0x03000C03);
    CHECK_EQ(ExtMan.SrcHash, 0xDEADBEEF);
    CHECK_EQ(ExtMan.Flags, 3);
    CHECK(ExtMan.HasIpcMessageSize);
    CHECK_EQ(ExtMan.IpcMessageSize, 512);
}

static
UINT32
RandomStep(
    UINT32* State)
{
    UINT32 Value = *State;

    Value ^= Value << 13;
    Value ^= Value >> 17;
    Value ^= Value << 5;
    *State = Value;
    return Value;
}

static
int
IsParseStatus(
    int Status)
{
    return Status >= SofIpc4ParseOk && Status <= SofIpc4ParseTruncated;
}

static
void
check_short_and_garbage_inputs(void)
{
    static const UINT32 Seeds[] = { 0x12345678, 0x9E3779B9, 0xDEADBEEF, 0xCAFEBABE };
    unsigned char Buffer[320];
    SOFIPC4_EXT_MAN ExtMan;
    SOFIPC4_FW_REGS Regs;
    SOFIPC4_MSG Msg;
    SIZE_T Length;
    unsigned int Seed, Index, Iteration;
    int Status;

    for (Length = 0; Length < sizeof(Ipc4FwRegisterWindow); Length++)
    {
        Status = SofIpc4ParseFwRegisters(Ipc4FwRegisterWindow, Length, &Regs);
        CHECK(Status == (Length == 0 ? SofIpc4ParseNoData : SofIpc4ParseTruncated));
        CHECK_EQ(Regs.AbiVersion, 0);
    }

    for (Length = 0; Length < sizeof(Ipc4ExtManifest); Length++)
    {
        Status = SofIpc4ParseExtManifest(Ipc4ExtManifest, Length, &ExtMan);
        CHECK(Status == (Length == 0 ? SofIpc4ParseNoData : (Length < 16 ? SofIpc4ParseTruncated : SofIpc4ParseBadLength)));
        CHECK(!ExtMan.HasVersion && !ExtMan.HasIpcMessageSize);
    }

    for (Index = 0; Index < sizeof(Ipc4ExtManifest); Index++)
    {
        memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
        Buffer[Index] ^= 0x5A;
        Status = SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan);
        CHECK(IsParseStatus(Status));
    }

    memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
    Buffer[20] = 0x00;
    CHECK_EQ(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseTruncated);
    CHECK(!ExtMan.HasVersion);
    CHECK_EQ(ExtMan.ElementCount, 0);
    memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
    Buffer[20] = 0x06;
    CHECK_EQ(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseTruncated);
    CHECK(!ExtMan.HasVersion);
    CHECK_EQ(ExtMan.ElementCount, 0);
    memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
    Buffer[93] = 0x40;
    CHECK_EQ(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseTruncated);
    CHECK(ExtMan.HasVersion && ExtMan.Major == 2);
    CHECK(!ExtMan.HasIpcMessageSize);
    CHECK_EQ(ExtMan.ElementCount, 1);

    memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
    Buffer[9] = 0x08;
    CHECK_EQ(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseBadLength);
    memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
    Buffer[9] = 0x71;
    CHECK_EQ(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseBadLength);
    memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
    Buffer[4] = 0x71;
    CHECK_EQ(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseBadLength);

    memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
    Buffer[16] = 0x7F;
    CHECK_EQ(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseOk);
    CHECK(!ExtMan.HasVersion);
    CHECK(ExtMan.HasIpcMessageSize);
    CHECK_EQ(ExtMan.ElementCount, 2);
    memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
    Buffer[88] = 0x7F;
    CHECK_EQ(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseOk);
    CHECK(ExtMan.HasVersion);
    CHECK(!ExtMan.HasIpcMessageSize);
    CHECK_EQ(ExtMan.ElementCount, 2);

    memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest));
    Buffer[45] = 0;
    Buffer[60] = 0;
    CHECK_EQ(SofIpc4ParseExtManifest(Buffer, sizeof(Ipc4ExtManifest), &ExtMan), SofIpc4ParseOk);
    CHECK_STR(ExtMan.Tag, "so");
    CHECK_STR(ExtMan.Date, "Jan 02 20");
    CHECK_STR(ExtMan.Time, "12:34:56");

    for (Seed = 0; Seed < sizeof(Seeds) / sizeof(Seeds[0]); Seed++)
    {
        UINT32 State = Seeds[Seed];

        for (Iteration = 0; Iteration < 5000; Iteration++)
        {
            Length = RandomStep(&State) % (sizeof(Buffer) + 1);
            for (Index = 0; Index < Length; Index++)
                Buffer[Index] = (unsigned char)RandomStep(&State);

            Status = SofIpc4ParseExtManifest(Buffer, Length, &ExtMan);
            CHECK(IsParseStatus(Status));
            Status = SofIpc4ParseFwRegisters(Buffer, Length, &Regs);
            CHECK(IsParseStatus(Status));

            if ((Iteration & 15) == 0)
                memcpy(Buffer, Ipc4ExtManifest, sizeof(Ipc4ExtManifest) < Length ? sizeof(Ipc4ExtManifest) : Length);
            if ((Iteration & 31) == 1 && Length > 0)
                Buffer[RandomStep(&State) % Length] ^= (unsigned char)RandomStep(&State);
            Status = SofIpc4ParseExtManifest(Buffer, Length, &ExtMan);
            CHECK(IsParseStatus(Status));
        }

        State = Seeds[Seed] ^ 0x55555555;
        for (Iteration = 0; Iteration < 5000; Iteration++)
        {
            UINT32 Values[8];

            for (Index = 0; Index < 8; Index++)
                Values[Index] = RandomStep(&State);

            SofIpc4PipelineCreate(&Msg, Values[0], Values[1], Values[2], Values[3], Values[4], Values[5]);
            CHECK_EQ(Msg.Primary >> 31, 0);
            CHECK_EQ(SofIpc4MsgType(&Msg), SofIpc4GlbCreatePipeline);
            CHECK_EQ(Msg.Extension >> 30, 0);
            CHECK_EQ(Msg.Extension & ~(SOFIPC4_PIPELINE_CREATE_LP | 0xFFFF0 | 0xF00000 | SOFIPC4_PIPELINE_CREATE_HAS_PAYLOAD), 0);

            SofIpc4ModuleBind(&Msg, Values[0], Values[1], Values[2], Values[3], Values[4], Values[5], Values[6] & 1);
            CHECK_EQ(Msg.Primary >> 31, 0);
            CHECK_EQ(SofIpc4MsgTarget(&Msg), SOFIPC4_TARGET_MODULE_MSG);
            CHECK_EQ(Msg.Extension >> 30, 0);
            CHECK_EQ(SofIpc4MsgType(&Msg), (Values[6] & 1) ? SofIpc4ModUnbind : SofIpc4ModBind);

            SofIpc4ModuleLargeConfig(&Msg, Values[0], Values[1], Values[2], Values[3], Values[4], Values[5] & 1);
            CHECK_EQ(Msg.Extension >> 30, 0);
            CHECK_EQ(Msg.Primary >> 31, 0);

            SofIpc4ChainDma(&Msg, Values[0], Values[1], Values[2], Values[3]);
            CHECK_EQ(Msg.Extension >> 24, 0);
            CHECK_EQ((Msg.Primary >> 29) & 7, 0);

            Msg.Primary = Values[0];
            Msg.Extension = Values[1];
            CHECK_EQ(SofIpc4MsgType(&Msg), (Values[0] >> 24) & 0x1F);
            CHECK_EQ(SofIpc4IsFwReady(&Msg), Values[0] == SOFIPC4_FW_READY_PRIMARY);
            CHECK(SofIpc4NotificationName(SofIpc4NotificationType(&Msg)) != NULL);
            CHECK(!SofIpc4ParseReply(&Msg, (Values[0] >> 24) & 0x1F, (Values[0] >> 30) & 1, &Values[7]) || (Values[0] & SOFIPC4_RSP_BIT));
        }
    }

    CHECK_EQ(SofIpc4ParseExtManifest(NULL, 0, &ExtMan), SofIpc4ParseNoData);
    CHECK_EQ(SofIpc4ParseFwRegisters(NULL, 16, &Regs), SofIpc4ParseNoData);
    CHECK_EQ(SofIpc4ParseFwRegisters(Ipc4FwRegisterWindow, 0, &Regs), SofIpc4ParseNoData);
}

static
void
check_names(void)
{
    CHECK_STR(SofIpc4StatusName(SofIpc4Success), "success");
    CHECK_STR(SofIpc4StatusName(SofIpc4InvalidChainStateTransition), "invalid chain DMA state transition");
    CHECK_STR(SofIpc4StatusName(8), "UNKNOWN");
    CHECK_STR(SofIpc4StatusName(0xFFFFFF), "UNKNOWN");
    CHECK_STR(SofIpc4GlobalTypeName(SofIpc4GlbCreatePipeline), "CREATE_PIPELINE");
    CHECK_STR(SofIpc4GlobalTypeName(SofIpc4GlbNotification), "NOTIFICATION");
    CHECK_STR(SofIpc4GlobalTypeName(3), "UNKNOWN");
    CHECK_STR(SofIpc4GlobalTypeName(32), "UNKNOWN");
    CHECK_STR(SofIpc4ModuleName(SofIpc4ModBind), "MOD_BIND");
    CHECK_STR(SofIpc4ModuleName(12), "UNKNOWN");
    CHECK_STR(SofIpc4ParseStatusName(SofIpc4ParseOk), "ok");
    CHECK_STR(SofIpc4ParseStatusName(SofIpc4ParseTruncated), "truncated");
    CHECK_STR(SofIpc4ParseStatusName((SOFIPC4_PARSE_STATUS)9), "unknown");
    CHECK_EQ(SOFIPC4_FW_READY_PRIMARY,
        (SOFIPC4_NOTIFY_FW_READY << SOFIPC4_NOTIFICATION_TYPE_SHIFT) | (SofIpc4GlbNotification << SOFIPC4_TYPE_SHIFT));
}

int main(void)
{
    check_vectors();
    check_replies();
    check_notifications();
    check_fw_registers();
    check_ext_manifest();
    check_short_and_garbage_inputs();
    check_names();

    printf("sofipc4: %d checks, %d failures\n", Checks, Failures);
    return Failures ? 1 : 0;
}
