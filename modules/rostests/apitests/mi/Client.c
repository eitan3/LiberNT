/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the Management Infrastructure client: applications, sessions, options, instances, queries
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include "../../../../dll/win32/mi/mi_private.h"

typedef MI_Result (MI_MAIN_CALL *PFN_MI_INITIALIZE)(MI_Uint32, const MI_Char *, MI_Instance **, MI_Application *);

static HANDLE CallbackDone;
static MI_Uint32 CallbackInstances;
static MI_Result CallbackResult;

static
void
MI_CALL
InstanceCallback(MI_Operation *Operation, void *Context, const MI_Instance *Instance, MI_Boolean MoreResults,
                 MI_Result ResultCode, const MI_Char *ErrorString, const MI_Instance *ErrorDetails,
                 MI_Result (MI_CALL *ResultAcknowledgement)(MI_Operation *))
{
    ok(Context == (void *)0x1234, "context %p\n", Context);
    if (Instance)
        CallbackInstances++;
    if (!MoreResults)
    {
        CallbackResult = ResultCode;
        SetEvent(CallbackDone);
    }
    if (ResultAcknowledgement)
        ResultAcknowledgement(Operation);
}

static
VOID
TestInstance(MI_Application *App)
{
    MI_Instance *Instance = NULL, *Copy = NULL;
    MI_Value Value, Out;
    MI_Type Type;
    MI_Uint32 Flags, Count, Index;
    const MI_Char *Name;
    MI_Result Result;

    Result = App->ft->NewInstance(App, L"Test_Class", NULL, &Instance);
    ok_long(Result, MI_RESULT_OK);
    if (!Instance)
        return;
    ok(Instance->ft->GetClassName(Instance, &Name) == MI_RESULT_OK && !wcscmp(Name, L"Test_Class"), "class name\n");
    Value.string = (MI_Char *)L"text";
    ok_long(Instance->ft->AddElement(Instance, L"Text", &Value, MI_STRING, 0), MI_RESULT_OK);
    ok_long(Instance->ft->AddElement(Instance, L"Text", &Value, MI_STRING, 0), MI_RESULT_ALREADY_EXISTS);
    Value.uint32 = 42;
    ok_long(Instance->ft->AddElement(Instance, L"Number", &Value, MI_UINT32, 0), MI_RESULT_OK);
    ok_long(Instance->ft->AddElement(Instance, L"Nothing", NULL, MI_UINT64, 0), MI_RESULT_OK);
    ok(Instance->ft->GetElementCount(Instance, &Count) == MI_RESULT_OK && Count == 3, "count %u\n", Count);

    Result = Instance->ft->GetElement(Instance, L"number", &Out, &Type, &Flags, &Index);
    ok_long(Result, MI_RESULT_OK);
    ok(Out.uint32 == 42 && Type == MI_UINT32 && Index == 1 && !(Flags & MI_FLAG_NULL), "number element\n");
    Result = Instance->ft->GetElement(Instance, L"Nothing", &Out, &Type, &Flags, NULL);
    ok(Result == MI_RESULT_OK && Type == MI_UINT64 && (Flags & MI_FLAG_NULL), "null element flags %x\n", Flags);
    ok_long(Instance->ft->GetElement(Instance, L"Missing", &Out, &Type, &Flags, NULL), MI_RESULT_NO_SUCH_PROPERTY);
    ok_long(Instance->ft->GetElementAt(Instance, 3, &Name, &Out, &Type, &Flags), MI_RESULT_NOT_FOUND);

    Value.uint32 = 7;
    ok_long(Instance->ft->SetElement(Instance, L"Number", &Value, MI_UINT32, 0), MI_RESULT_OK);
    ok_long(Instance->ft->SetElement(Instance, L"Number", &Value, MI_STRING, 0), MI_RESULT_TYPE_MISMATCH);
    ok_long(Instance->ft->SetElement(Instance, L"Missing", &Value, MI_UINT32, 0), MI_RESULT_NO_SUCH_PROPERTY);

    ok_long(Instance->ft->Clone(Instance, &Copy), MI_RESULT_OK);
    if (Copy)
    {
        Result = Copy->ft->GetElementAt(Copy, 0, &Name, &Out, &Type, &Flags);
        ok(Result == MI_RESULT_OK && !wcscmp(Name, L"Text") && Type == MI_STRING && !wcscmp(Out.string, L"text"),
           "cloned text element\n");
        ok(Out.string != NULL, "string\n");
        Result = Copy->ft->GetElement(Copy, L"Number", &Out, NULL, NULL, NULL);
        ok(Result == MI_RESULT_OK && Out.uint32 == 7, "cloned number %u\n", Out.uint32);
        ok_long(Copy->ft->ClearElement(Copy, L"Text"), MI_RESULT_OK);
        Result = Copy->ft->GetElement(Copy, L"Text", &Out, &Type, &Flags, NULL);
        ok(Result == MI_RESULT_OK && (Flags & MI_FLAG_NULL), "cleared flags %x\n", Flags);
        ok_long(Copy->ft->Delete(Copy), MI_RESULT_OK);
    }
    Result = Instance->ft->GetElement(Instance, L"Text", &Out, &Type, &Flags, NULL);
    ok(Result == MI_RESULT_OK && !(Flags & MI_FLAG_NULL) && !wcscmp(Out.string, L"text"), "original unchanged\n");
    ok_long(Instance->ft->Delete(Instance), MI_RESULT_OK);
}

