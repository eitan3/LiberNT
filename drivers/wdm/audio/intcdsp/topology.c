/*
 * PROJECT:     LiberNT Intel Smart Sound DSP driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Selects the topology file after a boot (the registry override, the DMI quirk table, the controller's _DSD names), builds its model for the device and says what it holds; with no file, or one the firmware cannot run, the DSP is powered down and no endpoint is created
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "intcdsp.h"
#include <ntstrsafe.h>
#include <softplg.h>
#include <ipc4model.h>
#include <policy.h>

#define INTCDSP_TOPOLOGY_PATH       L"\\SystemRoot\\System32\\drivers\\intel\\sof-tplg\\"
#define INTCDSP_TOPOLOGY_PATH_CHARS (sizeof(INTCDSP_TOPOLOGY_PATH) / sizeof(WCHAR))
#define INTCDSP_TOPOLOGY_MAX_BYTES  (4 * 1024 * 1024)
#define INTCDSP_TOPOLOGY_LINE_BYTES 480
#define INTCDSP_TOPOLOGY_VALUES_MAX 8

/* The module id of a UUID: its index in the firmware image's module table, the number every message to the module's instances carries */
static
UINT32
IntcDspModuleLookup(
    PVOID Context,
    const UINT8* Uuid)
{
    PINTCDSP_DEVICE Dev = Context;
    const SOF_RI_IMAGE* Ri = &Dev->Image.Ri;
    UINT32 Module;

    for (Module = 0; Module < Ri->ModuleCount; Module++)
        if (RtlCompareMemory(Ri->Modules[Module].Uuid, Uuid, SOFTPLG_UUID_BYTES) == SOFTPLG_UUID_BYTES)
            return Module;
    return SOFTPLG_MODULE_NONE;
}

/* One REG_SZ of the driver's Settings key, narrowed to ASCII; the value is left empty when it cannot be read */
static
NTSTATUS
IntcDspReadSettingString(
    _In_ PCWSTR Name,
    _Out_writes_bytes_(Bytes) PCHAR Buffer,
    SIZE_T Bytes)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Path, ValueName;
    UCHAR InfoBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + DSP_TOPOLOGY_NAME_BYTES * sizeof(WCHAR)];
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    HANDLE Key;
    ULONG Result, Index;
    WCHAR Wide;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, Bytes);
    RtlInitUnicodeString(&Path, INTCDSP_SETTINGS_PATH);
    RtlInitUnicodeString(&ValueName, Name);
    InitializeObjectAttributes(&Attributes, &Path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, InfoBuffer, sizeof(InfoBuffer), &Result);
    ZwClose(Key);
    if (!NT_SUCCESS(Status))
        return Status;
    Info = (PKEY_VALUE_PARTIAL_INFORMATION)InfoBuffer;
    if (Info->Type != REG_SZ)
        return STATUS_OBJECT_TYPE_MISMATCH;
    for (Index = 0; Index + 1 < Bytes; Index++)
    {
        if ((Index + 1) * sizeof(WCHAR) > Info->DataLength)
            break;
        RtlCopyMemory(&Wide, Info->Data + Index * sizeof(WCHAR), sizeof(Wide));
        if (Wide == 0)
            break;
        /* a name with a character outside ASCII is not a file name this driver opens */
        if (Wide > 0x7F)
            return STATUS_ILLEGAL_CHARACTER;
        Buffer[Index] = (CHAR)Wide;
    }
    Buffer[Index] = 0;
    return STATUS_SUCCESS;
}

/* The distinct values of one kind among a PCM's host-side formats, ascending */
typedef struct _TOPOLOGY_VALUES
{
    UINT32 Count;
    UINT32 Items[INTCDSP_TOPOLOGY_VALUES_MAX];
} TOPOLOGY_VALUES;

