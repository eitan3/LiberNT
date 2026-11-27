/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Present surfaces through a Direct3D 11 composition swap chain and DirectComposition
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

namespace LiberUI
{

typedef HRESULT (WINAPI *PFN_LIBER_D3D11CREATEDEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                     const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                                     D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *PFN_LIBER_DCOMPOSITIONCREATEDEVICE)(IDXGIDevice *, REFIID, void **);

struct SurfaceGpu
{
    HMODULE D3d11;
    HMODULE Dcomp;
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    IDXGIDevice *DxgiDevice;
    IDXGIFactory2 *Factory;
    IDCompositionDevice *Composition;
    IDCompositionTarget *Target;
    IDCompositionVisual *Visual;
    IDXGISwapChain1 *SwapChain;
    UINT Width;
    UINT Height;
};

template <class T>
static void ReleaseCom(T *&p)
{
    if (p)
    {
        p->Release();
        p = NULL;
    }
}

void GpuDestroy(SurfaceGpu *gpu)
{
    if (!gpu)
        return;
    if (gpu->Target)
        gpu->Target->SetRoot(NULL);
    if (gpu->Composition)
        gpu->Composition->Commit();
    ReleaseCom(gpu->Visual);
    ReleaseCom(gpu->Target);
    ReleaseCom(gpu->SwapChain);
    ReleaseCom(gpu->Composition);
    ReleaseCom(gpu->Factory);
    ReleaseCom(gpu->DxgiDevice);
    ReleaseCom(gpu->Context);
    ReleaseCom(gpu->Device);
    if (gpu->Dcomp)
        FreeLibrary(gpu->Dcomp);
    if (gpu->D3d11)
        FreeLibrary(gpu->D3d11);
    LocalFree(gpu);
}

SurfaceGpu *GpuCreate()
{
    static const D3D_DRIVER_TYPE Drivers[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    SurfaceGpu *gpu = (SurfaceGpu *)LocalAlloc(LPTR, sizeof(SurfaceGpu));
    PFN_LIBER_D3D11CREATEDEVICE createDevice;
    PFN_LIBER_DCOMPOSITIONCREATEDEVICE createComposition;
    IDXGIAdapter *adapter = NULL;
    HRESULT hr = E_FAIL;

    if (!gpu)
        return NULL;
    gpu->D3d11 = LoadLibraryW(L"d3d11.dll");
    gpu->Dcomp = LoadLibraryW(L"dcomp.dll");
    createDevice = gpu->D3d11 ? (PFN_LIBER_D3D11CREATEDEVICE)GetProcAddress(gpu->D3d11, "D3D11CreateDevice") : NULL;
    createComposition = gpu->Dcomp ?
        (PFN_LIBER_DCOMPOSITIONCREATEDEVICE)GetProcAddress(gpu->Dcomp, "DCompositionCreateDevice") : NULL;
    if (!createDevice || !createComposition)
    {
        GpuDestroy(gpu);
        return NULL;
    }

    for (UINT i = 0; i < _countof(Drivers) && FAILED(hr); i++)
    {
        hr = createDevice(NULL, Drivers[i], NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                          &gpu->Device, NULL, &gpu->Context);
    }
    if (SUCCEEDED(hr))
        hr = gpu->Device->QueryInterface(__uuidof(IDXGIDevice), (void **)&gpu->DxgiDevice);
    if (SUCCEEDED(hr))
        hr = gpu->DxgiDevice->GetAdapter(&adapter);
    if (SUCCEEDED(hr))
        hr = adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&gpu->Factory);
    if (adapter)
        adapter->Release();
    if (SUCCEEDED(hr))
        hr = createComposition(gpu->DxgiDevice, __uuidof(IDCompositionDevice), (void **)&gpu->Composition);
    if (FAILED(hr))
    {
        GpuDestroy(gpu);
        return NULL;
    }
    LUI_ASSERT(gpu->Device && gpu->Context && gpu->Factory && gpu->Composition);
    return gpu;
}

BOOL GpuAttach(SurfaceGpu *gpu, HWND hwnd)
{
    LUI_ASSERT(gpu && hwnd && !gpu->Target);
    if (!gpu || !hwnd || gpu->Target)
        return FALSE;
    if (FAILED(gpu->Composition->CreateTargetForHwnd(hwnd, TRUE, &gpu->Target)) ||
        FAILED(gpu->Composition->CreateVisual(&gpu->Visual)) || FAILED(gpu->Target->SetRoot(gpu->Visual)))
    {
        ReleaseCom(gpu->Visual);
        ReleaseCom(gpu->Target);
        return FALSE;
    }
    return TRUE;
}

static BOOL GpuSize(SurfaceGpu *gpu, UINT width, UINT height)
{
    if (gpu->SwapChain && gpu->Width == width && gpu->Height == height)
        return TRUE;

    if (gpu->SwapChain)
    {
        if (FAILED(gpu->SwapChain->ResizeBuffers(2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0)))
            return FALSE;
    }
    else
    {
        DXGI_SWAP_CHAIN_DESC1 desc;

        ZeroMemory(&desc, sizeof(desc));
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(gpu->Factory->CreateSwapChainForComposition(gpu->Device, &desc, NULL, &gpu->SwapChain)))
            return FALSE;
        if (FAILED(gpu->Visual->SetContent(gpu->SwapChain)))
        {
            ReleaseCom(gpu->SwapChain);
            return FALSE;
        }
    }
    gpu->Width = width;
    gpu->Height = height;
    return TRUE;
}

BOOL GpuPresent(SurfaceGpu *gpu, const DWORD *bits, UINT width, UINT height)
{
    ID3D11Texture2D *buffer = NULL;
    BOOL first;
    HRESULT hr;

    LUI_ASSERT(gpu && bits && width && height);
    if (!gpu || !gpu->Visual || !bits || !width || !height)
        return FALSE;

    first = gpu->SwapChain == NULL;
    if (!GpuSize(gpu, width, height))
        return FALSE;
    hr = gpu->SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&buffer);
    if (FAILED(hr))
        return FALSE;
    gpu->Context->UpdateSubresource(buffer, 0, NULL, bits, width * sizeof(DWORD), 0);
    buffer->Release();
    hr = gpu->SwapChain->Present(0, 0);
    if (FAILED(hr))
        return FALSE;
    if (first && FAILED(gpu->Composition->Commit()))
        return FALSE;
    return TRUE;
}

}
