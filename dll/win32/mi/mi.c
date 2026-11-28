/*
 * PROJECT:     LiberNT Management Infrastructure
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Local Management Infrastructure client over the WMI services
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <initguid.h>
#include <wbemcli.h>
#include <stdlib.h>
#include <wchar.h>

#include "mi_private.h"

#define MI_APPLICATION_MAGIC 0x41494d4cu
#define MI_INSTANCE_MAGIC ((ptrdiff_t)0x49494d4c)

typedef struct _MI_ELEMENT
{
    MI_Char *Name;
    MI_Type Type;
    MI_Uint32 Flags;
    MI_Value Value;
} MI_ELEMENT;

typedef struct _MI_INSTANCE_IMPL
{
    MI_Instance Base;
    MI_Char *ClassName;
    MI_Char *NameSpace;
    MI_Char *ServerName;
    MI_Uint32 Count;
    MI_Uint32 Capacity;
    MI_ELEMENT *Elements;
} MI_INSTANCE_IMPL;

typedef struct _MI_APPLICATION_IMPL
{
    LONG Sessions;
} MI_APPLICATION_IMPL;

typedef struct _MI_SESSION_IMPL
{
    MI_Application Application;
} MI_SESSION_IMPL;

typedef enum _MI_OPERATION_KIND
{
    MiOperationGet,
    MiOperationEnumerate,
    MiOperationQuery,
    MiOperationClass,
    MiOperationComplete
} MI_OPERATION_KIND;

typedef struct _MI_OPERATION_IMPL
{
    LONG References;
    MI_Session Session;
    MI_OPERATION_KIND Kind;
    MI_Char *NameSpace;
    MI_Char *Text;
    MI_Result Result;
    MI_Char *ErrorMessage;
    MI_OperationCallbacks Callbacks;
    BOOL HasCallbacks;
    MI_Operation *Handle;
    HANDLE Thread;
    DWORD ThreadId;
    HANDLE Done;
    MI_Uint32 Count;
    MI_Uint32 Next;
    MI_INSTANCE_IMPL **Instances;
    volatile LONG Cancelled;
} MI_OPERATION_IMPL;

typedef struct _MI_OPTION
{
    MI_Char *Name;
    MI_Type Type;
    MI_Value Value;
} MI_OPTION;

typedef struct _MI_OPTIONS_IMPL
{
    MI_Uint32 Count;
    MI_OPTION *Options;
    MI_Uint32 Credentials;
} MI_OPTIONS_IMPL;

static const MI_InstanceFT InstanceFT;
static const MI_ApplicationFT ApplicationFT;
static const MI_SessionFT SessionFT;
static const MI_OperationFT OperationFT;

static MI_Char *
MiStrDup(const MI_Char *Text)
{
    return Text ? _wcsdup(Text) : NULL;
}

static BOOL
MiIsArray(MI_Type Type)
{
    return (Type & MI_ARRAY) != 0;
}

static SIZE_T
MiScalarSize(MI_Type Type)
{
    switch (Type & ~MI_ARRAY)
    {
        case MI_BOOLEAN:
        case MI_UINT8:
        case MI_SINT8:
            return 1;
        case MI_UINT16:
        case MI_SINT16:
        case MI_CHAR16:
            return 2;
        case MI_UINT32:
        case MI_SINT32:
        case MI_REAL32:
            return 4;
        case MI_UINT64:
        case MI_SINT64:
        case MI_REAL64:
            return 8;
        case MI_DATETIME:
            return sizeof(MI_Datetime);
        default:
            return sizeof(void *);
    }
}

static MI_INSTANCE_IMPL *
MiInstanceFromHandle(const MI_Instance *Instance)
{
    if (!Instance || Instance->ft != &InstanceFT || Instance->reserved[0] != MI_INSTANCE_MAGIC)
        return NULL;
    return (MI_INSTANCE_IMPL *)Instance;
}

static MI_INSTANCE_IMPL *
MiInstanceCreate(const MI_Char *ClassName)
{
    MI_INSTANCE_IMPL *Instance = calloc(1, sizeof(*Instance));

    if (!Instance)
        return NULL;
    Instance->Base.ft = &InstanceFT;
    Instance->Base.reserved[0] = MI_INSTANCE_MAGIC;
    Instance->ClassName = MiStrDup(ClassName);
    if (ClassName && !Instance->ClassName)
    {
        free(Instance);
        return NULL;
    }
    return Instance;
}

static MI_Result MiInstanceCloneImpl(const MI_INSTANCE_IMPL *Source, MI_INSTANCE_IMPL **Copy);

static void
MiValueFree(MI_Type Type, MI_Value *Value)
{
    MI_Uint32 i;

    switch (Type)
    {
        case MI_STRING:
            free(Value->string);
            break;
        case MI_INSTANCE:
        case MI_REFERENCE:
            if (Value->instance)
                Value->instance->ft->Delete(Value->instance);
            break;
        case MI_STRINGA:
            for (i = 0; i < Value->array.size; i++)
                free(((MI_Char **)Value->array.data)[i]);
            free(Value->array.data);
            break;
        case MI_INSTANCEA:
        case MI_REFERENCEA:
            for (i = 0; i < Value->array.size; i++)
            {
                MI_Instance *Item = ((MI_Instance **)Value->array.data)[i];
                if (Item)
                    Item->ft->Delete(Item);
            }
            free(Value->array.data);
            break;
        default:
            if (MiIsArray(Type))
                free(Value->array.data);
            break;
    }
    memset(Value, 0, sizeof(*Value));
}

static MI_Result
MiValueCopy(MI_Type Type, const MI_Value *Source, MI_Value *Copy)
{
    MI_Uint32 i;

    memset(Copy, 0, sizeof(*Copy));
    switch (Type)
    {
        case MI_STRING:
            if (Source->string && !(Copy->string = MiStrDup(Source->string)))
                return MI_RESULT_SERVER_LIMITS_EXCEEDED;
            return MI_RESULT_OK;
        case MI_INSTANCE:
        case MI_REFERENCE:
            if (Source->instance)
                return Source->instance->ft->Clone(Source->instance, &Copy->instance);
            return MI_RESULT_OK;
        default:
            break;
    }
    if (!MiIsArray(Type))
    {
        *Copy = *Source;
        return MI_RESULT_OK;
    }
    Copy->array.size = Source->array.size;
    if (!Source->array.size)
        return MI_RESULT_OK;
    Copy->array.data = calloc(Source->array.size, MiScalarSize(Type));
    if (!Copy->array.data)
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    if (Type == MI_STRINGA)
    {
        for (i = 0; i < Source->array.size; i++)
        {
            const MI_Char *Item = ((MI_Char **)Source->array.data)[i];
            if (Item && !(((MI_Char **)Copy->array.data)[i] = MiStrDup(Item)))
            {
                MiValueFree(Type, Copy);
                return MI_RESULT_SERVER_LIMITS_EXCEEDED;
            }
        }
        return MI_RESULT_OK;
    }
    if (Type == MI_INSTANCEA || Type == MI_REFERENCEA)
    {
        for (i = 0; i < Source->array.size; i++)
        {
            const MI_Instance *Item = ((MI_Instance **)Source->array.data)[i];
            if (Item && Item->ft->Clone(Item, &((MI_Instance **)Copy->array.data)[i]) != MI_RESULT_OK)
            {
                MiValueFree(Type, Copy);
                return MI_RESULT_SERVER_LIMITS_EXCEEDED;
            }
        }
        return MI_RESULT_OK;
    }
    memcpy(Copy->array.data, Source->array.data, Source->array.size * MiScalarSize(Type));
    return MI_RESULT_OK;
}

static void
MiInstanceClear(MI_INSTANCE_IMPL *Instance)
{
    MI_Uint32 i;

    for (i = 0; i < Instance->Count; i++)
    {
        MiValueFree(Instance->Elements[i].Type, &Instance->Elements[i].Value);
        free(Instance->Elements[i].Name);
    }
    free(Instance->Elements);
    free(Instance->ClassName);
    free(Instance->NameSpace);
    free(Instance->ServerName);
    Instance->Elements = NULL;
    Instance->ClassName = Instance->NameSpace = Instance->ServerName = NULL;
    Instance->Count = Instance->Capacity = 0;
    Instance->Base.nameSpace = Instance->Base.serverName = NULL;
}

static MI_ELEMENT *
MiInstanceFind(const MI_INSTANCE_IMPL *Instance, const MI_Char *Name, MI_Uint32 *Index)
{
    MI_Uint32 i;

    for (i = 0; Name && i < Instance->Count; i++)
    {
        if (!_wcsicmp(Instance->Elements[i].Name, Name))
        {
            if (Index)
                *Index = i;
            return &Instance->Elements[i];
        }
    }
    return NULL;
}

static MI_Result
MiInstanceAppend(MI_INSTANCE_IMPL *Instance, const MI_Char *Name, MI_Type Type, MI_Uint32 Flags, MI_Value *Owned)
{
    MI_ELEMENT *Element;

    if (Instance->Count == Instance->Capacity)
    {
        MI_Uint32 Capacity = Instance->Capacity ? Instance->Capacity * 2 : 16;
        MI_ELEMENT *Grown = realloc(Instance->Elements, Capacity * sizeof(*Grown));

        if (!Grown)
            return MI_RESULT_SERVER_LIMITS_EXCEEDED;
        Instance->Elements = Grown;
        Instance->Capacity = Capacity;
    }
    Element = &Instance->Elements[Instance->Count];
    if (!(Element->Name = MiStrDup(Name)))
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    Element->Type = Type;
    Element->Flags = Flags & (MI_FLAG_KEY | MI_FLAG_READONLY | MI_FLAG_NULL);
    Element->Value = *Owned;
    memset(Owned, 0, sizeof(*Owned));
    Instance->Count++;
    return MI_RESULT_OK;
}

static MI_Result
MiTakeValue(const MI_Value *Value, MI_Type Type, MI_Uint32 Flags, MI_Value *Owned, MI_Uint32 *ElementFlags)
{
    *ElementFlags = Flags;
    memset(Owned, 0, sizeof(*Owned));
    if (!Value || (Flags & MI_FLAG_NULL))
    {
        *ElementFlags |= MI_FLAG_NULL;
        return MI_RESULT_OK;
    }
    if (Flags & MI_FLAG_ADOPT)
    {
        *Owned = *Value;
        return MI_RESULT_OK;
    }
    return MiValueCopy(Type, Value, Owned);
}

static MI_Result MI_CALL
MiInstance_Clone(const MI_Instance *Self, MI_Instance **NewInstance)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self), *Copy;
    MI_Result Result;

    if (!Instance || !NewInstance)
        return MI_RESULT_INVALID_PARAMETER;
    Result = MiInstanceCloneImpl(Instance, &Copy);
    *NewInstance = Result == MI_RESULT_OK ? &Copy->Base : NULL;
    return Result;
}

static MI_Result MI_CALL
MiInstance_Destruct(MI_Instance *Self)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);

    if (!Instance)
        return MI_RESULT_INVALID_PARAMETER;
    MiInstanceClear(Instance);
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_Delete(MI_Instance *Self)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);

    if (!Instance)
        return MI_RESULT_INVALID_PARAMETER;
    MiInstanceClear(Instance);
    Instance->Base.reserved[0] = 0;
    free(Instance);
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_IsA(const MI_Instance *Self, const MI_ClassDecl *ClassDecl, MI_Boolean *Flag)
{
    if (!MiInstanceFromHandle(Self) || !Flag)
        return MI_RESULT_INVALID_PARAMETER;
    *Flag = (MI_Boolean)(ClassDecl != NULL && Self->classDecl == ClassDecl);
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_GetClassName(const MI_Instance *Self, const MI_Char **ClassName)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);

    if (!Instance || !ClassName)
        return MI_RESULT_INVALID_PARAMETER;
    *ClassName = Instance->ClassName;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_SetNameSpace(MI_Instance *Self, const MI_Char *NameSpace)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);
    MI_Char *Copy;

    if (!Instance)
        return MI_RESULT_INVALID_PARAMETER;
    Copy = MiStrDup(NameSpace);
    if (NameSpace && !Copy)
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    free(Instance->NameSpace);
    Instance->NameSpace = Copy;
    Instance->Base.nameSpace = Copy;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_GetNameSpace(const MI_Instance *Self, const MI_Char **NameSpace)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);

    if (!Instance || !NameSpace)
        return MI_RESULT_INVALID_PARAMETER;
    *NameSpace = Instance->NameSpace;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_GetElementCount(const MI_Instance *Self, MI_Uint32 *Count)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);

    if (!Instance || !Count)
        return MI_RESULT_INVALID_PARAMETER;
    *Count = Instance->Count;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_AddElement(MI_Instance *Self, const MI_Char *Name, const MI_Value *Value, MI_Type Type, MI_Uint32 Flags)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);
    MI_Uint32 ElementFlags;
    MI_Value Owned;
    MI_Result Result;

    if (!Instance || !Name || (Type & ~31))
        return MI_RESULT_INVALID_PARAMETER;
    if (MiInstanceFind(Instance, Name, NULL))
        return MI_RESULT_ALREADY_EXISTS;
    Result = MiTakeValue(Value, Type, Flags, &Owned, &ElementFlags);
    if (Result != MI_RESULT_OK)
        return Result;
    Result = MiInstanceAppend(Instance, Name, Type, ElementFlags, &Owned);
    if (Result != MI_RESULT_OK)
        MiValueFree(Type, &Owned);
    return Result;
}

static MI_Result
MiInstanceSet(MI_ELEMENT *Element, const MI_Value *Value, MI_Type Type, MI_Uint32 Flags)
{
    MI_Uint32 ElementFlags;
    MI_Value Owned;
    MI_Result Result;

    if (Type != Element->Type)
        return MI_RESULT_TYPE_MISMATCH;
    Result = MiTakeValue(Value, Type, Flags, &Owned, &ElementFlags);
    if (Result != MI_RESULT_OK)
        return Result;
    MiValueFree(Element->Type, &Element->Value);
    Element->Value = Owned;
    Element->Flags = (Element->Flags & (MI_FLAG_KEY | MI_FLAG_READONLY)) | (ElementFlags & MI_FLAG_NULL);
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_SetElement(MI_Instance *Self, const MI_Char *Name, const MI_Value *Value, MI_Type Type, MI_Uint32 Flags)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);
    MI_ELEMENT *Element;

    if (!Instance || !Name)
        return MI_RESULT_INVALID_PARAMETER;
    if (!(Element = MiInstanceFind(Instance, Name, NULL)))
        return MI_RESULT_NO_SUCH_PROPERTY;
    return MiInstanceSet(Element, Value, Type, Flags);
}

static MI_Result MI_CALL
MiInstance_SetElementAt(MI_Instance *Self, MI_Uint32 Index, const MI_Value *Value, MI_Type Type, MI_Uint32 Flags)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);

    if (!Instance)
        return MI_RESULT_INVALID_PARAMETER;
    if (Index >= Instance->Count)
        return MI_RESULT_NOT_FOUND;
    return MiInstanceSet(&Instance->Elements[Index], Value, Type, Flags);
}

static void
MiElementOut(const MI_ELEMENT *Element, MI_Value *Value, MI_Type *Type, MI_Uint32 *Flags)
{
    if (Value)
        *Value = Element->Value;
    if (Type)
        *Type = Element->Type;
    if (Flags)
        *Flags = Element->Flags;
}

static MI_Result MI_CALL
MiInstance_GetElement(const MI_Instance *Self, const MI_Char *Name, MI_Value *Value, MI_Type *Type, MI_Uint32 *Flags,
                      MI_Uint32 *Index)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);
    MI_ELEMENT *Element;

    if (!Instance || !Name)
        return MI_RESULT_INVALID_PARAMETER;
    if (!(Element = MiInstanceFind(Instance, Name, Index)))
        return MI_RESULT_NO_SUCH_PROPERTY;
    MiElementOut(Element, Value, Type, Flags);
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_GetElementAt(const MI_Instance *Self, MI_Uint32 Index, const MI_Char **Name, MI_Value *Value,
                        MI_Type *Type, MI_Uint32 *Flags)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);

    if (!Instance)
        return MI_RESULT_INVALID_PARAMETER;
    if (Index >= Instance->Count)
        return MI_RESULT_NOT_FOUND;
    if (Name)
        *Name = Instance->Elements[Index].Name;
    MiElementOut(&Instance->Elements[Index], Value, Type, Flags);
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_ClearElementAt(MI_Instance *Self, MI_Uint32 Index)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);

    if (!Instance)
        return MI_RESULT_INVALID_PARAMETER;
    if (Index >= Instance->Count)
        return MI_RESULT_NOT_FOUND;
    MiValueFree(Instance->Elements[Index].Type, &Instance->Elements[Index].Value);
    Instance->Elements[Index].Flags |= MI_FLAG_NULL;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_ClearElement(MI_Instance *Self, const MI_Char *Name)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);
    MI_Uint32 Index;

    if (!Instance || !Name)
        return MI_RESULT_INVALID_PARAMETER;
    if (!MiInstanceFind(Instance, Name, &Index))
        return MI_RESULT_NO_SUCH_PROPERTY;
    return MiInstance_ClearElementAt(Self, Index);
}

static MI_Result MI_CALL
MiInstance_GetServerName(const MI_Instance *Self, const MI_Char **Name)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);

    if (!Instance || !Name)
        return MI_RESULT_INVALID_PARAMETER;
    *Name = Instance->ServerName;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_SetServerName(MI_Instance *Self, const MI_Char *Name)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceFromHandle(Self);
    MI_Char *Copy;

    if (!Instance)
        return MI_RESULT_INVALID_PARAMETER;
    Copy = MiStrDup(Name);
    if (Name && !Copy)
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    free(Instance->ServerName);
    Instance->ServerName = Copy;
    Instance->Base.serverName = Copy;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiInstance_GetClass(const MI_Instance *Self, MI_Class **InstanceClass)
{
    if (!MiInstanceFromHandle(Self) || !InstanceClass)
        return MI_RESULT_INVALID_PARAMETER;
    /* TODO: class objects (schema of the instance's class) */
    *InstanceClass = NULL;
    return MI_RESULT_NOT_SUPPORTED;
}