static
VOID
TestOptions(MI_Application *App)
{
    MI_OperationOptions Options = { 0 };
    MI_DestinationOptions Destination = { 0 };
    const MI_Char *Text;
    MI_Uint32 Number, Count;
    MI_Result Result;

    ok_long(App->ft->NewOperationOptions(App, MI_FALSE, &Options), MI_RESULT_OK);
    ok(Options.ft != NULL, "no operation options table\n");
    if (Options.ft)
    {
        ok_long(Options.ft->SetString(&Options, L"Test_Text", L"value", 0), MI_RESULT_OK);
        ok_long(Options.ft->SetNumber(&Options, L"Test_Number", 5, 0), MI_RESULT_OK);
        Result = Options.ft->GetString(&Options, L"Test_Text", &Text, NULL, NULL);
        ok(Result == MI_RESULT_OK && !wcscmp(Text, L"value"), "string option %d\n", Result);
        Result = Options.ft->GetNumber(&Options, L"Test_Number", &Number, NULL, NULL);
        ok(Result == MI_RESULT_OK && Number == 5, "number option %d\n", Result);
        ok_long(Options.ft->GetNumber(&Options, L"Test_Missing", &Number, NULL, NULL), MI_RESULT_NO_SUCH_PROPERTY);
        Options.ft->Delete(&Options);
    }
    ok_long(App->ft->NewDestinationOptions(App, &Destination), MI_RESULT_OK);
    if (Destination.ft)
    {
        ok_long(Destination.ft->GetOptionCount(&Destination, &Count), MI_RESULT_OK);
        Destination.ft->Delete(&Destination);
    }
}