static
VOID
IntcDspTopologyValue(
    _Inout_ TOPOLOGY_VALUES* Values,
    UINT32 Value)
{
    UINT32 Index;

    for (Index = 0; Index < Values->Count && Values->Items[Index] != Value; Index++)
        ;
    if (Index < Values->Count || Values->Count == INTCDSP_TOPOLOGY_VALUES_MAX)
        return;
    for (Index = Values->Count; Index > 0 && Values->Items[Index - 1] > Value; Index--)
        Values->Items[Index] = Values->Items[Index - 1];
    Values->Items[Index] = Value;
    Values->Count++;
}

/* Appends a formatted piece to a line; once a piece does not fit, the line ends and the pieces after it are dropped */
static
VOID
IntcDspTopologyAppend(
    _Inout_updates_bytes_(Bytes) PCHAR Line,
    SIZE_T Bytes,
    _Inout_ PSIZE_T Used,
    _In_z_ _Printf_format_string_ const char* Format,
    ...)
{
    va_list Arguments;
    size_t Written;
    NTSTATUS Status;

    if (*Used >= Bytes)
        return;
    va_start(Arguments, Format);
    Status = RtlStringCbVPrintfA(Line + *Used, Bytes - *Used, Format, Arguments);
    va_end(Arguments);
    if (Status != STATUS_SUCCESS)
    {
        Line[*Used] = 0;
        *Used = Bytes;
        return;
    }
    Written = 0;
    if (RtlStringCbLengthA(Line + *Used, Bytes - *Used, &Written) != STATUS_SUCCESS)
    {
        Line[*Used] = 0;
        *Used = Bytes;
        return;
    }
    *Used += Written;
}

/* The values of one kind as "a/b/c", then the suffix */
static
VOID
IntcDspTopologyValues(
    _Inout_updates_bytes_(Bytes) PCHAR Line,
    SIZE_T Bytes,
    _Inout_ PSIZE_T Used,
    _In_ const TOPOLOGY_VALUES* Values,
    _In_z_ const char* Suffix)
{
    UINT32 Index;

    for (Index = 0; Index < Values->Count; Index++)
        IntcDspTopologyAppend(Line, Bytes, Used, "%s%u", Index ? "/" : "", Values->Items[Index]);
    IntcDspTopologyAppend(Line, Bytes, Used, "%s", Suffix);
}

static
const char*
IntcDspPlural(
    UINT32 Count)
{
    return Count == 1 ? "" : "s";
}

/*
 * The model in the log: the modules it instantiates, by the firmware's own
 * name and id, and one entry per PCM with its directions and the rates,
 * channel counts and sample depths its host copiers offer. One PCM shares
 * the line with the modules; several get a line each, as the debug print
 * cuts a long line.
 */