static const MI_InstanceFT InstanceFT =
{
    MiInstance_Clone,
    MiInstance_Destruct,
    MiInstance_Delete,
    MiInstance_IsA,
    MiInstance_GetClassName,
    MiInstance_SetNameSpace,
    MiInstance_GetNameSpace,
    MiInstance_GetElementCount,
    MiInstance_AddElement,
    MiInstance_SetElement,
    MiInstance_SetElementAt,
    MiInstance_GetElement,
    MiInstance_GetElementAt,
    MiInstance_ClearElement,
    MiInstance_ClearElementAt,
    MiInstance_GetServerName,
    MiInstance_SetServerName,
    MiInstance_GetClass,
};

static MI_Result
MiInstanceCloneImpl(const MI_INSTANCE_IMPL *Source, MI_INSTANCE_IMPL **Copy)
{
    MI_INSTANCE_IMPL *Instance = MiInstanceCreate(Source->ClassName);
    MI_Result Result = MI_RESULT_OK;
    MI_Uint32 i;

    *Copy = NULL;
    if (!Instance)
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    Instance->Base.classDecl = Source->Base.classDecl;
    if (MiInstance_SetNameSpace(&Instance->Base, Source->NameSpace) != MI_RESULT_OK ||
        MiInstance_SetServerName(&Instance->Base, Source->ServerName) != MI_RESULT_OK)
        Result = MI_RESULT_SERVER_LIMITS_EXCEEDED;
    for (i = 0; Result == MI_RESULT_OK && i < Source->Count; i++)
    {
        const MI_ELEMENT *Element = &Source->Elements[i];
        MI_Value Owned;

        if (Element->Flags & MI_FLAG_NULL)
            memset(&Owned, 0, sizeof(Owned));
        else
            Result = MiValueCopy(Element->Type, &Element->Value, &Owned);
        if (Result == MI_RESULT_OK && (Result = MiInstanceAppend(Instance, Element->Name, Element->Type,
                                                                 Element->Flags, &Owned)) != MI_RESULT_OK)
            MiValueFree(Element->Type, &Owned);
    }
    if (Result != MI_RESULT_OK)
    {
        MiInstance_Delete(&Instance->Base);
        return Result;
    }
    *Copy = Instance;
    return MI_RESULT_OK;
}

static MI_Type
MiTypeFromCim(CIMTYPE Type)
{
    MI_Type Base;

    switch (Type & ~CIM_FLAG_ARRAY)
    {
        case CIM_SINT8: Base = MI_SINT8; break;
        case CIM_UINT8: Base = MI_UINT8; break;
        case CIM_SINT16: Base = MI_SINT16; break;
        case CIM_UINT16: Base = MI_UINT16; break;
        case CIM_SINT32: Base = MI_SINT32; break;
        case CIM_UINT32: Base = MI_UINT32; break;
        case CIM_SINT64: Base = MI_SINT64; break;
        case CIM_UINT64: Base = MI_UINT64; break;
        case CIM_REAL32: Base = MI_REAL32; break;
        case CIM_REAL64: Base = MI_REAL64; break;
        case CIM_BOOLEAN: Base = MI_BOOLEAN; break;
        case CIM_DATETIME: Base = MI_DATETIME; break;
        case CIM_REFERENCE: Base = MI_REFERENCE; break;
        case CIM_CHAR16: Base = MI_CHAR16; break;
        case CIM_OBJECT: Base = MI_INSTANCE; break;
        default: Base = MI_STRING; break;
    }
    return (Type & CIM_FLAG_ARRAY) ? (MI_Type)(Base | MI_ARRAY) : Base;
}

