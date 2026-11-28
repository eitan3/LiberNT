/*
 * PROJECT:     LiberNT Management Infrastructure
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Client-side Management Infrastructure types and function tables (MI_CALL_VERSION 1 ABI)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>

#define MI_CALL __stdcall
#define MI_MAIN_CALL __cdecl

typedef unsigned char MI_Boolean;
typedef unsigned char MI_Uint8;
typedef signed char MI_Sint8;
typedef unsigned short MI_Uint16;
typedef signed short MI_Sint16;
typedef unsigned int MI_Uint32;
typedef signed int MI_Sint32;
typedef unsigned long long MI_Uint64;
typedef signed long long MI_Sint64;
typedef float MI_Real32;
typedef double MI_Real64;
typedef unsigned short MI_Char16;
typedef wchar_t MI_Char;

#define MI_TRUE ((MI_Boolean)1)
#define MI_FALSE ((MI_Boolean)0)

typedef enum _MI_Result
{
    MI_RESULT_OK = 0,
    MI_RESULT_FAILED = 1,
    MI_RESULT_ACCESS_DENIED = 2,
    MI_RESULT_INVALID_NAMESPACE = 3,
    MI_RESULT_INVALID_PARAMETER = 4,
    MI_RESULT_INVALID_CLASS = 5,
    MI_RESULT_NOT_FOUND = 6,
    MI_RESULT_NOT_SUPPORTED = 7,
    MI_RESULT_CLASS_HAS_CHILDREN = 8,
    MI_RESULT_CLASS_HAS_INSTANCES = 9,
    MI_RESULT_INVALID_SUPERCLASS = 10,
    MI_RESULT_ALREADY_EXISTS = 11,
    MI_RESULT_NO_SUCH_PROPERTY = 12,
    MI_RESULT_TYPE_MISMATCH = 13,
    MI_RESULT_QUERY_LANGUAGE_NOT_SUPPORTED = 14,
    MI_RESULT_INVALID_QUERY = 15,
    MI_RESULT_METHOD_NOT_AVAILABLE = 16,
    MI_RESULT_METHOD_NOT_FOUND = 17,
    MI_RESULT_NAMESPACE_NOT_EMPTY = 20,
    MI_RESULT_INVALID_ENUMERATION_CONTEXT = 21,
    MI_RESULT_INVALID_OPERATION_TIMEOUT = 22,
    MI_RESULT_PULL_HAS_BEEN_ABANDONED = 23,
    MI_RESULT_PULL_CANNOT_BE_ABANDONED = 24,
    MI_RESULT_FILTERED_ENUMERATION_NOT_SUPPORTED = 25,
    MI_RESULT_CONTINUATION_ON_ERROR_NOT_SUPPORTED = 26,
    MI_RESULT_SERVER_LIMITS_EXCEEDED = 27,
    MI_RESULT_SERVER_IS_SHUTTING_DOWN = 28
} MI_Result;

typedef enum _MI_Type
{
    MI_BOOLEAN = 0,
    MI_UINT8 = 1,
    MI_SINT8 = 2,
    MI_UINT16 = 3,
    MI_SINT16 = 4,
    MI_UINT32 = 5,
    MI_SINT32 = 6,
    MI_UINT64 = 7,
    MI_SINT64 = 8,
    MI_REAL32 = 9,
    MI_REAL64 = 10,
    MI_CHAR16 = 11,
    MI_DATETIME = 12,
    MI_STRING = 13,
    MI_REFERENCE = 14,
    MI_INSTANCE = 15,
    MI_BOOLEANA = 16,
    MI_UINT8A = 17,
    MI_SINT8A = 18,
    MI_UINT16A = 19,
    MI_SINT16A = 20,
    MI_UINT32A = 21,
    MI_SINT32A = 22,
    MI_UINT64A = 23,
    MI_SINT64A = 24,
    MI_REAL32A = 25,
    MI_REAL64A = 26,
    MI_CHAR16A = 27,
    MI_DATETIMEA = 28,
    MI_STRINGA = 29,
    MI_REFERENCEA = 30,
    MI_INSTANCEA = 31,
    MI_ARRAY = 16
} MI_Type;

#define MI_FLAG_KEY (1 << 12)
#define MI_FLAG_READONLY (1 << 21)
#define MI_FLAG_NULL (1 << 29)
#define MI_FLAG_BORROW (1 << 30)
#define MI_FLAG_ADOPT ((MI_Uint32)(1u << 31))

typedef enum _MI_CancellationReason
{
    MI_REASON_NONE,
    MI_REASON_TIMEOUT,
    MI_REASON_SHUTDOWN,
    MI_REASON_SERVICESTOP
} MI_CancellationReason;

typedef enum _MI_SubscriptionDeliveryType
{
    MI_SubscriptionDeliveryType_Pull = 1,
    MI_SubscriptionDeliveryType_Push = 2
} MI_SubscriptionDeliveryType;

typedef enum _MI_ErrorCategory
{
    MI_ERRORCATEGORY_NOT_SPECIFIED = 0
} MI_ErrorCategory;

typedef struct _MI_Timestamp
{
    MI_Uint32 year;
    MI_Uint32 month;
    MI_Uint32 day;
    MI_Uint32 hour;
    MI_Uint32 minute;
    MI_Uint32 second;
    MI_Uint32 microseconds;
    MI_Sint32 utc;
} MI_Timestamp;

typedef struct _MI_Interval
{
    MI_Uint32 days;
    MI_Uint32 hours;
    MI_Uint32 minutes;
    MI_Uint32 seconds;
    MI_Uint32 microseconds;
    MI_Uint32 __padding1;
    MI_Uint32 __padding2;
    MI_Uint32 __padding3;
} MI_Interval;

typedef struct _MI_Datetime
{
    MI_Uint32 isTimestamp;
    union
    {
        MI_Timestamp timestamp;
        MI_Interval interval;
    } u;
} MI_Datetime;

typedef struct _MI_Instance MI_Instance;
typedef struct _MI_Class MI_Class;
typedef struct _MI_ClassDecl MI_ClassDecl;
typedef struct _MI_QualifierSet MI_QualifierSet;
typedef struct _MI_ParameterSet MI_ParameterSet;
typedef struct _MI_Application MI_Application;
typedef struct _MI_Session MI_Session;
typedef struct _MI_Operation MI_Operation;
typedef struct _MI_HostedProvider MI_HostedProvider;
typedef struct _MI_DestinationOptions MI_DestinationOptions;
typedef struct _MI_OperationOptions MI_OperationOptions;
typedef struct _MI_SubscriptionDeliveryOptions MI_SubscriptionDeliveryOptions;
typedef struct _MI_Serializer MI_Serializer;
typedef struct _MI_Deserializer MI_Deserializer;
typedef struct _MI_SessionCallbacks MI_SessionCallbacks;
typedef struct _MI_UserCredentials MI_UserCredentials;
typedef void *MI_MainFunction;

typedef struct _MI_Array
{
    void *data;
    MI_Uint32 size;
} MI_Array;

typedef union _MI_Value
{
    MI_Boolean boolean;
    MI_Uint8 uint8;
    MI_Sint8 sint8;
    MI_Uint16 uint16;
    MI_Sint16 sint16;
    MI_Uint32 uint32;
    MI_Sint32 sint32;
    MI_Uint64 uint64;
    MI_Sint64 sint64;
    MI_Real32 real32;
    MI_Real64 real64;
    MI_Char16 char16;
    MI_Datetime datetime;
    MI_Char *string;
    MI_Instance *instance;
    MI_Instance *reference;
    MI_Array array;
} MI_Value;

typedef struct _MI_InstanceFT
{
    MI_Result (MI_CALL *Clone)(const MI_Instance *self, MI_Instance **newInstance);
    MI_Result (MI_CALL *Destruct)(MI_Instance *self);
    MI_Result (MI_CALL *Delete)(MI_Instance *self);
    MI_Result (MI_CALL *IsA)(const MI_Instance *self, const MI_ClassDecl *classDecl, MI_Boolean *flag);
    MI_Result (MI_CALL *GetClassName)(const MI_Instance *self, const MI_Char **className);
    MI_Result (MI_CALL *SetNameSpace)(MI_Instance *self, const MI_Char *nameSpace);
    MI_Result (MI_CALL *GetNameSpace)(const MI_Instance *self, const MI_Char **nameSpace);
    MI_Result (MI_CALL *GetElementCount)(const MI_Instance *self, MI_Uint32 *count);
    MI_Result (MI_CALL *AddElement)(MI_Instance *self, const MI_Char *name, const MI_Value *value, MI_Type type,
                                    MI_Uint32 flags);
    MI_Result (MI_CALL *SetElement)(MI_Instance *self, const MI_Char *name, const MI_Value *value, MI_Type type,
                                    MI_Uint32 flags);
    MI_Result (MI_CALL *SetElementAt)(MI_Instance *self, MI_Uint32 index, const MI_Value *value, MI_Type type,
                                      MI_Uint32 flags);
    MI_Result (MI_CALL *GetElement)(const MI_Instance *self, const MI_Char *name, MI_Value *value, MI_Type *type,
                                    MI_Uint32 *flags, MI_Uint32 *index);
    MI_Result (MI_CALL *GetElementAt)(const MI_Instance *self, MI_Uint32 index, const MI_Char **name, MI_Value *value,
                                      MI_Type *type, MI_Uint32 *flags);
    MI_Result (MI_CALL *ClearElement)(MI_Instance *self, const MI_Char *name);
    MI_Result (MI_CALL *ClearElementAt)(MI_Instance *self, MI_Uint32 index);
    MI_Result (MI_CALL *GetServerName)(const MI_Instance *self, const MI_Char **name);
    MI_Result (MI_CALL *SetServerName)(MI_Instance *self, const MI_Char *name);
    MI_Result (MI_CALL *GetClass)(const MI_Instance *self, MI_Class **instanceClass);
} MI_InstanceFT;

typedef struct _MI_InstanceExFT
{
    MI_InstanceFT parent;
    MI_Result (MI_CALL *Normalize)(MI_Instance **self);
} MI_InstanceExFT;

struct _MI_Instance
{
    const MI_InstanceFT *ft;
    const MI_ClassDecl *classDecl;
    const MI_Char *serverName;
    const MI_Char *nameSpace;
    ptrdiff_t reserved[4];
};

typedef struct _MI_ClassFT
{
    MI_Result (MI_CALL *GetClassName)(const MI_Class *self, const MI_Char **className);
    MI_Result (MI_CALL *GetNameSpace)(const MI_Class *self, const MI_Char **nameSpace);
    MI_Result (MI_CALL *GetServerName)(const MI_Class *self, const MI_Char **serverName);
    MI_Result (MI_CALL *GetElementCount)(const MI_Class *self, MI_Uint32 *count);
    MI_Result (MI_CALL *GetElement)(const MI_Class *self, const MI_Char *name, MI_Value *value, MI_Boolean *valueExists,
                                    MI_Type *type, MI_Char **referenceClass, MI_QualifierSet *qualifierSet,
                                    MI_Uint32 *flags, MI_Uint32 *index);
    MI_Result (MI_CALL *GetElementAt)(const MI_Class *self, MI_Uint32 index, const MI_Char **name, MI_Value *value,
                                      MI_Boolean *valueExists, MI_Type *type, MI_Char **referenceClass,
                                      MI_QualifierSet *qualifierSet, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetClassQualifierSet)(const MI_Class *self, MI_QualifierSet *qualifierSet);
    MI_Result (MI_CALL *GetMethodCount)(const MI_Class *self, MI_Uint32 *count);
    MI_Result (MI_CALL *GetMethodAt)(const MI_Class *self, MI_Uint32 index, const MI_Char **name,
                                     MI_QualifierSet *qualifierSet, MI_ParameterSet *parameterSet);
    MI_Result (MI_CALL *GetMethod)(const MI_Class *self, const MI_Char *name, MI_QualifierSet *qualifierSet,
                                   MI_ParameterSet *parameterSet, MI_Uint32 *index);
    MI_Result (MI_CALL *GetParentClassName)(const MI_Class *self, const MI_Char **name);
    MI_Result (MI_CALL *GetParentClass)(const MI_Class *self, MI_Class **parentClass);
    MI_Result (MI_CALL *Delete)(MI_Class *self);
    MI_Result (MI_CALL *Clone)(const MI_Class *self, MI_Class **newClass);
} MI_ClassFT;

typedef void (MI_CALL *MI_OperationCallback_Instance)(MI_Operation *operation, void *callbackContext,
                                                      const MI_Instance *instance, MI_Boolean moreResults,
                                                      MI_Result resultCode, const MI_Char *errorString,
                                                      const MI_Instance *errorDetails,
                                                      MI_Result (MI_CALL *resultAcknowledgement)(MI_Operation *operation));
typedef void (MI_CALL *MI_OperationCallback_Class)(MI_Operation *operation, void *callbackContext,
                                                   const MI_Class *classResult, MI_Boolean moreResults,
                                                   MI_Result resultCode, const MI_Char *errorString,
                                                   const MI_Instance *errorDetails,
                                                   MI_Result (MI_CALL *resultAcknowledgement)(MI_Operation *operation));

typedef struct _MI_OperationCallbacks
{
    void *callbackContext;
    void *promptUser;
    void *writeError;
    void *writeMessage;
    void *writeProgress;
    MI_OperationCallback_Instance instanceResult;
    void *indicationResult;
    MI_OperationCallback_Class classResult;
    void *streamedParameterResult;
} MI_OperationCallbacks;

struct _MI_Serializer
{
    MI_Uint64 reserved1;
    ptrdiff_t reserved2;
};

struct _MI_Deserializer
{
    MI_Uint64 reserved1;
    ptrdiff_t reserved2;
};

typedef MI_Result (MI_CALL *MI_Deserializer_ClassObjectNeeded)(void *context, const MI_Char *serverName,
                                                                const MI_Char *namespaceName,
                                                                const MI_Char *className,
                                                                MI_Class **requestedClassObject);

typedef struct _MI_ApplicationFT
{
    MI_Result (MI_CALL *Close)(MI_Application *application);
    MI_Result (MI_CALL *NewSession)(MI_Application *application, const MI_Char *protocol, const MI_Char *destination,
                                    MI_DestinationOptions *options, MI_SessionCallbacks *callbacks,
                                    MI_Instance **extendedError, MI_Session *session);
    MI_Result (MI_CALL *NewHostedProvider)(MI_Application *application, const MI_Char *namespaceName,
                                           const MI_Char *providerName, MI_MainFunction mi_Main,
                                           MI_Instance **extendedError, MI_HostedProvider *provider);
    MI_Result (MI_CALL *NewInstance)(MI_Application *application, const MI_Char *className,
                                     const MI_ClassDecl *classRTTI, MI_Instance **instance);
    MI_Result (MI_CALL *NewDestinationOptions)(MI_Application *application, MI_DestinationOptions *options);
    MI_Result (MI_CALL *NewOperationOptions)(MI_Application *application, MI_Boolean customOptionsMustUnderstand,
                                             MI_OperationOptions *options);
    MI_Result (MI_CALL *NewSubscriptionDeliveryOptions)(MI_Application *application,
                                                        MI_SubscriptionDeliveryType deliveryType,
                                                        MI_SubscriptionDeliveryOptions *deliveryOptions);
    MI_Result (MI_CALL *NewSerializer)(MI_Application *application, MI_Uint32 flags, MI_Char *format,
                                       MI_Serializer *serializer);
    MI_Result (MI_CALL *NewDeserializer)(MI_Application *application, MI_Uint32 flags, MI_Char *format,
                                         MI_Deserializer *deserializer);
    MI_Result (MI_CALL *NewInstanceFromClass)(MI_Application *application, const MI_Char *className,
                                              const MI_Class *classObject, MI_Instance **instance);
    MI_Result (MI_CALL *NewClass)(MI_Application *application, const MI_ClassDecl *classDecl,
                                  const MI_Char *namespaceName, const MI_Char *serverName, MI_Class **classObject);
} MI_ApplicationFT;

typedef struct _MI_HostedProviderFT
{
    MI_Result (MI_CALL *Close)(MI_HostedProvider *hostedProvider);
    MI_Result (MI_CALL *GetApplication)(MI_HostedProvider *hostedProvider, MI_Application *application);
} MI_HostedProviderFT;

typedef struct _MI_SessionFT
{
    MI_Result (MI_CALL *Close)(MI_Session *session, void *completionContext,
                               void (MI_CALL *completionCallback)(void *completionContext));
    MI_Result (MI_CALL *GetApplication)(MI_Session *session, MI_Application *application);
    void (MI_CALL *GetInstance)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                                const MI_Char *namespaceName, const MI_Instance *inboundInstance,
                                MI_OperationCallbacks *callbacks, MI_Operation *operation);
    void (MI_CALL *ModifyInstance)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                                   const MI_Char *namespaceName, const MI_Instance *inboundInstance,
                                   MI_OperationCallbacks *callbacks, MI_Operation *operation);
    void (MI_CALL *CreateInstance)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                                   const MI_Char *namespaceName, const MI_Instance *inboundInstance,
                                   MI_OperationCallbacks *callbacks, MI_Operation *operation);
    void (MI_CALL *DeleteInstance)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                                   const MI_Char *namespaceName, const MI_Instance *inboundInstance,
                                   MI_OperationCallbacks *callbacks, MI_Operation *operation);
    void (MI_CALL *Invoke)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                           const MI_Char *namespaceName, const MI_Char *className, const MI_Char *methodName,
                           const MI_Instance *inboundInstance, const MI_Instance *inboundProperties,
                           MI_OperationCallbacks *callbacks, MI_Operation *operation);
    void (MI_CALL *EnumerateInstances)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                                       const MI_Char *namespaceName, const MI_Char *className, MI_Boolean keysOnly,
                                       MI_OperationCallbacks *callbacks, MI_Operation *operation);
    void (MI_CALL *QueryInstances)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                                   const MI_Char *namespaceName, const MI_Char *queryDialect,
                                   const MI_Char *queryExpression, MI_OperationCallbacks *callbacks,
                                   MI_Operation *operation);
    void (MI_CALL *AssociatorInstances)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                                        const MI_Char *namespaceName, const MI_Instance *instanceKeys,
                                        const MI_Char *assocClass, const MI_Char *resultClass, const MI_Char *role,
                                        const MI_Char *resultRole, MI_Boolean keysOnly,
                                        MI_OperationCallbacks *callbacks, MI_Operation *operation);
    void (MI_CALL *ReferenceInstances)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                                       const MI_Char *namespaceName, const MI_Instance *instanceKeys,
                                       const MI_Char *resultClass, const MI_Char *role, MI_Boolean keysOnly,
                                       MI_OperationCallbacks *callbacks, MI_Operation *operation);
    void (MI_CALL *Subscribe)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                              const MI_Char *namespaceName, const MI_Char *queryDialect,
                              const MI_Char *queryExpression, const MI_SubscriptionDeliveryOptions *deliverOptions,
                              MI_OperationCallbacks *callbacks, MI_Operation *operation);
    void (MI_CALL *GetClass)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                             const MI_Char *namespaceName, const MI_Char *className, MI_OperationCallbacks *callbacks,
                             MI_Operation *operation);
    void (MI_CALL *EnumerateClasses)(MI_Session *session, MI_Uint32 flags, MI_OperationOptions *options,
                                     const MI_Char *namespaceName, const MI_Char *className,
                                     MI_Boolean classNamesOnly, MI_OperationCallbacks *callbacks,
                                     MI_Operation *operation);
    void (MI_CALL *TestConnection)(MI_Session *session, MI_Uint32 flags, MI_OperationCallbacks *callbacks,
                                   MI_Operation *operation);
} MI_SessionFT;

typedef struct _MI_OperationFT
{
    MI_Result (MI_CALL *Close)(MI_Operation *operation);
    MI_Result (MI_CALL *Cancel)(MI_Operation *operation, MI_CancellationReason reason);
    MI_Result (MI_CALL *GetSession)(MI_Operation *operation, MI_Session *session);
    MI_Result (MI_CALL *GetInstance)(MI_Operation *operation, const MI_Instance **instance, MI_Boolean *moreResults,
                                     MI_Result *result, const MI_Char **errorMessage,
                                     const MI_Instance **completionDetails);
    MI_Result (MI_CALL *GetIndication)(MI_Operation *operation, const MI_Instance **instance,
                                       const MI_Char **bookmark, const MI_Char **machineID, MI_Boolean *moreResults,
                                       MI_Result *result, const MI_Char **errorMessage,
                                       const MI_Instance **completionDetails);
    MI_Result (MI_CALL *GetClass)(MI_Operation *operation, const MI_Class **classResult, MI_Boolean *moreResults,
                                  MI_Result *result, const MI_Char **errorMessage,
                                  const MI_Instance **completionDetails);
} MI_OperationFT;

typedef struct _MI_DestinationOptionsFT
{
    void (MI_CALL *Delete)(MI_DestinationOptions *options);
    MI_Result (MI_CALL *SetString)(MI_DestinationOptions *options, const MI_Char *optionName, const MI_Char *value,
                                   MI_Uint32 flags);
    MI_Result (MI_CALL *SetNumber)(MI_DestinationOptions *options, const MI_Char *optionName, MI_Uint32 value,
                                   MI_Uint32 flags);
    MI_Result (MI_CALL *AddCredentials)(MI_DestinationOptions *options, const MI_Char *optionName,
                                        const MI_UserCredentials *credentials, MI_Uint32 flags);
    MI_Result (MI_CALL *GetString)(MI_DestinationOptions *options, const MI_Char *optionName, const MI_Char **value,
                                   MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetNumber)(MI_DestinationOptions *options, const MI_Char *optionName, MI_Uint32 *value,
                                   MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetOptionCount)(MI_DestinationOptions *options, MI_Uint32 *count);
    MI_Result (MI_CALL *GetOptionAt)(MI_DestinationOptions *options, MI_Uint32 index, const MI_Char **optionName,
                                     MI_Value *value, MI_Type *type, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetOption)(MI_DestinationOptions *options, const MI_Char *optionName, MI_Value *value,
                                   MI_Type *type, MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetCredentialsCount)(MI_DestinationOptions *options, MI_Uint32 *count);
    MI_Result (MI_CALL *GetCredentialsAt)(MI_DestinationOptions *options, MI_Uint32 index, const MI_Char **optionName,
                                          MI_UserCredentials *credentials, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetCredentialsPasswordAt)(MI_DestinationOptions *options, MI_Uint32 index,
                                                  const MI_Char **optionName, MI_Char *password,
                                                  MI_Uint32 bufferLength, MI_Uint32 *passwordLength,
                                                  MI_Uint32 *flags);
    MI_Result (MI_CALL *Clone)(const MI_DestinationOptions *self, MI_DestinationOptions *newDestinationOptions);
    MI_Result (MI_CALL *SetInterval)(MI_DestinationOptions *options, const MI_Char *optionName,
                                     const MI_Interval *value, MI_Uint32 flags);
    MI_Result (MI_CALL *GetInterval)(MI_DestinationOptions *options, const MI_Char *optionName, MI_Interval *value,
                                     MI_Uint32 *index, MI_Uint32 *flags);
} MI_DestinationOptionsFT;

typedef struct _MI_OperationOptionsFT
{
    void (MI_CALL *Delete)(MI_OperationOptions *options);
    MI_Result (MI_CALL *SetString)(MI_OperationOptions *options, const MI_Char *optionName, const MI_Char *value,
                                   MI_Uint32 flags);
    MI_Result (MI_CALL *SetNumber)(MI_OperationOptions *options, const MI_Char *optionName, MI_Uint32 value,
                                   MI_Uint32 flags);
    MI_Result (MI_CALL *SetCustomOption)(MI_OperationOptions *options, const MI_Char *optionName, MI_Type valueType,
                                         const MI_Value *value, MI_Boolean mustComply, MI_Uint32 flags);
    MI_Result (MI_CALL *GetString)(MI_OperationOptions *options, const MI_Char *optionName, const MI_Char **value,
                                   MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetNumber)(MI_OperationOptions *options, const MI_Char *optionName, MI_Uint32 *value,
                                   MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetOptionCount)(MI_OperationOptions *options, MI_Uint32 *count);
    MI_Result (MI_CALL *GetOptionAt)(MI_OperationOptions *options, MI_Uint32 index, const MI_Char **optionName,
                                     MI_Value *value, MI_Type *type, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetOption)(MI_OperationOptions *options, const MI_Char *optionName, MI_Value *value,
                                   MI_Type *type, MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetEnabledChannels)(MI_OperationOptions *options, const MI_Char *optionName,
                                            MI_Uint32 *channels, MI_Uint32 bufferLength, MI_Uint32 *channelCount,
                                            MI_Uint32 *flags);
    MI_Result (MI_CALL *Clone)(const MI_OperationOptions *self, MI_OperationOptions *newOperationOptions);
    MI_Result (MI_CALL *SetInterval)(MI_OperationOptions *options, const MI_Char *optionName,
                                     const MI_Interval *value, MI_Uint32 flags);
    MI_Result (MI_CALL *GetInterval)(MI_OperationOptions *options, const MI_Char *optionName, MI_Interval *value,
                                     MI_Uint32 *index, MI_Uint32 *flags);
} MI_OperationOptionsFT;

typedef struct _MI_SubscriptionDeliveryOptionsFT
{
    MI_Result (MI_CALL *SetString)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                   const MI_Char *value, MI_Uint32 flags);
    MI_Result (MI_CALL *SetNumber)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                   MI_Uint32 value, MI_Uint32 flags);
    MI_Result (MI_CALL *SetDateTime)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                     const MI_Datetime *value, MI_Uint32 flags);
    MI_Result (MI_CALL *SetInterval)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                     const MI_Interval *value, MI_Uint32 flags);
    MI_Result (MI_CALL *AddCredentials)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                        const MI_UserCredentials *credentials, MI_Uint32 flags);
    MI_Result (MI_CALL *Delete)(MI_SubscriptionDeliveryOptions *self);
    MI_Result (MI_CALL *GetString)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                   const MI_Char **value, MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetNumber)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                   MI_Uint32 *value, MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetDateTime)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                     MI_Datetime *value, MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetInterval)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                     MI_Interval *value, MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetOptionCount)(MI_SubscriptionDeliveryOptions *options, MI_Uint32 *count);
    MI_Result (MI_CALL *GetOptionAt)(MI_SubscriptionDeliveryOptions *options, MI_Uint32 index,
                                     const MI_Char **optionName, MI_Value *value, MI_Type *type, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetOption)(MI_SubscriptionDeliveryOptions *options, const MI_Char *optionName,
                                   MI_Value *value, MI_Type *type, MI_Uint32 *index, MI_Uint32 *flags);
    MI_Result (MI_CALL *GetCredentialsCount)(MI_SubscriptionDeliveryOptions *options, MI_Uint32 *count);
    MI_Result (MI_CALL *GetCredentialsAt)(MI_SubscriptionDeliveryOptions *options, MI_Uint32 index,
                                          const MI_Char **optionName, MI_UserCredentials *credentials,
                                          MI_Uint32 *flags);
    MI_Result (MI_CALL *GetCredentialsPasswordAt)(MI_SubscriptionDeliveryOptions *options, MI_Uint32 index,
                                                  const MI_Char **optionName, MI_Char *password,
                                                  MI_Uint32 bufferLength, MI_Uint32 *passwordLength,
                                                  MI_Uint32 *flags);
    MI_Result (MI_CALL *Clone)(const MI_SubscriptionDeliveryOptions *self,
                               MI_SubscriptionDeliveryOptions *newSubscriptionDeliveryOptions);
} MI_SubscriptionDeliveryOptionsFT;

typedef struct _MI_SerializerFT
{
    MI_Result (MI_CALL *Close)(MI_Serializer *serializer);
    MI_Result (MI_CALL *SerializeClass)(MI_Serializer *serializer, MI_Uint32 flags, const MI_Class *classObject,
                                        MI_Uint8 *clientBuffer, MI_Uint32 clientBufferLength,
                                        MI_Uint32 *clientBufferNeeded);
    MI_Result (MI_CALL *SerializeInstance)(MI_Serializer *serializer, MI_Uint32 flags,
                                           const MI_Instance *instanceObject, MI_Uint8 *clientBuffer,
                                           MI_Uint32 clientBufferLength, MI_Uint32 *clientBufferNeeded);
} MI_SerializerFT;

typedef struct _MI_DeserializerFT
{
    MI_Result (MI_CALL *Close)(MI_Deserializer *deserializer);
    MI_Result (MI_CALL *DeserializeClass)(MI_Deserializer *deserializer, MI_Uint32 flags, MI_Uint8 *serializedBuffer,
                                          MI_Uint32 serializedBufferLength, MI_Class *parentClass,
                                          const MI_Char *serverName, const MI_Char *namespaceName,
                                          MI_Deserializer_ClassObjectNeeded classObjectNeeded,
                                          void *classObjectNeededContext, MI_Uint32 *serializedBufferRead,
                                          MI_Class **classObject, MI_Instance **cimErrorDetails);
    MI_Result (MI_CALL *Class_GetClassName)(MI_Deserializer *deserializer, MI_Uint8 *serializedBuffer,
                                            MI_Uint32 serializedBufferLength, MI_Char *className,
                                            MI_Uint32 *classNameLength, MI_Instance **cimErrorDetails);
    MI_Result (MI_CALL *Class_GetParentClassName)(MI_Deserializer *deserializer, MI_Uint8 *serializedBuffer,
                                                  MI_Uint32 serializedBufferLength, MI_Char *parentClassName,
                                                  MI_Uint32 *parentClassNameLength, MI_Instance **cimErrorDetails);
    MI_Result (MI_CALL *DeserializeInstance)(MI_Deserializer *deserializer, MI_Uint32 flags,
                                             MI_Uint8 *serializedBuffer, MI_Uint32 serializedBufferLength,
                                             MI_Class **classObjects, MI_Uint32 numberClassObjects,
                                             MI_Deserializer_ClassObjectNeeded classObjectNeeded,
                                             void *classObjectNeededContext, MI_Uint32 *serializedBufferRead,
                                             MI_Instance **instanceObject, MI_Instance **cimErrorDetails);
    MI_Result (MI_CALL *Instance_GetClassName)(MI_Deserializer *deserializer, MI_Uint8 *serializedBuffer,
                                               MI_Uint32 serializedBufferLength, MI_Char *className,
                                               MI_Uint32 *classNameLength, MI_Instance **cimErrorDetails);
} MI_DeserializerFT;

typedef struct _MI_UtilitiesFT
{
    MI_ErrorCategory (MI_CALL *MapErrorToMiErrorCategory)(MI_Char *errorType, MI_Uint32 error);
    MI_Result (MI_CALL *CimErrorFromErrorCode)(MI_Uint32 error, const MI_Char *errorType,
                                               const MI_Char *errorMessage, MI_Instance **cimError);
} MI_UtilitiesFT;

struct _MI_Application
{
    MI_Uint64 reserved1;
    ptrdiff_t reserved2;
    const MI_ApplicationFT *ft;
};

struct _MI_Session
{
    MI_Uint64 reserved1;
    ptrdiff_t reserved2;
    const MI_SessionFT *ft;
};

struct _MI_Operation
{
    MI_Uint64 reserved1;
    ptrdiff_t reserved2;
    const MI_OperationFT *ft;
};

struct _MI_HostedProvider
{
    MI_Uint64 reserved1;
    ptrdiff_t reserved2;
    const MI_HostedProviderFT *ft;
};

struct _MI_DestinationOptions
{
    MI_Uint64 reserved1;
    ptrdiff_t reserved2;
    const MI_DestinationOptionsFT *ft;
};

struct _MI_OperationOptions
{
    MI_Uint64 reserved1;
    ptrdiff_t reserved2;
    const MI_OperationOptionsFT *ft;
};

struct _MI_SubscriptionDeliveryOptions
{
    MI_Uint64 reserved1;
    ptrdiff_t reserved2;
    const MI_SubscriptionDeliveryOptionsFT *ft;
};

typedef struct _MI_ClientFT_V1
{
    const MI_ApplicationFT *applicationFT;
    const MI_SessionFT *sessionFT;
    const MI_OperationFT *operationFT;
    const MI_HostedProviderFT *hostedProviderFT;
    const MI_SerializerFT *serializerFT;
    const MI_DeserializerFT *deserializerFT;
    const MI_SubscriptionDeliveryOptionsFT *subscribeDeliveryOptionsFT;
    const MI_DestinationOptionsFT *destinationOptionsFT;
    const MI_OperationOptionsFT *operationOptionsFT;
    const MI_UtilitiesFT *utilitiesFT;
} MI_ClientFT_V1;