static
VOID
IntcDspTopologyLog(
    _In_ PINTCDSP_DEVICE Dev)
{
    const SOFTPLG_IPC4_MODEL* Model = Dev->TopologyModel;
    const SOFTPLG_IPC4_WIDGET* Widget;
    const SOFTPLG_IPC4_PCM* Pcm;
    CHAR Line[INTCDSP_TOPOLOGY_LINE_BYTES];
    CHAR Lower[SOFTPLG_TEXT_BYTES];
    /* one bit per module id the firmware image has (fewer than 256) */
    UINT32 Seen[8];
    SIZE_T Used = 0, N;
    UINT32 W, P, F, Module, First, Count, Side, Modules = 0;
    TOPOLOGY_VALUES Rates, Channels, Depths;

    /* the arrow of the acceptance line, in UTF-8 as the serial log carries it */
    static const char Arrow[] = " \xe2\x86\x92 ";

    RtlZeroMemory(Line, sizeof(Line));
    RtlZeroMemory(Seen, sizeof(Seen));
    IntcDspTopologyAppend(Line, sizeof(Line), &Used, "topology: modules");
    for (W = 0; W < Model->WidgetCount; W++)
    {
        Widget = &Model->Widgets[W];
        Module = Widget->ModuleId;
        if (Module == SOFTPLG_MODULE_NONE || Module >= Dev->Image.Ri.ModuleCount || Module >= 8 * 32)
            continue;
        if (Seen[Module / 32] & (1u << (Module % 32)))
            continue;
        Seen[Module / 32] |= 1u << (Module % 32);
        /* the firmware names its modules in capitals; the line says them in lower case */
        for (N = 0; N + 1 < sizeof(Lower) && Dev->Image.Ri.Modules[Module].Name[N] != 0; N++)
        {
            CHAR C = Dev->Image.Ri.Modules[Module].Name[N];
            Lower[N] = C >= 'A' && C <= 'Z' ? C + ('a' - 'A') : C;
        }
        Lower[N] = 0;
        IntcDspTopologyAppend(Line, sizeof(Line), &Used, "%s%s%sid %u", Modules++ ? ", " : " ", Lower, Arrow, Module);
    }
    if (Model->PcmCount != 1)
    {
        IntcDspLog("%s\n", Line);
        Used = 0;
        Line[0] = 0;
    }
    for (P = 0; P < Model->PcmCount; P++)
    {
        Pcm = &Model->Pcms[P];
        Rates.Count = Channels.Count = Depths.Count = 0;
        for (Side = 0; Side < 2; Side++)
        {
            /* the formats a host copier offers the host: the inputs of a playback copier, the outputs of a capture copier */
            if ((Side == 0 ? Pcm->PlaybackCopier : Pcm->CaptureCopier) == SOFTPLG_NO_WIDGET)
                continue;
            Widget = &Model->Widgets[Side == 0 ? Pcm->PlaybackCopier : Pcm->CaptureCopier];
            First = Widget->FirstFormat + (Side == 0 ? 0 : Widget->InputFormats);
            Count = Side == 0 ? Widget->InputFormats : Widget->OutputFormats;
            for (F = 0; F < Count; F++)
            {
                if (!Model->Formats[First + F].Expressible)
                    continue;
                IntcDspTopologyValue(&Rates, Model->Formats[First + F].Words[0]);
                IntcDspTopologyValue(&Channels, SOFTPLG_FORMAT_CHANNELS_OF(Model->Formats[First + F].Words[5]));
                IntcDspTopologyValue(&Depths, SOFTPLG_FORMAT_VALID_OF(Model->Formats[First + F].Words[5]));
            }
        }
        if (Model->PcmCount == 1)
            IntcDspTopologyAppend(Line, sizeof(Line), &Used, "; endpoint '%s' ", Pcm->Name);
        else
            IntcDspTopologyAppend(Line, sizeof(Line), &Used, "topology: endpoint '%s' ", Pcm->Name);
        if (Pcm->PlaybackCopier != SOFTPLG_NO_WIDGET && Pcm->CaptureCopier != SOFTPLG_NO_WIDGET)
            IntcDspTopologyAppend(Line, sizeof(Line), &Used, "render+capture ");
        else if (Pcm->PlaybackCopier != SOFTPLG_NO_WIDGET)
            IntcDspTopologyAppend(Line, sizeof(Line), &Used, "render ");
        else if (Pcm->CaptureCopier != SOFTPLG_NO_WIDGET)
            IntcDspTopologyAppend(Line, sizeof(Line), &Used, "capture ");
        else
        {
            IntcDspTopologyAppend(Line, sizeof(Line), &Used, "without a host copier\n");
            IntcDspLog("%s", Line);
            Used = 0;
            Line[0] = 0;
            continue;
        }
        IntcDspTopologyValues(Line, sizeof(Line), &Used, &Rates, " Hz ");
        IntcDspTopologyValues(Line, sizeof(Line), &Used, &Channels, " ch ");
        IntcDspTopologyValues(Line, sizeof(Line), &Used, &Depths, "\n");
        IntcDspLog("%s", Line);
        Used = 0;
        Line[0] = 0;
    }
}

