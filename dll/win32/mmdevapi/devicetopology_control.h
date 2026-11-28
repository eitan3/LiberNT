/*
 * PROJECT:     LiberNT Core Audio
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     The control interface of a part that stands for a KS node: which parts are nodes, and the three requests the driver sees through the KS channel of the topology's filter (a KSP_NODE, a KSM_NODE and a KSE_NODE carrying the node's id). Free of COM, so that the host test runs the very code the DLL runs (N15-08)
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#ifndef _MMDEVAPI_DEVICETOPOLOGY_CONTROL_H_
#define _MMDEVAPI_DEVICETOPOLOGY_CONTROL_H_

#include <stddef.h>

/* The includer provides HRESULT, BOOL, DWORD, UINT, ULONG, HANDLE, ZeroMemory, KSPROPERTY, KSMETHOD, KSEVENT, KSP_NODE, KSM_NODE, KSE_NODE, the IOCTL_KS_* codes, S_OK and E_INVALIDARG */

/* A part's local id: its kind in the high half and its index in the filter's list in the low one */
#define PART_KIND_CONNECTOR 1
#define PART_KIND_SUBUNIT 2
#define PART_ID(kind, index) (((UINT)(kind) << 16) | (index))
#define PART_KIND(id) ((id) >> 16)
#define PART_INDEX(id) ((id) & 0xffff)

/*
 * The KS channel of a filter: the handle of the filter's device interface and the one call every
 * request goes through, as DeviceIoControl takes it (the request in, the data out, the bytes the
 * driver answers). The DLL's is DeviceIoControl, a failure as HRESULT_FROM_WIN32 of its error; a
 * test's is its own.
 */
struct ks_channel
{
    HANDLE filter;
    HRESULT (*request)(HANDLE filter, DWORD code, const void *in, ULONG in_size, void *out,
                       ULONG out_size, ULONG *returned);
};

/*
 * The node a part stands for: a subunit of a filter's topology, a KS node whose index is the
 * filter's node list's, reached through the filter's path. A connector (a pin) and a part of an
 * endpoint's own topology (it has no path) stand for no node.
 */
static inline BOOL part_node_id(UINT local_id, const void *path, ULONG *node)
{
    if (PART_KIND(local_id) != PART_KIND_SUBUNIT || !path)
        return FALSE;
    *node = PART_INDEX(local_id);
    return TRUE;
}

/*
 * A property of the node: the caller's header (the set, the id and the flags) as a KSP_NODE of
 * the node, whatever node the caller's buffer names; the data is the driver's to read or fill.
 */
static HRESULT ks_node_property(const struct ks_channel *channel, ULONG node, const KSPROPERTY *property,
                                ULONG length, void *data, ULONG size, ULONG *returned)
{
    KSP_NODE request;
    ULONG bytes = 0;
    HRESULT hr;

    if (!property || length < sizeof(*property))
        return E_INVALIDARG;
    ZeroMemory(&request, sizeof(request));
    request.Property = *property;
    request.NodeId = node;
    hr = channel->request(channel->filter, IOCTL_KS_PROPERTY, &request, sizeof(request), data, size, &bytes);
    if (returned)
        *returned = bytes;
    return hr;
}

/* A method of the node, as the property: a KSM_NODE of the node through IOCTL_KS_METHOD */
static HRESULT ks_node_method(const struct ks_channel *channel, ULONG node, const KSMETHOD *method,
                              ULONG length, void *data, ULONG size, ULONG *returned)
{
    KSM_NODE request;
    ULONG bytes = 0;
    HRESULT hr;

    if (!method || length < sizeof(*method))
        return E_INVALIDARG;
    ZeroMemory(&request, sizeof(request));
    request.Method = *method;
    request.NodeID = node;
    hr = channel->request(channel->filter, IOCTL_KS_METHOD, &request, sizeof(request), data, size, &bytes);
    if (returned)
        *returned = bytes;
    return hr;
}

/*
 * An event of the node: with an event (a KSE_NODE of the node) it is enabled through
 * IOCTL_KS_ENABLE_EVENT, the data being the KSEVENTDATA; without one (no length) the event the
 * data names is disabled through IOCTL_KS_DISABLE_EVENT, which takes the data as its request.
 */
static HRESULT ks_node_event(const struct ks_channel *channel, ULONG node, const KSEVENT *event,
                             ULONG length, void *data, ULONG size, ULONG *returned)
{
    KSE_NODE request;
    ULONG bytes = 0;
    HRESULT hr;

    if (!length)
    {
        hr = channel->request(channel->filter, IOCTL_KS_DISABLE_EVENT, data, size, NULL, 0, &bytes);
        if (returned)
            *returned = bytes;
        return hr;
    }
    if (!event || length < sizeof(*event))
        return E_INVALIDARG;
    ZeroMemory(&request, sizeof(request));
    request.Event = *event;
    request.NodeId = node;
    hr = channel->request(channel->filter, IOCTL_KS_ENABLE_EVENT, &request, sizeof(request), data, size, &bytes);
    if (returned)
        *returned = bytes;
    return hr;
}

#endif /* !_MMDEVAPI_DEVICETOPOLOGY_CONTROL_H_ */