static MI_Uint32
MiDigits(const WCHAR *Text, int Count, BOOL *Ok)
{
    MI_Uint32 Value = 0;
    int i;

    for (i = 0; i < Count; i++)
    {
        if (Text[i] < '0' || Text[i] > '9')
        {
            *Ok = FALSE;
            return 0;
        }
        Value = Value * 10 + (Text[i] - '0');
    }
    return Value;
}

static BOOL
MiParseDatetime(const WCHAR *Text, MI_Datetime *Datetime)
{
    BOOL Ok = TRUE;

    memset(Datetime, 0, sizeof(*Datetime));
    if (!Text || wcslen(Text) != 25 || Text[14] != '.')
        return FALSE;
    if (Text[21] == ':')
    {
        Datetime->isTimestamp = 0;
        Datetime->u.interval.days = MiDigits(Text, 8, &Ok);
        Datetime->u.interval.hours = MiDigits(Text + 8, 2, &Ok);
        Datetime->u.interval.minutes = MiDigits(Text + 10, 2, &Ok);
        Datetime->u.interval.seconds = MiDigits(Text + 12, 2, &Ok);
        Datetime->u.interval.microseconds = MiDigits(Text + 15, 6, &Ok);
        return Ok;
    }
    if (Text[21] != '+' && Text[21] != '-')
        return FALSE;
    Datetime->isTimestamp = 1;
    Datetime->u.timestamp.year = MiDigits(Text, 4, &Ok);
    Datetime->u.timestamp.month = MiDigits(Text + 4, 2, &Ok);
    Datetime->u.timestamp.day = MiDigits(Text + 6, 2, &Ok);
    Datetime->u.timestamp.hour = MiDigits(Text + 8, 2, &Ok);
    Datetime->u.timestamp.minute = MiDigits(Text + 10, 2, &Ok);
    Datetime->u.timestamp.second = MiDigits(Text + 12, 2, &Ok);
    Datetime->u.timestamp.microseconds = MiDigits(Text + 15, 6, &Ok);
    Datetime->u.timestamp.utc = (MI_Sint32)MiDigits(Text + 22, 3, &Ok);
    if (Text[21] == '-')
        Datetime->u.timestamp.utc = -Datetime->u.timestamp.utc;
    return Ok;
}

static BOOL
MiScalarFromVariant(MI_Type Type, VARIANT *Variant, void *Out)
{
    VARIANT Converted;
    BOOL Ok = TRUE;

    if (Type == MI_STRING)
    {
        if (V_VT(Variant) != VT_BSTR)
            return FALSE;
        *(MI_Char **)Out = MiStrDup(V_BSTR(Variant) ? V_BSTR(Variant) : L"");
        return *(MI_Char **)Out != NULL;
    }
    if (Type == MI_DATETIME)
        return V_VT(Variant) == VT_BSTR && MiParseDatetime(V_BSTR(Variant), (MI_Datetime *)Out);
    if ((Type == MI_UINT64 || Type == MI_SINT64) && V_VT(Variant) == VT_BSTR)
    {
        if (Type == MI_UINT64)
            *(MI_Uint64 *)Out = _wcstoui64(V_BSTR(Variant), NULL, 10);
        else
            *(MI_Sint64 *)Out = _wcstoi64(V_BSTR(Variant), NULL, 10);
        return TRUE;
    }
    VariantInit(&Converted);
    switch (Type)
    {
        case MI_BOOLEAN:
            Ok = SUCCEEDED(VariantChangeType(&Converted, Variant, 0, VT_BOOL));
            if (Ok)
                *(MI_Boolean *)Out = V_BOOL(&Converted) ? MI_TRUE : MI_FALSE;
            break;
        case MI_UINT8:
        case MI_SINT8:
        case MI_UINT16:
        case MI_SINT16:
        case MI_CHAR16:
        case MI_UINT32:
        case MI_SINT32:
        case MI_UINT64:
        case MI_SINT64:
            Ok = SUCCEEDED(VariantChangeType(&Converted, Variant, 0, VT_I8));
            if (Ok)
                memcpy(Out, &V_I8(&Converted), MiScalarSize(Type));
            break;
        case MI_REAL32:
            Ok = SUCCEEDED(VariantChangeType(&Converted, Variant, 0, VT_R4));
            if (Ok)
                *(MI_Real32 *)Out = V_R4(&Converted);
            break;
        case MI_REAL64:
            Ok = SUCCEEDED(VariantChangeType(&Converted, Variant, 0, VT_R8));
            if (Ok)
                *(MI_Real64 *)Out = V_R8(&Converted);
            break;
        default:
            Ok = FALSE;
            break;
    }
    VariantClear(&Converted);
    return Ok;
}

static BOOL
MiArrayFromVariant(MI_Type Type, VARIANT *Variant, MI_Value *Value)
{
    MI_Type Scalar = (MI_Type)(Type & ~MI_ARRAY);
    SAFEARRAY *Array;
    LONG Lower, Upper, i;
    VARTYPE ItemType;
    SIZE_T Size = MiScalarSize(Type);

    if (!(V_VT(Variant) & VT_ARRAY) || !(Array = V_ARRAY(Variant)))
        return FALSE;
    if (FAILED(SafeArrayGetLBound(Array, 1, &Lower)) || FAILED(SafeArrayGetUBound(Array, 1, &Upper)) ||
        FAILED(SafeArrayGetVartype(Array, &ItemType)))
        return FALSE;
    Value->array.size = Upper >= Lower ? (MI_Uint32)(Upper - Lower + 1) : 0;
    if (!Value->array.size)
        return TRUE;
    if (!(Value->array.data = calloc(Value->array.size, Size)))
        return FALSE;
    for (i = 0; i < (LONG)Value->array.size; i++)
    {
        LONG Index = Lower + i;
        VARIANT Item;

        VariantInit(&Item);
        V_VT(&Item) = ItemType;
        if (ItemType == VT_BSTR)
        {
            if (FAILED(SafeArrayGetElement(Array, &Index, &V_BSTR(&Item))))
                break;
        }
        else if (FAILED(SafeArrayGetElement(Array, &Index, &V_I8(&Item))))
            break;
        if (!MiScalarFromVariant(Scalar, &Item, (BYTE *)Value->array.data + i * Size))
        {
            VariantClear(&Item);
            break;
        }
        VariantClear(&Item);
    }
    if (i != (LONG)Value->array.size)
    {
        MiValueFree(Type, Value);
        return FALSE;
    }
    return TRUE;
}

static MI_Result
MiInstanceFromWbem(IWbemClassObject *Object, MI_INSTANCE_IMPL **Out)
{
    MI_INSTANCE_IMPL *Instance;
    VARIANT Variant;
    BSTR Name;
    CIMTYPE CimType;
    MI_Result Result = MI_RESULT_OK;

    *Out = NULL;
    VariantInit(&Variant);
    if (FAILED(IWbemClassObject_Get(Object, L"__CLASS", 0, &Variant, NULL, NULL)) || V_VT(&Variant) != VT_BSTR)
    {
        VariantClear(&Variant);
        return MI_RESULT_FAILED;
    }
    Instance = MiInstanceCreate(V_BSTR(&Variant));
    VariantClear(&Variant);
    if (!Instance)
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    if (SUCCEEDED(IWbemClassObject_Get(Object, L"__NAMESPACE", 0, &Variant, NULL, NULL)) && V_VT(&Variant) == VT_BSTR)
        MiInstance_SetNameSpace(&Instance->Base, V_BSTR(&Variant));
    VariantClear(&Variant);
    if (SUCCEEDED(IWbemClassObject_Get(Object, L"__SERVER", 0, &Variant, NULL, NULL)) && V_VT(&Variant) == VT_BSTR)
        MiInstance_SetServerName(&Instance->Base, V_BSTR(&Variant));
    VariantClear(&Variant);

    if (FAILED(IWbemClassObject_BeginEnumeration(Object, WBEM_FLAG_NONSYSTEM_ONLY)))
    {
        MiInstance_Delete(&Instance->Base);
        return MI_RESULT_FAILED;
    }
    while (Result == MI_RESULT_OK &&
           IWbemClassObject_Next(Object, 0, &Name, &Variant, &CimType, NULL) == WBEM_S_NO_ERROR)
    {
        MI_Type Type = MiTypeFromCim(CimType);
        MI_Uint32 Flags = 0;
        MI_Value Owned;

        memset(&Owned, 0, sizeof(Owned));
        if (V_VT(&Variant) == VT_NULL || V_VT(&Variant) == VT_EMPTY || Type == MI_REFERENCE ||
            Type == MI_INSTANCE || Type == MI_REFERENCEA || Type == MI_INSTANCEA)
        {
            /* FIXME: references and embedded objects are reported as null values */
            Flags = MI_FLAG_NULL;
        }
        else if (MiIsArray(Type) ? !MiArrayFromVariant(Type, &Variant, &Owned)
                                 : !MiScalarFromVariant(Type, &Variant, &Owned))
        {
            Flags = MI_FLAG_NULL;
            memset(&Owned, 0, sizeof(Owned));
        }
        Result = MiInstanceAppend(Instance, Name, Type, Flags, &Owned);
        if (Result != MI_RESULT_OK)
            MiValueFree(Type, &Owned);
        SysFreeString(Name);
        VariantClear(&Variant);
    }
    IWbemClassObject_EndEnumeration(Object);
    if (Result != MI_RESULT_OK)
    {
        MiInstance_Delete(&Instance->Base);
        return Result;
    }
    *Out = Instance;
    return MI_RESULT_OK;
}

static MI_Result
MiResultFromHresult(HRESULT Hr)
{
    switch (Hr)
    {
        case S_OK:
        case WBEM_S_FALSE:
            return MI_RESULT_OK;
        case WBEM_E_ACCESS_DENIED:
        case E_ACCESSDENIED:
            return MI_RESULT_ACCESS_DENIED;
        case WBEM_E_INVALID_NAMESPACE:
            return MI_RESULT_INVALID_NAMESPACE;
        case WBEM_E_INVALID_PARAMETER:
        case E_INVALIDARG:
            return MI_RESULT_INVALID_PARAMETER;
        case WBEM_E_INVALID_CLASS:
            return MI_RESULT_INVALID_CLASS;
        case WBEM_E_NOT_FOUND:
            return MI_RESULT_NOT_FOUND;
        case WBEM_E_NOT_SUPPORTED:
        case E_NOTIMPL:
            return MI_RESULT_NOT_SUPPORTED;
        case WBEM_E_INVALID_QUERY_TYPE:
            return MI_RESULT_QUERY_LANGUAGE_NOT_SUPPORTED;
        case WBEM_E_INVALID_QUERY:
            return MI_RESULT_INVALID_QUERY;
        case E_OUTOFMEMORY:
        case WBEM_E_OUT_OF_MEMORY:
            return MI_RESULT_SERVER_LIMITS_EXCEEDED;
        default:
            return MI_RESULT_FAILED;
    }
}