/* The line of a file the driver refuses, with the widget the refusal names */
static
VOID
IntcDspTopologyRefused(
    _In_z_ const char* Name,
    SOFTPLG_MODEL_STATUS Status,
    _In_opt_ const SOFTPLG_IPC4_MODEL* Model,
    UINT32 Widget)
{
    CHAR Named[SOFTPLG_TEXT_BYTES];
    SIZE_T Index;

    Named[0] = 0;
    if (Model != NULL && Widget < Model->WidgetCount)
    {
        for (Index = 0; Index + 1 < sizeof(Named) && Model->Widgets[Widget].Name[Index] != 0; Index++)
            Named[Index] = Model->Widgets[Widget].Name[Index];
        Named[Index] = 0;
    }
    IntcDspLog("topology '%s': %s%s%s%s; the DSP is powered down, no audio endpoint\n",
        Name, SoftplgModelStatusName(Status), Named[0] != 0 ? " at '" : "", Named, Named[0] != 0 ? "'" : "");
}

/*
 * The topology of a booted device: the file the evidence names, read and
 * built into the model the device keeps. FALSE when no file was selected,
 * the file could not be read or built, or the firmware cannot run what it
 * describes; the caller powers the DSP down then and no endpoint is created.
 */
BOOLEAN
IntcDspTopologyLoad(
    _Inout_ PINTCDSP_DEVICE Dev)
{
    SOFTPLG_MODEL_INPUT Input;
    SOFTPLG_IPC4_LIMITS Limits;
    SOFTPLG_IPC4_PROBLEM Problem;
    PSOFTPLG_IPC4_MODEL Model = NULL;
    DSP_TOPOLOGY_EVIDENCE Evidence;
    DSP_TOPOLOGY_SELECTION Selection;
    ADSP_TOPOLOGY_SOURCES Sources;
    CHAR Override[DSP_TOPOLOGY_NAME_BYTES];
    WCHAR Path[INTCDSP_TOPOLOGY_PATH_CHARS + DSP_TOPOLOGY_NAME_BYTES];
    PUCHAR Data = NULL;
    SIZE_T Bytes = 0, Index;
    SOFTPLG_MODEL_STATUS Result;
    NTSTATUS Status;

    RtlZeroMemory(&Evidence, sizeof(Evidence));
    RtlZeroMemory(&Sources, sizeof(Sources));
    if (NT_SUCCESS(IntcDspReadSettingString(INTCDSP_SETTINGS_TOPOLOGY, Override, sizeof(Override))) && Override[0] != 0)
        Evidence.Override = Override;
    IntcDspTopologySources(&Dev->Bus, &Sources);
    Evidence.Dmi = Sources.DmiValid ? &Sources.Dmi : NULL;
    Evidence.SpeakerTplg = Sources.SpeakerTplg[0] != 0 ? Sources.SpeakerTplg : NULL;
    Evidence.HpTplg = Sources.HpTplg[0] != 0 ? Sources.HpTplg : NULL;
    Evidence.MicTplg = Sources.MicTplg[0] != 0 ? Sources.MicTplg : NULL;
    DspTopologySelect(&Evidence, &Selection);

    /* the model of the device's first boot stands while the selection names the same file: a resume reads nothing again (R6) */
    if (Dev->TopologyModel != NULL && Selection.Name != NULL && DspTopologyNameValid(Selection.Name) &&
        strncmp(Dev->TopologyName, Selection.Name, sizeof(Dev->TopologyName)) == 0)
    {
        IntcDspLog("topology '%s': resident, the model of the device's first boot\n", Dev->TopologyName);
        IntcDspTopologyLog(Dev);
        return TRUE;
    }
    /* any other outcome replaces the model of the boot before */
    if (Dev->Topology != NULL)
    {
        ExFreePoolWithTag(Dev->Topology, INTCDSP_POOL_TAG);
        Dev->Topology = NULL;
        Dev->TopologyBytes = 0;
        Dev->TopologyModel = NULL;
        Dev->TopologyName[0] = 0;
    }

    if (Selection.Source == DspTopologySourceNone)
    {
        IntcDspLog("topology: none selected (no override, no quirk, no _DSD name); the DSP is powered down, no audio endpoint\n");
        return FALSE;
    }
    if (!DspTopologyNameValid(Selection.Name))
    {
        IntcDspLog("topology: '%s' from %s is not a plain file name; the DSP is powered down, no audio endpoint\n",
            Selection.Name, DspTopologySourceName(Selection.Source));
        return FALSE;
    }

    /* the topology directory, then the name as it reads */
    RtlCopyMemory(Path, INTCDSP_TOPOLOGY_PATH, sizeof(INTCDSP_TOPOLOGY_PATH));
    for (Index = 0; Selection.Name[Index] != 0; Index++)
        Path[INTCDSP_TOPOLOGY_PATH_CHARS - 1 + Index] = (WCHAR)Selection.Name[Index];
    Path[INTCDSP_TOPOLOGY_PATH_CHARS - 1 + Index] = 0;
    Status = IntcDspReadFile(Path, &Data, &Bytes, INTCDSP_TOPOLOGY_MAX_BYTES);
    if (!NT_SUCCESS(Status))
    {
        IntcDspLog("topology '%s' from %s: not read, 0x%08lx; the DSP is powered down, no audio endpoint\n",
            Selection.Name, DspTopologySourceName(Selection.Source), Status);
        return FALSE;
    }

    RtlZeroMemory(&Input, sizeof(Input));
    Input.Data = Data;
    Input.Size = Bytes;
    Input.Lookup = IntcDspModuleLookup;
    Input.LookupContext = Dev;
    Result = SoftplgModelMeasure(&Input, &Bytes, &Problem);
    if (Result == SoftplgModelOk)
    {
        Dev->Topology = ExAllocatePoolWithTag(NonPagedPool, Bytes, INTCDSP_POOL_TAG);
        if (Dev->Topology == NULL)
            Result = SoftplgModelBufferTooSmall;
    }
    if (Result == SoftplgModelOk)
        Result = SoftplgModelBuild(&Input, Dev->Topology, Bytes, &Model, &Problem);
    if (Result != SoftplgModelOk)
    {
        IntcDspTopologyRefused(Selection.Name, Result, Model, Problem.Widget);
        goto Fail;
    }
    Dev->TopologyBytes = Bytes;
    Dev->TopologyModel = Model;
    for (Index = 0; Selection.Name[Index] != 0 && Index + 1 < sizeof(Dev->TopologyName); Index++)
        Dev->TopologyName[Index] = Selection.Name[Index];
    Dev->TopologyName[Index] = 0;
    IntcDspLog("topology '%s': ABI %u.%u.%u, %I64u bytes, %u control%s, %u widget%s, %u edge%s, %u pcm%s, %u pipeline%s, %u manifest item%s, from %s\n",
        Dev->TopologyName, Model->AbiMajor, Model->AbiMinor, Model->AbiPatch, (ULONGLONG)Bytes, Model->ControlCount, IntcDspPlural(Model->ControlCount),
        Model->WidgetCount, IntcDspPlural(Model->WidgetCount), Model->EdgeCount, IntcDspPlural(Model->EdgeCount), Model->PcmCount, IntcDspPlural(Model->PcmCount),
        Model->PipelineCount, IntcDspPlural(Model->PipelineCount), Model->ManifestItems, IntcDspPlural(Model->ManifestItems), DspTopologySourceName(Selection.Source));

    /* the firmware's limits as its configuration reply gave them */
    Limits.MaxPipelines = Dev->Fw.FwConfig.MaxPipelines;
    Limits.MailboxBytes = Dev->Fw.FwConfig.DownlinkMailboxBytes;
    Result = SoftplgModelValidate(Model, &Limits, &Problem);
    if (Result != SoftplgModelOk)
    {
        IntcDspTopologyRefused(Dev->TopologyName, Result, Model, Problem.Widget);
        goto Fail;
    }
    /* the model stands alone: the file's bytes are not needed again */
    ExFreePoolWithTag(Data, INTCDSP_POOL_TAG);
    IntcDspTopologyLog(Dev);
    return TRUE;

Fail:
    if (Data != NULL)
        ExFreePoolWithTag(Data, INTCDSP_POOL_TAG);
    if (Dev->Topology != NULL)
    {
        ExFreePoolWithTag(Dev->Topology, INTCDSP_POOL_TAG);
        Dev->Topology = NULL;
    }
    Dev->TopologyModel = NULL;
    return FALSE;
}