static
VOID
TestQueries(MI_Application *App)
{
    MI_Session Session = { 0 };
    MI_Operation Operation = { 0 };
    MI_OperationCallbacks Callbacks = { 0 };
    const MI_Instance *Instance;
    MI_Boolean More;
    MI_Result Result, Final;
    const MI_Char *Error;
    MI_Uint32 Pulled = 0;

    ok_long(App->ft->NewSession(App, NULL, NULL, NULL, NULL, NULL, &Session), MI_RESULT_OK);
    if (!Session.ft)
        return;

    Session.ft->QueryInstances(&Session, 0, NULL, L"root\\cimv2", L"NotAQueryLanguage", L"SELECT * FROM Win32_Processor",
                               NULL, &Operation);
    ok(Operation.ft != NULL, "no operation\n");
    if (Operation.ft)
    {
        Result = Operation.ft->GetInstance(&Operation, &Instance, &More, &Final, &Error, NULL);
        ok(Result == MI_RESULT_OK && !Instance && !More, "unknown dialect result %d more %d\n", Result, More);
        ok_long(Final, MI_RESULT_QUERY_LANGUAGE_NOT_SUPPORTED);
        ok_long(Operation.ft->Close(&Operation), MI_RESULT_OK);
    }

    Session.ft->QueryInstances(&Session, 0, NULL, L"root/cimv2", L"WQL", L"SELECT * FROM Win32_OperatingSystem", NULL,
                               &Operation);
    for (More = MI_TRUE; Operation.ft && More;)
    {
        Result = Operation.ft->GetInstance(&Operation, &Instance, &More, &Final, &Error, NULL);
        if (Result != MI_RESULT_OK)
            break;
        if (Instance)
        {
            const MI_Char *ClassName;
            MI_Value Value;
            MI_Type Type;

            Pulled++;
            ok(Instance->ft->GetClassName(Instance, &ClassName) == MI_RESULT_OK &&
               !_wcsicmp(ClassName, L"Win32_OperatingSystem"), "class\n");
            ok(Instance->ft->GetElement(Instance, L"Caption", &Value, &Type, NULL, NULL) == MI_RESULT_OK &&
               Type == MI_STRING, "caption type %d\n", Type);
        }
    }
    if (Operation.ft)
        ok_long(Operation.ft->Close(&Operation), MI_RESULT_OK);
    if (Final != MI_RESULT_OK)
    {
        skip("WMI is unavailable (%d)\n", Final);
    }
    else
    {
        ok_long(Pulled, 1);
        CallbackDone = CreateEventW(NULL, TRUE, FALSE, NULL);
        Callbacks.callbackContext = (void *)0x1234;
        Callbacks.instanceResult = InstanceCallback;
        Session.ft->EnumerateInstances(&Session, 0, NULL, NULL, L"Win32_OperatingSystem", MI_FALSE, &Callbacks,
                                       &Operation);
        ok_long(WaitForSingleObject(CallbackDone, 10000), WAIT_OBJECT_0);
        ok_long(CallbackInstances, 1);
        ok_long(CallbackResult, MI_RESULT_OK);
        if (Operation.ft)
            ok_long(Operation.ft->Close(&Operation), MI_RESULT_OK);
        CloseHandle(CallbackDone);
    }
    ok_long(Session.ft->Close(&Session, NULL, NULL), MI_RESULT_OK);
}

START_TEST(Client)
{
    HMODULE Module = LoadLibraryW(L"mi.dll");
    PFN_MI_INITIALIZE Initialize;
    const MI_ClientFT_V1 **ClientFT;
    MI_Application App = { 0 };

    ok(Module != NULL, "mi.dll not loaded %lu\n", GetLastError());
    if (!Module)
        return;
    Initialize = (PFN_MI_INITIALIZE)GetProcAddress(Module, "MI_Application_InitializeV1");
    ClientFT = (const MI_ClientFT_V1 **)GetProcAddress(Module, "mi_clientFT_V1");
    ok(Initialize != NULL && ClientFT != NULL && *ClientFT != NULL, "exports missing\n");
    if (!Initialize || !ClientFT || !*ClientFT)
        return;

    ok_long(Initialize(0, L"LiberNT test", NULL, &App), MI_RESULT_OK);
    ok(App.ft == (*ClientFT)->applicationFT, "application table %p, client table %p\n", App.ft,
       (*ClientFT)->applicationFT);
    ok((*ClientFT)->sessionFT && (*ClientFT)->operationFT && (*ClientFT)->destinationOptionsFT &&
       (*ClientFT)->operationOptionsFT, "client tables missing\n");
    if (!App.ft)
        return;
    TestInstance(&App);
    TestOptions(&App);
    TestQueries(&App);
    ok_long(App.ft->Close(&App), MI_RESULT_OK);
}