static void
MiOperationRelease(MI_OPERATION_IMPL *Operation)
{
    MI_Uint32 i;

    if (InterlockedDecrement(&Operation->References) != 0)
        return;
    for (i = 0; i < Operation->Count; i++)
        MiInstance_Delete(&Operation->Instances[i]->Base);
    free(Operation->Instances);
    free(Operation->NameSpace);
    free(Operation->Text);
    free(Operation->ErrorMessage);
    if (Operation->Thread)
        CloseHandle(Operation->Thread);
    if (Operation->Done)
        CloseHandle(Operation->Done);
    free(Operation);
}

static MI_Result
MiOperationAdd(MI_OPERATION_IMPL *Operation, MI_INSTANCE_IMPL *Instance)
{
    MI_INSTANCE_IMPL **Grown;

    Grown = realloc(Operation->Instances, (Operation->Count + 1) * sizeof(*Grown));
    if (!Grown)
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    Operation->Instances = Grown;
    Operation->Instances[Operation->Count++] = Instance;
    return MI_RESULT_OK;
}

static MI_Result
MiOperationCollect(MI_OPERATION_IMPL *Operation, IEnumWbemClassObject *Enum)
{
    IWbemClassObject *Object;
    ULONG Got;
    HRESULT Hr;
    MI_Result Result = MI_RESULT_OK;

    while (Result == MI_RESULT_OK && !Operation->Cancelled)
    {
        Hr = IEnumWbemClassObject_Next(Enum, WBEM_INFINITE, 1, &Object, &Got);
        if (Hr != WBEM_S_NO_ERROR || !Got)
        {
            if (FAILED(Hr))
                Result = MiResultFromHresult(Hr);
            break;
        }
        {
            MI_INSTANCE_IMPL *Instance;

            Result = MiInstanceFromWbem(Object, &Instance);
            if (Result == MI_RESULT_OK && (Result = MiOperationAdd(Operation, Instance)) != MI_RESULT_OK)
                MiInstance_Delete(&Instance->Base);
        }
        IWbemClassObject_Release(Object);
    }
    return Result;
}

static MI_Result
MiOperationExecute(MI_OPERATION_IMPL *Operation)
{
    IWbemLocator *Locator = NULL;
    IWbemServices *Services = NULL;
    IEnumWbemClassObject *Enum = NULL;
    IWbemClassObject *Object = NULL;
    BSTR NameSpace, Text = NULL, Language = NULL;
    MI_Result Result;
    HRESULT Hr;
    WCHAR *p;

    if (Operation->Kind == MiOperationComplete || Operation->Kind == MiOperationClass)
        return Operation->Result;
    for (p = Operation->NameSpace; *p; p++)
    {
        if (*p == '/')
            *p = '\\';
    }
    Hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void **)&Locator);
    if (FAILED(Hr))
        return MiResultFromHresult(Hr);
    NameSpace = SysAllocString(Operation->NameSpace);
    Hr = NameSpace ? IWbemLocator_ConnectServer(Locator, NameSpace, NULL, NULL, NULL, 0, NULL, NULL, &Services)
                   : E_OUTOFMEMORY;
    SysFreeString(NameSpace);
    IWbemLocator_Release(Locator);
    if (FAILED(Hr))
        return MiResultFromHresult(Hr);

    Text = SysAllocString(Operation->Text);
    if (!Text)
        Hr = E_OUTOFMEMORY;
    else if (Operation->Kind == MiOperationQuery)
    {
        Language = SysAllocString(L"WQL");
        Hr = Language ? IWbemServices_ExecQuery(Services, Language, Text, WBEM_FLAG_FORWARD_ONLY, NULL, &Enum)
                      : E_OUTOFMEMORY;
    }
    else if (Operation->Kind == MiOperationEnumerate)
        Hr = IWbemServices_CreateInstanceEnum(Services, Text, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_DEEP, NULL, &Enum);
    else
        Hr = IWbemServices_GetObject(Services, Text, 0, NULL, &Object, NULL);

    if (FAILED(Hr))
        Result = MiResultFromHresult(Hr);
    else if (Enum)
        Result = MiOperationCollect(Operation, Enum);
    else
    {
        MI_INSTANCE_IMPL *Instance;

        Result = MiInstanceFromWbem(Object, &Instance);
        if (Result == MI_RESULT_OK && (Result = MiOperationAdd(Operation, Instance)) != MI_RESULT_OK)
            MiInstance_Delete(&Instance->Base);
    }
    if (Object)
        IWbemClassObject_Release(Object);
    if (Enum)
        IEnumWbemClassObject_Release(Enum);
    IWbemServices_Release(Services);
    SysFreeString(Text);
    SysFreeString(Language);
    return Result;
}

static DWORD WINAPI
MiOperationThread(void *Context)
{
    MI_OPERATION_IMPL *Operation = Context;
    HRESULT Init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    MI_Uint32 i;

    Operation->Result = MiOperationExecute(Operation);
    if (Operation->Cancelled && Operation->Result == MI_RESULT_OK)
        Operation->Result = MI_RESULT_FAILED;
    if (SUCCEEDED(Init))
        CoUninitialize();

    if (Operation->HasCallbacks && Operation->Kind == MiOperationClass && Operation->Callbacks.classResult)
    {
        Operation->Callbacks.classResult(Operation->Handle, Operation->Callbacks.callbackContext, NULL, MI_FALSE,
                                         Operation->Result, Operation->ErrorMessage, NULL, NULL);
    }
    else if (Operation->HasCallbacks && Operation->Callbacks.instanceResult)
    {
        for (i = 0; i < Operation->Count && !Operation->Cancelled; i++)
        {
            Operation->Callbacks.instanceResult(Operation->Handle, Operation->Callbacks.callbackContext,
                                                &Operation->Instances[i]->Base, MI_TRUE, MI_RESULT_OK, NULL, NULL,
                                                NULL);
        }
        Operation->Callbacks.instanceResult(Operation->Handle, Operation->Callbacks.callbackContext, NULL, MI_FALSE,
                                            Operation->Result, Operation->ErrorMessage, NULL, NULL);
    }
    SetEvent(Operation->Done);
    MiOperationRelease(Operation);
    return 0;
}

static void
MiOperationStart(MI_Session *Session, MI_OPERATION_KIND Kind, const MI_Char *NameSpace, const MI_Char *Text,
                 MI_Result Result, MI_OperationCallbacks *Callbacks, MI_Operation *Handle)
{
    MI_OPERATION_IMPL *Operation;

    if (!Handle)
        return;
    memset(Handle, 0, sizeof(*Handle));
    Operation = calloc(1, sizeof(*Operation));
    if (Operation)
    {
        Operation->References = 2;
        Operation->Session = *Session;
        Operation->Kind = Kind;
        Operation->Result = Result;
        Operation->Handle = Handle;
        Operation->NameSpace = MiStrDup(NameSpace ? NameSpace : L"root\\cimv2");
        Operation->Text = MiStrDup(Text ? Text : L"");
        Operation->Done = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (Callbacks)
        {
            Operation->Callbacks = *Callbacks;
            Operation->HasCallbacks = Callbacks->instanceResult != NULL || Callbacks->classResult != NULL;
        }
    }
    if (!Operation || !Operation->NameSpace || !Operation->Text || !Operation->Done)
    {
        if (Operation)
        {
            Operation->References = 1;
            MiOperationRelease(Operation);
        }
        if (Callbacks && Callbacks->instanceResult)
            Callbacks->instanceResult(NULL, Callbacks->callbackContext, NULL, MI_FALSE,
                                      MI_RESULT_SERVER_LIMITS_EXCEEDED, NULL, NULL, NULL);
        return;
    }
    Handle->reserved1 = 1;
    Handle->reserved2 = (ptrdiff_t)Operation;
    Handle->ft = &OperationFT;
    Operation->Thread = CreateThread(NULL, 0, MiOperationThread, Operation, 0, &Operation->ThreadId);
    if (!Operation->Thread)
        MiOperationThread(Operation);
}

static MI_OPERATION_IMPL *
MiOperationFromHandle(MI_Operation *Handle)
{
    if (!Handle || Handle->ft != &OperationFT || !Handle->reserved2)
        return NULL;
    return (MI_OPERATION_IMPL *)Handle->reserved2;
}

static MI_Result MI_CALL
MiOperation_Close(MI_Operation *Handle)
{
    MI_OPERATION_IMPL *Operation = MiOperationFromHandle(Handle);

    if (!Operation)
        return MI_RESULT_INVALID_PARAMETER;
    if (Operation->ThreadId != GetCurrentThreadId())
        WaitForSingleObject(Operation->Done, INFINITE);
    Handle->reserved2 = 0;
    Handle->ft = NULL;
    MiOperationRelease(Operation);
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiOperation_Cancel(MI_Operation *Handle, MI_CancellationReason Reason)
{
    MI_OPERATION_IMPL *Operation = MiOperationFromHandle(Handle);

    if (!Operation)
        return MI_RESULT_INVALID_PARAMETER;
    InterlockedExchange(&Operation->Cancelled, 1);
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiOperation_GetSession(MI_Operation *Handle, MI_Session *Session)
{
    MI_OPERATION_IMPL *Operation = MiOperationFromHandle(Handle);

    if (!Operation || !Session)
        return MI_RESULT_INVALID_PARAMETER;
    *Session = Operation->Session;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiOperation_GetInstance(MI_Operation *Handle, const MI_Instance **Instance, MI_Boolean *MoreResults,
                        MI_Result *Result, const MI_Char **ErrorMessage, const MI_Instance **CompletionDetails)
{
    MI_OPERATION_IMPL *Operation = MiOperationFromHandle(Handle);

    if (!Operation || !Instance || Operation->HasCallbacks)
        return MI_RESULT_INVALID_PARAMETER;
    WaitForSingleObject(Operation->Done, INFINITE);
    if (CompletionDetails)
        *CompletionDetails = NULL;
    if (ErrorMessage)
        *ErrorMessage = NULL;
    if (Operation->Next < Operation->Count)
    {
        *Instance = &Operation->Instances[Operation->Next++]->Base;
        if (MoreResults)
            *MoreResults = MI_TRUE;
        if (Result)
            *Result = MI_RESULT_OK;
        return MI_RESULT_OK;
    }
    *Instance = NULL;
    if (MoreResults)
        *MoreResults = MI_FALSE;
    if (Result)
        *Result = Operation->Result;
    if (ErrorMessage)
        *ErrorMessage = Operation->ErrorMessage;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiOperation_GetIndication(MI_Operation *Handle, const MI_Instance **Instance, const MI_Char **Bookmark,
                          const MI_Char **MachineID, MI_Boolean *MoreResults, MI_Result *Result,
                          const MI_Char **ErrorMessage, const MI_Instance **CompletionDetails)
{
    MI_OPERATION_IMPL *Operation = MiOperationFromHandle(Handle);

    if (!Operation || !Instance)
        return MI_RESULT_INVALID_PARAMETER;
    WaitForSingleObject(Operation->Done, INFINITE);
    *Instance = NULL;
    if (Bookmark)
        *Bookmark = NULL;
    if (MachineID)
        *MachineID = NULL;
    if (MoreResults)
        *MoreResults = MI_FALSE;
    if (Result)
        *Result = MI_RESULT_NOT_SUPPORTED;
    if (ErrorMessage)
        *ErrorMessage = NULL;
    if (CompletionDetails)
        *CompletionDetails = NULL;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiOperation_GetClass(MI_Operation *Handle, const MI_Class **ClassResult, MI_Boolean *MoreResults, MI_Result *Result,
                     const MI_Char **ErrorMessage, const MI_Instance **CompletionDetails)
{
    MI_OPERATION_IMPL *Operation = MiOperationFromHandle(Handle);

    if (!Operation || !ClassResult)
        return MI_RESULT_INVALID_PARAMETER;
    WaitForSingleObject(Operation->Done, INFINITE);
    *ClassResult = NULL;
    if (MoreResults)
        *MoreResults = MI_FALSE;
    if (Result)
        *Result = Operation->Result;
    if (ErrorMessage)
        *ErrorMessage = NULL;
    if (CompletionDetails)
        *CompletionDetails = NULL;
    return MI_RESULT_OK;
}

static const MI_OperationFT OperationFT =
{
    MiOperation_Close,
    MiOperation_Cancel,
    MiOperation_GetSession,
    MiOperation_GetInstance,
    MiOperation_GetIndication,
    MiOperation_GetClass,
};

static MI_Result MI_CALL
MiSession_Close(MI_Session *Session, void *CompletionContext, void (MI_CALL *CompletionCallback)(void *))
{
    if (!Session || Session->ft != &SessionFT)
        return MI_RESULT_INVALID_PARAMETER;
    free((MI_SESSION_IMPL *)Session->reserved2);
    Session->reserved2 = 0;
    Session->ft = NULL;
    if (CompletionCallback)
        CompletionCallback(CompletionContext);
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiSession_GetApplication(MI_Session *Session, MI_Application *Application)
{
    if (!Session || Session->ft != &SessionFT || !Application)
        return MI_RESULT_INVALID_PARAMETER;
    *Application = ((MI_SESSION_IMPL *)Session->reserved2)->Application;
    return MI_RESULT_OK;
}

static MI_Char *
MiObjectPath(const MI_Instance *Instance)
{
    MI_INSTANCE_IMPL *Impl = MiInstanceFromHandle(Instance);
    SIZE_T Length;
    MI_Char *Path;
    MI_Uint32 i, Keys = 0;

    if (!Impl || !Impl->ClassName)
        return NULL;
    Length = wcslen(Impl->ClassName) + 2;
    for (i = 0; i < Impl->Count; i++)
    {
        const MI_ELEMENT *Element = &Impl->Elements[i];
        if (!(Element->Flags & MI_FLAG_NULL) && Element->Type == MI_STRING)
            Length += wcslen(Element->Name) + wcslen(Element->Value.string) * 2 + 5;
        else if (!(Element->Flags & MI_FLAG_NULL) && Element->Type <= MI_SINT64)
            Length += wcslen(Element->Name) + 25;
    }
    if (!(Path = malloc(Length * sizeof(MI_Char))))
        return NULL;
    wcscpy(Path, Impl->ClassName);
    for (i = 0; i < Impl->Count; i++)
    {
        const MI_ELEMENT *Element = &Impl->Elements[i];
        WCHAR Number[24];

        if (Element->Flags & MI_FLAG_NULL)
            continue;
        if (Element->Type == MI_STRING)
        {
            const MI_Char *s;
            MI_Char *d;

            wcscat(Path, Keys++ ? L"," : L".");
            wcscat(Path, Element->Name);
            wcscat(Path, L"=\"");
            d = Path + wcslen(Path);
            for (s = Element->Value.string; *s; s++)
            {
                if (*s == '"' || *s == '\\')
                    *d++ = '\\';
                *d++ = *s;
            }
            *d = 0;
            wcscat(Path, L"\"");
        }
        else if (Element->Type >= MI_UINT8 && Element->Type <= MI_SINT64)
        {
            MI_Sint64 Signed = 0;
            MI_Uint64 Unsigned = 0;

            switch (Element->Type)
            {
                case MI_UINT8: Unsigned = Element->Value.uint8; break;
                case MI_SINT8: Signed = Element->Value.sint8; break;
                case MI_UINT16: Unsigned = Element->Value.uint16; break;
                case MI_SINT16: Signed = Element->Value.sint16; break;
                case MI_UINT32: Unsigned = Element->Value.uint32; break;
                case MI_SINT32: Signed = Element->Value.sint32; break;
                case MI_UINT64: Unsigned = Element->Value.uint64; break;
                default: Signed = Element->Value.sint64; break;
            }
            if (Element->Type == MI_SINT8 || Element->Type == MI_SINT16 || Element->Type == MI_SINT32 ||
                Element->Type == MI_SINT64)
                swprintf(Number, 24, L"%I64d", Signed);
            else
                swprintf(Number, 24, L"%I64u", Unsigned);
            wcscat(Path, Keys++ ? L"," : L".");
            wcscat(Path, Element->Name);
            wcscat(Path, L"=");
            wcscat(Path, Number);
        }
    }
    return Path;
}

static void MI_CALL
MiSession_GetInstance(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options, const MI_Char *NameSpace,
                      const MI_Instance *InboundInstance, MI_OperationCallbacks *Callbacks, MI_Operation *Operation)
{
    MI_Char *Path = MiObjectPath(InboundInstance);

    MiOperationStart(Session, Path ? MiOperationGet : MiOperationComplete, NameSpace, Path,
                     Path ? MI_RESULT_OK : MI_RESULT_INVALID_PARAMETER, Callbacks, Operation);
    free(Path);
}

static void MI_CALL
MiSession_Unsupported(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options, const MI_Char *NameSpace,
                      const MI_Instance *InboundInstance, MI_OperationCallbacks *Callbacks, MI_Operation *Operation)
{
    /* TODO: modifying, creating and deleting instances */
    MiOperationStart(Session, MiOperationComplete, NameSpace, NULL, MI_RESULT_NOT_SUPPORTED, Callbacks, Operation);
}

static void MI_CALL
MiSession_Invoke(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options, const MI_Char *NameSpace,
                 const MI_Char *ClassName, const MI_Char *MethodName, const MI_Instance *InboundInstance,
                 const MI_Instance *InboundProperties, MI_OperationCallbacks *Callbacks, MI_Operation *Operation)
{
    /* TODO: method invocation */
    MiOperationStart(Session, MiOperationComplete, NameSpace, NULL, MI_RESULT_NOT_SUPPORTED, Callbacks, Operation);
}

static void MI_CALL
MiSession_EnumerateInstances(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options,
                             const MI_Char *NameSpace, const MI_Char *ClassName, MI_Boolean KeysOnly,
                             MI_OperationCallbacks *Callbacks, MI_Operation *Operation)
{
    MiOperationStart(Session, ClassName ? MiOperationEnumerate : MiOperationComplete, NameSpace, ClassName,
                     ClassName ? MI_RESULT_OK : MI_RESULT_INVALID_PARAMETER, Callbacks, Operation);
}

static void MI_CALL
MiSession_QueryInstances(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options, const MI_Char *NameSpace,
                         const MI_Char *QueryDialect, const MI_Char *QueryExpression,
                         MI_OperationCallbacks *Callbacks, MI_Operation *Operation)
{
    MI_Result Result = MI_RESULT_OK;

    if (!QueryExpression)
        Result = MI_RESULT_INVALID_PARAMETER;
    else if (QueryDialect && _wcsicmp(QueryDialect, L"WQL"))
        Result = MI_RESULT_QUERY_LANGUAGE_NOT_SUPPORTED;
    MiOperationStart(Session, Result == MI_RESULT_OK ? MiOperationQuery : MiOperationComplete, NameSpace,
                     QueryExpression, Result, Callbacks, Operation);
}

static void MI_CALL
MiSession_AssociatorInstances(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options,
                              const MI_Char *NameSpace, const MI_Instance *InstanceKeys, const MI_Char *AssocClass,
                              const MI_Char *ResultClass, const MI_Char *Role, const MI_Char *ResultRole,
                              MI_Boolean KeysOnly, MI_OperationCallbacks *Callbacks, MI_Operation *Operation)
{
    /* TODO: association traversal */
    MiOperationStart(Session, MiOperationComplete, NameSpace, NULL, MI_RESULT_NOT_SUPPORTED, Callbacks, Operation);
}

static void MI_CALL
MiSession_ReferenceInstances(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options,
                             const MI_Char *NameSpace, const MI_Instance *InstanceKeys, const MI_Char *ResultClass,
                             const MI_Char *Role, MI_Boolean KeysOnly, MI_OperationCallbacks *Callbacks,
                             MI_Operation *Operation)
{
    /* TODO: reference traversal */
    MiOperationStart(Session, MiOperationComplete, NameSpace, NULL, MI_RESULT_NOT_SUPPORTED, Callbacks, Operation);
}

static void MI_CALL
MiSession_Subscribe(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options, const MI_Char *NameSpace,
                    const MI_Char *QueryDialect, const MI_Char *QueryExpression,
                    const MI_SubscriptionDeliveryOptions *DeliverOptions, MI_OperationCallbacks *Callbacks,
                    MI_Operation *Operation)
{
    /* TODO: indication subscriptions */
    MiOperationStart(Session, MiOperationComplete, NameSpace, NULL, MI_RESULT_NOT_SUPPORTED, Callbacks, Operation);
}

static void MI_CALL
MiSession_GetClass(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options, const MI_Char *NameSpace,
                   const MI_Char *ClassName, MI_OperationCallbacks *Callbacks, MI_Operation *Operation)
{
    /* TODO: class objects */
    MiOperationStart(Session, MiOperationClass, NameSpace, NULL, MI_RESULT_NOT_SUPPORTED, Callbacks, Operation);
}

static void MI_CALL
MiSession_EnumerateClasses(MI_Session *Session, MI_Uint32 Flags, MI_OperationOptions *Options,
                           const MI_Char *NameSpace, const MI_Char *ClassName, MI_Boolean ClassNamesOnly,
                           MI_OperationCallbacks *Callbacks, MI_Operation *Operation)
{
    /* TODO: class enumeration */
    MiOperationStart(Session, MiOperationClass, NameSpace, NULL, MI_RESULT_NOT_SUPPORTED, Callbacks, Operation);
}

static void MI_CALL
MiSession_TestConnection(MI_Session *Session, MI_Uint32 Flags, MI_OperationCallbacks *Callbacks,
                         MI_Operation *Operation)
{
    MiOperationStart(Session, MiOperationComplete, NULL, NULL, MI_RESULT_OK, Callbacks, Operation);
}

static const MI_SessionFT SessionFT =
{
    MiSession_Close,
    MiSession_GetApplication,
    MiSession_GetInstance,
    MiSession_Unsupported,
    MiSession_Unsupported,
    MiSession_Unsupported,
    MiSession_Invoke,
    MiSession_EnumerateInstances,
    MiSession_QueryInstances,
    MiSession_AssociatorInstances,
    MiSession_ReferenceInstances,
    MiSession_Subscribe,
    MiSession_GetClass,
    MiSession_EnumerateClasses,
    MiSession_TestConnection,
};

static MI_OPTIONS_IMPL *
MiOptionsCreate(void)
{
    return calloc(1, sizeof(MI_OPTIONS_IMPL));
}

static void
MiOptionsFree(MI_OPTIONS_IMPL *Options)
{
    MI_Uint32 i;

    if (!Options)
        return;
    for (i = 0; i < Options->Count; i++)
    {
        MiValueFree(Options->Options[i].Type, &Options->Options[i].Value);
        free(Options->Options[i].Name);
    }
    free(Options->Options);
    free(Options);
}

static MI_Result
MiOptionsSet(MI_OPTIONS_IMPL *Options, const MI_Char *Name, MI_Type Type, const MI_Value *Value)
{
    MI_OPTION *Option = NULL, *Grown;
    MI_Value Copy;
    MI_Uint32 i;
    MI_Result Result;

    if (!Options || !Name)
        return MI_RESULT_INVALID_PARAMETER;
    if ((Result = MiValueCopy(Type, Value, &Copy)) != MI_RESULT_OK)
        return Result;
    for (i = 0; i < Options->Count; i++)
    {
        if (!_wcsicmp(Options->Options[i].Name, Name))
            Option = &Options->Options[i];
    }
    if (!Option)
    {
        Grown = realloc(Options->Options, (Options->Count + 1) * sizeof(*Grown));
        if (!Grown)
        {
            MiValueFree(Type, &Copy);
            return MI_RESULT_SERVER_LIMITS_EXCEEDED;
        }
        Options->Options = Grown;
        Option = &Options->Options[Options->Count];
        if (!(Option->Name = MiStrDup(Name)))
        {
            MiValueFree(Type, &Copy);
            return MI_RESULT_SERVER_LIMITS_EXCEEDED;
        }
        Options->Count++;
    }
    else
        MiValueFree(Option->Type, &Option->Value);
    Option->Type = Type;
    Option->Value = Copy;
    return MI_RESULT_OK;
}

static MI_Result
MiOptionsGet(MI_OPTIONS_IMPL *Options, const MI_Char *Name, MI_Type Type, MI_Value *Value, MI_Uint32 *Index)
{
    MI_Uint32 i;

    if (!Options || !Name)
        return MI_RESULT_INVALID_PARAMETER;
    for (i = 0; i < Options->Count; i++)
    {
        if (!_wcsicmp(Options->Options[i].Name, Name))
        {
            if (Type != (MI_Type)-1 && Options->Options[i].Type != Type)
                return MI_RESULT_TYPE_MISMATCH;
            if (Value)
                *Value = Options->Options[i].Value;
            if (Index)
                *Index = i;
            return MI_RESULT_OK;
        }
    }
    return MI_RESULT_NO_SUCH_PROPERTY;
}

static MI_Result
MiOptionsClone(MI_OPTIONS_IMPL *Source, MI_OPTIONS_IMPL **Copy)
{
    MI_OPTIONS_IMPL *Options = MiOptionsCreate();
    MI_Uint32 i;

    *Copy = NULL;
    if (!Options)
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    for (i = 0; Source && i < Source->Count; i++)
    {
        if (MiOptionsSet(Options, Source->Options[i].Name, Source->Options[i].Type,
                         &Source->Options[i].Value) != MI_RESULT_OK)
        {
            MiOptionsFree(Options);
            return MI_RESULT_SERVER_LIMITS_EXCEEDED;
        }
    }
    Options->Credentials = Source ? Source->Credentials : 0;
    *Copy = Options;
    return MI_RESULT_OK;
}

#define OPTIONS_IMPL(Handle) ((Handle) ? (MI_OPTIONS_IMPL *)(Handle)->reserved2 : NULL)

static MI_Result
MiOptionsGetAt(MI_OPTIONS_IMPL *Options, MI_Uint32 Index, const MI_Char **Name, MI_Value *Value, MI_Type *Type,
               MI_Uint32 *Flags)
{
    if (!Options)
        return MI_RESULT_INVALID_PARAMETER;
    if (Index >= Options->Count)
        return MI_RESULT_NOT_FOUND;
    if (Name)
        *Name = Options->Options[Index].Name;
    if (Value)
        *Value = Options->Options[Index].Value;
    if (Type)
        *Type = Options->Options[Index].Type;
    if (Flags)
        *Flags = 0;
    return MI_RESULT_OK;
}

static MI_Result
MiOptionsGetTyped(MI_OPTIONS_IMPL *Options, const MI_Char *Name, MI_Type Type, MI_Value *Value, MI_Uint32 *Index,
                  MI_Uint32 *Flags)
{
    if (Flags)
        *Flags = 0;
    return MiOptionsGet(Options, Name, Type, Value, Index);
}

static MI_Result
MiOptionsSetString(MI_OPTIONS_IMPL *Options, const MI_Char *Name, const MI_Char *Text)
{
    MI_Value Value;

    Value.string = (MI_Char *)Text;
    return MiOptionsSet(Options, Name, MI_STRING, &Value);
}

static MI_Result
MiOptionsSetNumber(MI_OPTIONS_IMPL *Options, const MI_Char *Name, MI_Uint32 Number)
{
    MI_Value Value;

    Value.uint32 = Number;
    return MiOptionsSet(Options, Name, MI_UINT32, &Value);
}

static MI_Result
MiOptionsSetInterval(MI_OPTIONS_IMPL *Options, const MI_Char *Name, const MI_Interval *Interval)
{
    MI_Value Value;

    if (!Interval)
        return MI_RESULT_INVALID_PARAMETER;
    memset(&Value, 0, sizeof(Value));
    Value.datetime.isTimestamp = 0;
    Value.datetime.u.interval = *Interval;
    return MiOptionsSet(Options, Name, MI_DATETIME, &Value);
}

static MI_Result
MiOptionsGetString(MI_OPTIONS_IMPL *Options, const MI_Char *Name, const MI_Char **Text, MI_Uint32 *Index,
                   MI_Uint32 *Flags)
{
    MI_Value Value;
    MI_Result Result = MiOptionsGetTyped(Options, Name, MI_STRING, &Value, Index, Flags);

    if (Result == MI_RESULT_OK && Text)
        *Text = Value.string;
    return Result;
}

static MI_Result
MiOptionsGetNumber(MI_OPTIONS_IMPL *Options, const MI_Char *Name, MI_Uint32 *Number, MI_Uint32 *Index,
                   MI_Uint32 *Flags)
{
    MI_Value Value;
    MI_Result Result = MiOptionsGetTyped(Options, Name, MI_UINT32, &Value, Index, Flags);

    if (Result == MI_RESULT_OK && Number)
        *Number = Value.uint32;
    return Result;
}

static MI_Result
MiOptionsGetInterval(MI_OPTIONS_IMPL *Options, const MI_Char *Name, MI_Interval *Interval, MI_Uint32 *Index,
                     MI_Uint32 *Flags)
{
    MI_Value Value;
    MI_Result Result = MiOptionsGetTyped(Options, Name, MI_DATETIME, &Value, Index, Flags);

    if (Result == MI_RESULT_OK && Interval)
        *Interval = Value.datetime.u.interval;
    return Result;
}

static MI_Result
MiOptionsCount(MI_OPTIONS_IMPL *Options, MI_Uint32 *Count)
{
    if (!Options || !Count)
        return MI_RESULT_INVALID_PARAMETER;
    *Count = Options->Count;
    return MI_RESULT_OK;
}

static void MI_CALL
MiDest_Delete(MI_DestinationOptions *Options)
{
    if (!Options)
        return;
    MiOptionsFree(OPTIONS_IMPL(Options));
    memset(Options, 0, sizeof(*Options));
}

static MI_Result MI_CALL
MiDest_SetString(MI_DestinationOptions *Options, const MI_Char *Name, const MI_Char *Value, MI_Uint32 Flags)
{
    return MiOptionsSetString(OPTIONS_IMPL(Options), Name, Value);
}

static MI_Result MI_CALL
MiDest_SetNumber(MI_DestinationOptions *Options, const MI_Char *Name, MI_Uint32 Value, MI_Uint32 Flags)
{
    return MiOptionsSetNumber(OPTIONS_IMPL(Options), Name, Value);
}

static MI_Result MI_CALL
MiDest_AddCredentials(MI_DestinationOptions *Options, const MI_Char *Name, const MI_UserCredentials *Credentials,
                      MI_Uint32 Flags)
{
    MI_OPTIONS_IMPL *Impl = OPTIONS_IMPL(Options);

    if (!Impl || !Credentials)
        return MI_RESULT_INVALID_PARAMETER;
    /* TODO: keep the credentials for remote sessions; local sessions run as the caller */
    Impl->Credentials++;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiDest_GetString(MI_DestinationOptions *Options, const MI_Char *Name, const MI_Char **Value, MI_Uint32 *Index,
                 MI_Uint32 *Flags)
{
    return MiOptionsGetString(OPTIONS_IMPL(Options), Name, Value, Index, Flags);
}

static MI_Result MI_CALL
MiDest_GetNumber(MI_DestinationOptions *Options, const MI_Char *Name, MI_Uint32 *Value, MI_Uint32 *Index,
                 MI_Uint32 *Flags)
{
    return MiOptionsGetNumber(OPTIONS_IMPL(Options), Name, Value, Index, Flags);
}

static MI_Result MI_CALL
MiDest_GetOptionCount(MI_DestinationOptions *Options, MI_Uint32 *Count)
{
    return MiOptionsCount(OPTIONS_IMPL(Options), Count);
}

static MI_Result MI_CALL
MiDest_GetOptionAt(MI_DestinationOptions *Options, MI_Uint32 Index, const MI_Char **Name, MI_Value *Value,
                   MI_Type *Type, MI_Uint32 *Flags)
{
    return MiOptionsGetAt(OPTIONS_IMPL(Options), Index, Name, Value, Type, Flags);
}

static MI_Result MI_CALL
MiDest_GetOption(MI_DestinationOptions *Options, const MI_Char *Name, MI_Value *Value, MI_Type *Type,
                 MI_Uint32 *Index, MI_Uint32 *Flags)
{
    MI_OPTIONS_IMPL *Impl = OPTIONS_IMPL(Options);
    MI_Uint32 i;
    MI_Result Result = MiOptionsGetTyped(Impl, Name, (MI_Type)-1, Value, &i, Flags);

    if (Result == MI_RESULT_OK && Type)
        *Type = Impl->Options[i].Type;
    if (Result == MI_RESULT_OK && Index)
        *Index = i;
    return Result;
}

static MI_Result MI_CALL
MiDest_GetCredentialsCount(MI_DestinationOptions *Options, MI_Uint32 *Count)
{
    MI_OPTIONS_IMPL *Impl = OPTIONS_IMPL(Options);

    if (!Impl || !Count)
        return MI_RESULT_INVALID_PARAMETER;
    *Count = Impl->Credentials;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiDest_GetCredentialsAt(MI_DestinationOptions *Options, MI_Uint32 Index, const MI_Char **Name,
                        MI_UserCredentials *Credentials, MI_Uint32 *Flags)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiDest_GetCredentialsPasswordAt(MI_DestinationOptions *Options, MI_Uint32 Index, const MI_Char **Name,
                                MI_Char *Password, MI_Uint32 BufferLength, MI_Uint32 *PasswordLength,
                                MI_Uint32 *Flags)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static const MI_DestinationOptionsFT DestinationOptionsFT;

static MI_Result MI_CALL
MiDest_Clone(const MI_DestinationOptions *Self, MI_DestinationOptions *NewOptions)
{
    MI_OPTIONS_IMPL *Copy;
    MI_Result Result;

    if (!Self || !NewOptions)
        return MI_RESULT_INVALID_PARAMETER;
    if ((Result = MiOptionsClone(OPTIONS_IMPL(Self), &Copy)) != MI_RESULT_OK)
        return Result;
    NewOptions->reserved1 = 1;
    NewOptions->reserved2 = (ptrdiff_t)Copy;
    NewOptions->ft = &DestinationOptionsFT;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiDest_SetInterval(MI_DestinationOptions *Options, const MI_Char *Name, const MI_Interval *Value, MI_Uint32 Flags)
{
    return MiOptionsSetInterval(OPTIONS_IMPL(Options), Name, Value);
}

static MI_Result MI_CALL
MiDest_GetInterval(MI_DestinationOptions *Options, const MI_Char *Name, MI_Interval *Value, MI_Uint32 *Index,
                   MI_Uint32 *Flags)
{
    return MiOptionsGetInterval(OPTIONS_IMPL(Options), Name, Value, Index, Flags);
}

static const MI_DestinationOptionsFT DestinationOptionsFT =
{
    MiDest_Delete,
    MiDest_SetString,
    MiDest_SetNumber,
    MiDest_AddCredentials,
    MiDest_GetString,
    MiDest_GetNumber,
    MiDest_GetOptionCount,
    MiDest_GetOptionAt,
    MiDest_GetOption,
    MiDest_GetCredentialsCount,
    MiDest_GetCredentialsAt,
    MiDest_GetCredentialsPasswordAt,
    MiDest_Clone,
    MiDest_SetInterval,
    MiDest_GetInterval,
};

static void MI_CALL
MiOpt_Delete(MI_OperationOptions *Options)
{
    if (!Options)
        return;
    MiOptionsFree(OPTIONS_IMPL(Options));
    memset(Options, 0, sizeof(*Options));
}

static MI_Result MI_CALL
MiOpt_SetString(MI_OperationOptions *Options, const MI_Char *Name, const MI_Char *Value, MI_Uint32 Flags)
{
    return MiOptionsSetString(OPTIONS_IMPL(Options), Name, Value);
}

static MI_Result MI_CALL
MiOpt_SetNumber(MI_OperationOptions *Options, const MI_Char *Name, MI_Uint32 Value, MI_Uint32 Flags)
{
    return MiOptionsSetNumber(OPTIONS_IMPL(Options), Name, Value);
}

static MI_Result MI_CALL
MiOpt_SetCustomOption(MI_OperationOptions *Options, const MI_Char *Name, MI_Type ValueType, const MI_Value *Value,
                      MI_Boolean MustComply, MI_Uint32 Flags)
{
    if (!Value || (ValueType & ~31))
        return MI_RESULT_INVALID_PARAMETER;
    return MiOptionsSet(OPTIONS_IMPL(Options), Name, ValueType, Value);
}

static MI_Result MI_CALL
MiOpt_GetString(MI_OperationOptions *Options, const MI_Char *Name, const MI_Char **Value, MI_Uint32 *Index,
                MI_Uint32 *Flags)
{
    return MiOptionsGetString(OPTIONS_IMPL(Options), Name, Value, Index, Flags);
}

static MI_Result MI_CALL
MiOpt_GetNumber(MI_OperationOptions *Options, const MI_Char *Name, MI_Uint32 *Value, MI_Uint32 *Index,
                MI_Uint32 *Flags)
{
    return MiOptionsGetNumber(OPTIONS_IMPL(Options), Name, Value, Index, Flags);
}

static MI_Result MI_CALL
MiOpt_GetOptionCount(MI_OperationOptions *Options, MI_Uint32 *Count)
{
    return MiOptionsCount(OPTIONS_IMPL(Options), Count);
}

static MI_Result MI_CALL
MiOpt_GetOptionAt(MI_OperationOptions *Options, MI_Uint32 Index, const MI_Char **Name, MI_Value *Value,
                  MI_Type *Type, MI_Uint32 *Flags)
{
    return MiOptionsGetAt(OPTIONS_IMPL(Options), Index, Name, Value, Type, Flags);
}

static MI_Result MI_CALL
MiOpt_GetOption(MI_OperationOptions *Options, const MI_Char *Name, MI_Value *Value, MI_Type *Type, MI_Uint32 *Index,
                MI_Uint32 *Flags)
{
    MI_OPTIONS_IMPL *Impl = OPTIONS_IMPL(Options);
    MI_Uint32 i;
    MI_Result Result = MiOptionsGetTyped(Impl, Name, (MI_Type)-1, Value, &i, Flags);

    if (Result == MI_RESULT_OK && Type)
        *Type = Impl->Options[i].Type;
    if (Result == MI_RESULT_OK && Index)
        *Index = i;
    return Result;
}

static MI_Result MI_CALL
MiOpt_GetEnabledChannels(MI_OperationOptions *Options, const MI_Char *Name, MI_Uint32 *Channels,
                         MI_Uint32 BufferLength, MI_Uint32 *ChannelCount, MI_Uint32 *Flags)
{
    if (!ChannelCount)
        return MI_RESULT_INVALID_PARAMETER;
    *ChannelCount = 0;
    if (Flags)
        *Flags = 0;
    return MI_RESULT_OK;
}

static const MI_OperationOptionsFT OperationOptionsFT;

static MI_Result MI_CALL
MiOpt_Clone(const MI_OperationOptions *Self, MI_OperationOptions *NewOptions)
{
    MI_OPTIONS_IMPL *Copy;
    MI_Result Result;

    if (!Self || !NewOptions)
        return MI_RESULT_INVALID_PARAMETER;
    if ((Result = MiOptionsClone(OPTIONS_IMPL(Self), &Copy)) != MI_RESULT_OK)
        return Result;
    NewOptions->reserved1 = 1;
    NewOptions->reserved2 = (ptrdiff_t)Copy;
    NewOptions->ft = &OperationOptionsFT;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiOpt_SetInterval(MI_OperationOptions *Options, const MI_Char *Name, const MI_Interval *Value, MI_Uint32 Flags)
{
    return MiOptionsSetInterval(OPTIONS_IMPL(Options), Name, Value);
}

static MI_Result MI_CALL
MiOpt_GetInterval(MI_OperationOptions *Options, const MI_Char *Name, MI_Interval *Value, MI_Uint32 *Index,
                  MI_Uint32 *Flags)
{
    return MiOptionsGetInterval(OPTIONS_IMPL(Options), Name, Value, Index, Flags);
}

static const MI_OperationOptionsFT OperationOptionsFT =
{
    MiOpt_Delete,
    MiOpt_SetString,
    MiOpt_SetNumber,
    MiOpt_SetCustomOption,
    MiOpt_GetString,
    MiOpt_GetNumber,
    MiOpt_GetOptionCount,
    MiOpt_GetOptionAt,
    MiOpt_GetOption,
    MiOpt_GetEnabledChannels,
    MiOpt_Clone,
    MiOpt_SetInterval,
    MiOpt_GetInterval,
};

static MI_Result MI_CALL
MiSub_SetString(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, const MI_Char *v, MI_Uint32 f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_SetNumber(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, MI_Uint32 v, MI_Uint32 f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_SetDateTime(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, const MI_Datetime *v, MI_Uint32 f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_SetInterval(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, const MI_Interval *v, MI_Uint32 f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_AddCredentials(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, const MI_UserCredentials *c, MI_Uint32 f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_Delete(MI_SubscriptionDeliveryOptions *o)
{
    if (o)
        memset(o, 0, sizeof(*o));
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiSub_GetString(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, const MI_Char **v, MI_Uint32 *i, MI_Uint32 *f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_GetNumber(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, MI_Uint32 *v, MI_Uint32 *i, MI_Uint32 *f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_GetDateTime(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, MI_Datetime *v, MI_Uint32 *i, MI_Uint32 *f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_GetInterval(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, MI_Interval *v, MI_Uint32 *i, MI_Uint32 *f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_GetOptionCount(MI_SubscriptionDeliveryOptions *o, MI_Uint32 *c)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_GetOptionAt(MI_SubscriptionDeliveryOptions *o, MI_Uint32 x, const MI_Char **n, MI_Value *v, MI_Type *t,
                  MI_Uint32 *f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_GetOption(MI_SubscriptionDeliveryOptions *o, const MI_Char *n, MI_Value *v, MI_Type *t, MI_Uint32 *i,
                MI_Uint32 *f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_GetCredentialsCount(MI_SubscriptionDeliveryOptions *o, MI_Uint32 *c)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_GetCredentialsAt(MI_SubscriptionDeliveryOptions *o, MI_Uint32 x, const MI_Char **n, MI_UserCredentials *c,
                       MI_Uint32 *f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_GetCredentialsPasswordAt(MI_SubscriptionDeliveryOptions *o, MI_Uint32 x, const MI_Char **n, MI_Char *p,
                               MI_Uint32 l, MI_Uint32 *pl, MI_Uint32 *f)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSub_Clone(const MI_SubscriptionDeliveryOptions *s, MI_SubscriptionDeliveryOptions *n)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static const MI_SubscriptionDeliveryOptionsFT SubscriptionDeliveryOptionsFT =
{
    MiSub_SetString,
    MiSub_SetNumber,
    MiSub_SetDateTime,
    MiSub_SetInterval,
    MiSub_AddCredentials,
    MiSub_Delete,
    MiSub_GetString,
    MiSub_GetNumber,
    MiSub_GetDateTime,
    MiSub_GetInterval,
    MiSub_GetOptionCount,
    MiSub_GetOptionAt,
    MiSub_GetOption,
    MiSub_GetCredentialsCount,
    MiSub_GetCredentialsAt,
    MiSub_GetCredentialsPasswordAt,
    MiSub_Clone,
};

static MI_Result MI_CALL
MiSer_Close(MI_Serializer *s)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSer_SerializeClass(MI_Serializer *s, MI_Uint32 f, const MI_Class *c, MI_Uint8 *b, MI_Uint32 l, MI_Uint32 *n)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiSer_SerializeInstance(MI_Serializer *s, MI_Uint32 f, const MI_Instance *i, MI_Uint8 *b, MI_Uint32 l, MI_Uint32 *n)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static const MI_SerializerFT SerializerFT =
{
    MiSer_Close,
    MiSer_SerializeClass,
    MiSer_SerializeInstance,
};

static MI_Result MI_CALL
MiDes_Close(MI_Deserializer *d)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiDes_DeserializeClass(MI_Deserializer *d, MI_Uint32 f, MI_Uint8 *b, MI_Uint32 l, MI_Class *p, const MI_Char *s,
                       const MI_Char *n, MI_Deserializer_ClassObjectNeeded c, void *x, MI_Uint32 *r, MI_Class **o,
                       MI_Instance **e)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiDes_Class_GetClassName(MI_Deserializer *d, MI_Uint8 *b, MI_Uint32 l, MI_Char *c, MI_Uint32 *cl, MI_Instance **e)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiDes_Class_GetParentClassName(MI_Deserializer *d, MI_Uint8 *b, MI_Uint32 l, MI_Char *c, MI_Uint32 *cl,
                               MI_Instance **e)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiDes_DeserializeInstance(MI_Deserializer *d, MI_Uint32 f, MI_Uint8 *b, MI_Uint32 l, MI_Class **c, MI_Uint32 nc,
                          MI_Deserializer_ClassObjectNeeded cb, void *x, MI_Uint32 *r, MI_Instance **i,
                          MI_Instance **e)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiDes_Instance_GetClassName(MI_Deserializer *d, MI_Uint8 *b, MI_Uint32 l, MI_Char *c, MI_Uint32 *cl,
                            MI_Instance **e)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static const MI_DeserializerFT DeserializerFT =
{
    MiDes_Close,
    MiDes_DeserializeClass,
    MiDes_Class_GetClassName,
    MiDes_Class_GetParentClassName,
    MiDes_DeserializeInstance,
    MiDes_Instance_GetClassName,
};

static MI_Result MI_CALL
MiHost_Close(MI_HostedProvider *p)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiHost_GetApplication(MI_HostedProvider *p, MI_Application *a)
{
    return MI_RESULT_NOT_SUPPORTED;
}

static const MI_HostedProviderFT HostedProviderFT =
{
    MiHost_Close,
    MiHost_GetApplication,
};

static MI_ErrorCategory MI_CALL
MiUtil_MapErrorToMiErrorCategory(MI_Char *ErrorType, MI_Uint32 Error)
{
    /* TODO: map error codes to error categories */
    return MI_ERRORCATEGORY_NOT_SPECIFIED;
}

static MI_Result MI_CALL
MiUtil_CimErrorFromErrorCode(MI_Uint32 Error, const MI_Char *ErrorType, const MI_Char *ErrorMessage,
                             MI_Instance **CimError)
{
    /* TODO: build CIM_Error instances */
    if (CimError)
        *CimError = NULL;
    return MI_RESULT_NOT_SUPPORTED;
}

static const MI_UtilitiesFT UtilitiesFT =
{
    MiUtil_MapErrorToMiErrorCategory,
    MiUtil_CimErrorFromErrorCode,
};

static MI_APPLICATION_IMPL *
MiApplicationFromHandle(MI_Application *Application)
{
    if (!Application || Application->ft != &ApplicationFT || Application->reserved1 != MI_APPLICATION_MAGIC)
        return NULL;
    return (MI_APPLICATION_IMPL *)Application->reserved2;
}

static MI_Result MI_CALL
MiApp_Close(MI_Application *Application)
{
    MI_APPLICATION_IMPL *App = MiApplicationFromHandle(Application);

    if (!App)
        return MI_RESULT_INVALID_PARAMETER;
    free(App);
    memset(Application, 0, sizeof(*Application));
    return MI_RESULT_OK;
}

static BOOL
MiIsLocalDestination(const MI_Char *Destination)
{
    WCHAR Name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD Size = ARRAYSIZE(Name);

    if (!Destination || !*Destination || !wcscmp(Destination, L".") || !_wcsicmp(Destination, L"localhost"))
        return TRUE;
    return GetComputerNameW(Name, &Size) && !_wcsicmp(Destination, Name);
}

static MI_Result MI_CALL
MiApp_NewSession(MI_Application *Application, const MI_Char *Protocol, const MI_Char *Destination,
                 MI_DestinationOptions *Options, MI_SessionCallbacks *Callbacks, MI_Instance **ExtendedError,
                 MI_Session *Session)
{
    MI_SESSION_IMPL *Impl;

    if (ExtendedError)
        *ExtendedError = NULL;
    if (!MiApplicationFromHandle(Application) || !Session)
        return MI_RESULT_INVALID_PARAMETER;
    memset(Session, 0, sizeof(*Session));
    if (Protocol && _wcsicmp(Protocol, L"WMIDCOM") && _wcsicmp(Protocol, L"WINRM"))
        return MI_RESULT_NOT_SUPPORTED;
    /* TODO: sessions to other computers */
    if (!MiIsLocalDestination(Destination))
        return MI_RESULT_NOT_SUPPORTED;
    if (!(Impl = calloc(1, sizeof(*Impl))))
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    Impl->Application = *Application;
    Session->reserved1 = 1;
    Session->reserved2 = (ptrdiff_t)Impl;
    Session->ft = &SessionFT;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiApp_NewHostedProvider(MI_Application *Application, const MI_Char *NameSpace, const MI_Char *ProviderName,
                        MI_MainFunction Main, MI_Instance **ExtendedError, MI_HostedProvider *Provider)
{
    if (ExtendedError)
        *ExtendedError = NULL;
    /* TODO: hosted providers */
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiApp_NewInstance(MI_Application *Application, const MI_Char *ClassName, const MI_ClassDecl *ClassRTTI,
                  MI_Instance **Instance)
{
    MI_INSTANCE_IMPL *Impl;

    if (!MiApplicationFromHandle(Application) || !ClassName || !Instance)
        return MI_RESULT_INVALID_PARAMETER;
    if (!(Impl = MiInstanceCreate(ClassName)))
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    /* FIXME: the properties of ClassRTTI are not added to the new instance */
    Impl->Base.classDecl = ClassRTTI;
    *Instance = &Impl->Base;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiApp_NewDestinationOptions(MI_Application *Application, MI_DestinationOptions *Options)
{
    MI_OPTIONS_IMPL *Impl;

    if (!MiApplicationFromHandle(Application) || !Options)
        return MI_RESULT_INVALID_PARAMETER;
    if (!(Impl = MiOptionsCreate()))
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    Options->reserved1 = 1;
    Options->reserved2 = (ptrdiff_t)Impl;
    Options->ft = &DestinationOptionsFT;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiApp_NewOperationOptions(MI_Application *Application, MI_Boolean CustomOptionsMustUnderstand,
                          MI_OperationOptions *Options)
{
    MI_OPTIONS_IMPL *Impl;

    if (!MiApplicationFromHandle(Application) || !Options)
        return MI_RESULT_INVALID_PARAMETER;
    if (!(Impl = MiOptionsCreate()))
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    Options->reserved1 = 1;
    Options->reserved2 = (ptrdiff_t)Impl;
    Options->ft = &OperationOptionsFT;
    return MI_RESULT_OK;
}

static MI_Result MI_CALL
MiApp_NewSubscriptionDeliveryOptions(MI_Application *Application, MI_SubscriptionDeliveryType DeliveryType,
                                     MI_SubscriptionDeliveryOptions *DeliveryOptions)
{
    /* TODO: subscription delivery options */
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiApp_NewSerializer(MI_Application *Application, MI_Uint32 Flags, MI_Char *Format, MI_Serializer *Serializer)
{
    /* TODO: instance and class serialization */
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiApp_NewDeserializer(MI_Application *Application, MI_Uint32 Flags, MI_Char *Format, MI_Deserializer *Deserializer)
{
    /* TODO: instance and class deserialization */
    return MI_RESULT_NOT_SUPPORTED;
}

static MI_Result MI_CALL
MiApp_NewInstanceFromClass(MI_Application *Application, const MI_Char *ClassName, const MI_Class *ClassObject,
                           MI_Instance **Instance)
{
    return MiApp_NewInstance(Application, ClassName, NULL, Instance);
}

static MI_Result MI_CALL
MiApp_NewClass(MI_Application *Application, const MI_ClassDecl *ClassDecl, const MI_Char *NameSpace,
               const MI_Char *ServerName, MI_Class **ClassObject)
{
    /* TODO: class objects */
    return MI_RESULT_NOT_SUPPORTED;
}

static const MI_ApplicationFT ApplicationFT =
{
    MiApp_Close,
    MiApp_NewSession,
    MiApp_NewHostedProvider,
    MiApp_NewInstance,
    MiApp_NewDestinationOptions,
    MiApp_NewOperationOptions,
    MiApp_NewSubscriptionDeliveryOptions,
    MiApp_NewSerializer,
    MiApp_NewDeserializer,
    MiApp_NewInstanceFromClass,
    MiApp_NewClass,
};

static const MI_ClientFT_V1 ClientFT =
{
    &ApplicationFT,
    &SessionFT,
    &OperationFT,
    &HostedProviderFT,
    &SerializerFT,
    &DeserializerFT,
    &SubscriptionDeliveryOptionsFT,
    &DestinationOptionsFT,
    &OperationOptionsFT,
    &UtilitiesFT,
};

const MI_ClientFT_V1 *mi_clientFT_V1 = &ClientFT;

MI_Result MI_MAIN_CALL
MI_Application_InitializeV1(MI_Uint32 Flags, const MI_Char *ApplicationID, MI_Instance **ExtendedError,
                            MI_Application *Application)
{
    MI_APPLICATION_IMPL *App;

    if (ExtendedError)
        *ExtendedError = NULL;
    if (!Application || Flags)
        return MI_RESULT_INVALID_PARAMETER;
    memset(Application, 0, sizeof(*Application));
    if (!(App = calloc(1, sizeof(*App))))
        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
    Application->reserved1 = MI_APPLICATION_MAGIC;
    Application->reserved2 = (ptrdiff_t)App;
    Application->ft = &ApplicationFT;
    return MI_RESULT_OK;
}

C_ASSERT(sizeof(MI_Datetime) == 36);
C_ASSERT(sizeof(MI_Value) == 40);
C_ASSERT(FIELD_OFFSET(MI_Instance, reserved) == 4 * sizeof(void *));
C_ASSERT(sizeof(MI_Application) == 8 + 2 * sizeof(void *));
C_ASSERT(sizeof(MI_ClientFT_V1) == 10 * sizeof(void *));
C_ASSERT(sizeof(MI_SessionFT) == 15 * sizeof(void *));
C_ASSERT(sizeof(MI_InstanceFT) == 18 * sizeof(void *));
C_ASSERT(sizeof(MI_ApplicationFT) == 11 * sizeof(void *));
C_ASSERT(sizeof(MI_OperationFT) == 6 * sizeof(void *));
C_ASSERT(sizeof(MI_DestinationOptionsFT) == 15 * sizeof(void *));
C_ASSERT(sizeof(MI_OperationOptionsFT) == 13 * sizeof(void *));
C_ASSERT(sizeof(MI_SubscriptionDeliveryOptionsFT) == 17 * sizeof(void *));
C_ASSERT(sizeof(MI_OperationCallbacks) == 9 * sizeof(void *));
