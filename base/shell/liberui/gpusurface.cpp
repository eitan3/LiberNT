/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 device, canvas and DirectComposition presentation of a surface
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"
#include "gpu.h"

#include <d3dcompiler.h>

namespace LiberUI
{

typedef HRESULT (WINAPI *PFN_LIBER_D3D11CREATEDEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                                     const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                                     D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
typedef HRESULT (WINAPI *PFN_LIBER_DCOMPOSITIONCREATEDEVICE)(IDXGIDevice *, REFIID, void **);
typedef HRESULT (WINAPI *PFN_LIBER_D3DCOMPILE)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *,
                                              ID3DInclude *, const char *, const char *, UINT, UINT, ID3DBlob **,
                                              ID3DBlob **);

static const char LiberShaderSource[] =
    "cbuffer Draw : register(b0) {\n"
    "  float4 rect; float4 shape; float4 params; float4 arc; float4 color;\n"
    "  float4 clipRect; float4 clipParams; float4 target; float4 uvRect;\n"
    "};\n"
    "Texture2D tex : register(t0);\n"
    "SamplerState samp : register(s0);\n"
    "struct V { float4 pos : SV_Position; float2 p : TEXCOORD0; float2 uv : TEXCOORD1; };\n"
    "V VS(uint id : SV_VertexID) {\n"
    "  V o; float2 t = float2(id & 1, id >> 1);\n"
    "  o.p = lerp(rect.xy, rect.zw, t); o.uv = lerp(uvRect.xy, uvRect.zw, t);\n"
    "  o.pos = float4(o.p / target.xy * float2(2, -2) + float2(-1, 1), 0, 1); return o;\n"
    "}\n"
    "float roundRect(float2 p, float4 r, float radius) {\n"
    "  float2 c = (r.xy + r.zw) * 0.5; float2 h = max((r.zw - r.xy) * 0.5, 0);\n"
    "  radius = clamp(radius, 0, min(h.x, h.y));\n"
    "  float2 q = abs(p - c) - (h - radius);\n"
    "  return length(max(q, 0)) + min(max(q.x, q.y), 0) - radius;\n"
    "}\n"
    "float ellipse(float2 p, float4 r) {\n"
    "  float2 c = (r.xy + r.zw) * 0.5; float2 h = max((r.zw - r.xy) * 0.5, 0.0001);\n"
    "  float k = length((p - c) / h); return (k - 1) * min(h.x, h.y);\n"
    "}\n"
    "float cover(float d) { return saturate(0.5 - d); }\n"
    "float erfApprox(float x) {\n"
    "  float x2 = x * x; float a = 0.147;\n"
    "  return sign(x) * sqrt(1 - exp(-x2 * (1.2732395 + a * x2) / (1 + a * x2)));\n"
    "}\n"
    "float4 PS(V v) : SV_Target {\n"
    "  float2 p = v.p; float kind = params.z; float c = 1; float4 result;\n"
    "  if (kind < 0.5) c = cover(roundRect(p, shape, params.x));\n"
    "  else if (kind < 1.5) { float d = roundRect(p, shape, params.x); c = cover(abs(d + params.y * 0.5) - params.y * 0.5); }\n"
    "  else if (kind < 2.5) c = cover(ellipse(p, shape));\n"
    "  else if (kind < 3.5) {\n"
    "    float2 m = (shape.xy + shape.zw) * 0.5; float r = (shape.z - shape.x) * 0.5; float2 d = p - m;\n"
    "    float a = atan2(d.y, d.x) - arc.x; a = a - 6.2831853 * floor(a / 6.2831853);\n"
    "    float band = abs(length(d) - r) - params.y * 0.5;\n"
    "    float2 e0 = m + r * float2(cos(arc.x), sin(arc.x));\n"
    "    float2 e1 = m + r * float2(cos(arc.x + arc.y), sin(arc.x + arc.y));\n"
    "    float caps = min(length(p - e0), length(p - e1)) - params.y * 0.5;\n"
    "    c = cover((arc.y >= 6.28318 || a <= arc.y) ? min(band, caps) : caps);\n"
    "  }\n"
    "  else if (kind < 4.5) {\n"
    "    float2 ba = shape.zw - shape.xy; float len = max(length(ba), 0.000001); float2 dir = ba / len;\n"
    "    float2 pa = p - shape.xy; float along = dot(pa, dir); float across = abs(dot(pa, float2(-dir.y, dir.x)));\n"
    "    c = cover(max(across - params.y * 0.5, max(-along, along - len)));\n"
    "  }\n"
    "  else if (kind < 5.5) c = tex.Sample(samp, v.uv).a;\n"
    "  else if (kind > 6.5 && kind < 7.5) {\n"
    "    float sigma = max(params.y, 0.0001);\n"
    "    c = saturate(0.5 - 0.5 * erfApprox(roundRect(p, shape, params.x) / (sigma * 1.4142136)));\n"
    "    if (uvRect.w > 0.5) c *= 1 - cover(roundRect(p, arc, params.x));\n"
    "  }\n"
    "  else if (kind > 7.5) c = cover(roundRect(p, shape, params.x));\n"
    "  if (clipParams.z > 0.5) c = 0;\n"
    "  else if (clipParams.y > 0.5) c *= cover(roundRect(p, clipRect, clipParams.x));\n"
    "  if (kind > 7.5) {\n"
    "    float2 g = arc.zw - arc.xy; float t = saturate(dot(p - arc.xy, g) / max(dot(g, g), 0.000001));\n"
    "    result = lerp(color, uvRect, t) * c;\n"
    "  }\n"
    "  else if (kind > 5.5 && kind < 6.5) result = tex.Sample(samp, v.uv) * c * color.a; else result = color * c;\n"
    "  return result;\n"
    "}\n";

template <class T>
static void ReleaseCom(T *&p)
{
    if (p)
    {
        p->Release();
        p = NULL;
    }
}

static void ReleaseCanvas(SurfaceGpu *gpu)
{
    ReleaseCom(gpu->CanvasSource);
    ReleaseCom(gpu->CanvasView);
    ReleaseCom(gpu->Canvas);
}

static void ReleaseDevice(SurfaceGpu *gpu)
{
    ReleaseCom(gpu->Sampler);
    ReleaseCom(gpu->Rasterizer);
    ReleaseCom(gpu->BlendCopy);
    ReleaseCom(gpu->BlendOver);
    ReleaseCom(gpu->Constants);
    ReleaseCom(gpu->PixelShader);
    ReleaseCom(gpu->VertexShader);
    ReleaseCom(gpu->Composition);
    ReleaseCom(gpu->Factory);
    ReleaseCom(gpu->DxgiDevice);
    ReleaseCom(gpu->Context);
    ReleaseCom(gpu->Device);
}

void GpuDestroy(SurfaceGpu *gpu)
{
    if (!gpu)
        return;
    for (UINT i = 0; i < GpuCacheEntries; i++)
        ReleaseCom(gpu->Cache[i].View);
    if (gpu->Target)
        gpu->Target->SetRoot(NULL);
    if (gpu->Composition)
        gpu->Composition->Commit();
    ReleaseCanvas(gpu);
    ReleaseCom(gpu->Visual);
    ReleaseCom(gpu->Target);
    ReleaseCom(gpu->SwapChain);
    ReleaseDevice(gpu);
    if (gpu->Compiler)
        FreeLibrary(gpu->Compiler);
    if (gpu->Dcomp)
        FreeLibrary(gpu->Dcomp);
    if (gpu->D3d11)
        FreeLibrary(gpu->D3d11);
    LocalFree(gpu);
}

static HRESULT CreatePipeline(SurfaceGpu *gpu)
{
    PFN_LIBER_D3DCOMPILE compile;
    ID3DBlob *vs = NULL, *ps = NULL, *errors = NULL;
    HRESULT hr;

    gpu->Compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    compile = gpu->Compiler ? (PFN_LIBER_D3DCOMPILE)GetProcAddress(gpu->Compiler, "D3DCompile") : NULL;
    if (!compile)
        return E_NOINTERFACE;

    hr = compile(LiberShaderSource, sizeof(LiberShaderSource) - 1, "LiberUI", NULL, NULL, "VS", "vs_4_0", 0, 0, &vs,
                 &errors);
    if (SUCCEEDED(hr))
        hr = compile(LiberShaderSource, sizeof(LiberShaderSource) - 1, "LiberUI", NULL, NULL, "PS", "ps_4_0", 0, 0,
                     &ps, &errors);
    if (FAILED(hr) && errors)
        OutputDebugStringA((const char *)errors->GetBufferPointer());
    ReleaseCom(errors);
    if (SUCCEEDED(hr))
        hr = gpu->Device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), NULL, &gpu->VertexShader);
    if (SUCCEEDED(hr))
        hr = gpu->Device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), NULL, &gpu->PixelShader);
    ReleaseCom(vs);
    ReleaseCom(ps);
    if (FAILED(hr))
        return hr;

    D3D11_BUFFER_DESC cb;
    ZeroMemory(&cb, sizeof(cb));
    cb.ByteWidth = sizeof(GpuDrawConstants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = gpu->Device->CreateBuffer(&cb, NULL, &gpu->Constants);
    if (FAILED(hr))
        return hr;

    D3D11_BLEND_DESC blend;
    ZeroMemory(&blend, sizeof(blend));
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = gpu->Device->CreateBlendState(&blend, &gpu->BlendOver);
    if (FAILED(hr))
        return hr;
    blend.RenderTarget[0].BlendEnable = FALSE;
    hr = gpu->Device->CreateBlendState(&blend, &gpu->BlendCopy);
    if (FAILED(hr))
        return hr;

    D3D11_RASTERIZER_DESC raster;
    ZeroMemory(&raster, sizeof(raster));
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.ScissorEnable = TRUE;
    raster.DepthClipEnable = TRUE;
    hr = gpu->Device->CreateRasterizerState(&raster, &gpu->Rasterizer);
    if (FAILED(hr))
        return hr;

    D3D11_SAMPLER_DESC sampler;
    ZeroMemory(&sampler, sizeof(sampler));
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    return gpu->Device->CreateSamplerState(&sampler, &gpu->Sampler);
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
        if (SUCCEEDED(hr))
            hr = gpu->Device->QueryInterface(__uuidof(IDXGIDevice), (void **)&gpu->DxgiDevice);
        if (SUCCEEDED(hr))
            hr = gpu->DxgiDevice->GetAdapter(&adapter);
        if (SUCCEEDED(hr))
            hr = adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&gpu->Factory);
        ReleaseCom(adapter);
        if (SUCCEEDED(hr))
            hr = createComposition(gpu->DxgiDevice, __uuidof(IDCompositionDevice), (void **)&gpu->Composition);
        if (SUCCEEDED(hr))
            hr = CreatePipeline(gpu);
        if (FAILED(hr))
            ReleaseDevice(gpu);
    }
    if (FAILED(hr))
    {
        GpuDestroy(gpu);
        return NULL;
    }
    LUI_ASSERT(gpu->Device && gpu->Context && gpu->Factory && gpu->Composition && gpu->PixelShader);
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

BOOL GpuHasCanvas(const SurfaceGpu *gpu)
{
    return gpu && gpu->Canvas && gpu->SwapChain;
}

BOOL GpuDeviceLost(SurfaceGpu *gpu)
{
    return gpu && gpu->Device && gpu->Device->GetDeviceRemovedReason() != S_OK;
}

BOOL GpuEnsureCanvas(SurfaceGpu *gpu, UINT width, UINT height, BOOL *recreated)
{
    LUI_ASSERT(gpu && width && height && recreated);
    if (!gpu || !width || !height || !recreated)
        return FALSE;
    *recreated = FALSE;
    if (gpu->Canvas && gpu->Width == width && gpu->Height == height)
        return TRUE;
    *recreated = TRUE;

    ReleaseCanvas(gpu);
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
        if (FAILED(gpu->Visual->SetContent(gpu->SwapChain)) || FAILED(gpu->Composition->Commit()))
        {
            ReleaseCom(gpu->SwapChain);
            return FALSE;
        }
    }

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(gpu->Device->CreateTexture2D(&desc, NULL, &gpu->Canvas)) ||
        FAILED(gpu->Device->CreateRenderTargetView(gpu->Canvas, NULL, &gpu->CanvasView)))
    {
        ReleaseCanvas(gpu);
        return FALSE;
    }
    const float clear[4] = { 0, 0, 0, 0 };
    gpu->Context->ClearRenderTargetView(gpu->CanvasView, clear);
    gpu->Width = width;
    gpu->Height = height;
    return TRUE;
}

static BOOL DrawCanvasFaded(SurfaceGpu *gpu, ID3D11Texture2D *buffer, float opacity)
{
    ID3D11RenderTargetView *target = NULL;
    D3D11_MAPPED_SUBRESOURCE mapped;
    GpuDrawConstants constants;
    const float clear[4] = { 0, 0, 0, 0 }, factor[4] = { 0, 0, 0, 0 };
    ID3D11ShaderResourceView *none = NULL;

    if (!gpu->CanvasSource && FAILED(gpu->Device->CreateShaderResourceView(gpu->Canvas, NULL, &gpu->CanvasSource)))
        return FALSE;
    if (FAILED(gpu->Device->CreateRenderTargetView(buffer, NULL, &target)))
        return FALSE;
    ZeroMemory(&constants, sizeof(constants));
    constants.Rect[2] = constants.Shape[2] = (float)gpu->Width;
    constants.Rect[3] = constants.Shape[3] = (float)gpu->Height;
    constants.Params[2] = (float)GpuImage;
    constants.Color[0] = constants.Color[1] = constants.Color[2] = constants.Color[3] = opacity;
    constants.Target[0] = (float)gpu->Width;
    constants.Target[1] = (float)gpu->Height;
    constants.UvRect[2] = constants.UvRect[3] = 1.0f;
    if (FAILED(gpu->Context->Map(gpu->Constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        target->Release();
        return FALSE;
    }
    CopyMemory(mapped.pData, &constants, sizeof(constants));
    gpu->Context->Unmap(gpu->Constants, 0);

    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)gpu->Width, (float)gpu->Height, 0.0f, 1.0f };
    D3D11_RECT scissor = { 0, 0, (LONG)gpu->Width, (LONG)gpu->Height };
    ID3D11DeviceContext *context = gpu->Context;
    context->OMSetRenderTargets(1, &target, NULL);
    context->ClearRenderTargetView(target, clear);
    context->RSSetViewports(1, &viewport);
    context->RSSetState(gpu->Rasterizer);
    context->RSSetScissorRects(1, &scissor);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->IASetInputLayout(NULL);
    context->VSSetShader(gpu->VertexShader, NULL, 0);
    context->PSSetShader(gpu->PixelShader, NULL, 0);
    context->VSSetConstantBuffers(0, 1, &gpu->Constants);
    context->PSSetConstantBuffers(0, 1, &gpu->Constants);
    context->PSSetSamplers(0, 1, &gpu->Sampler);
    context->PSSetShaderResources(0, 1, &gpu->CanvasSource);
    context->OMSetBlendState(gpu->BlendCopy, factor, 0xFFFFFFFF);
    context->Draw(4, 0);
    context->PSSetShaderResources(0, 1, &none);
    context->OMSetRenderTargets(0, NULL, NULL);
    target->Release();
    return TRUE;
}

BOOL GpuPresentCanvas(SurfaceGpu *gpu, float opacity)
{
    ID3D11Texture2D *buffer = NULL;
    BOOL drawn = TRUE;

    LUI_ASSERT(opacity >= 0.0f && opacity <= 1.0f);
    if (!gpu || !gpu->Canvas || !gpu->SwapChain)
        return FALSE;
    if (FAILED(gpu->SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&buffer)))
        return FALSE;
    if (opacity >= 1.0f)
        gpu->Context->CopyResource(buffer, gpu->Canvas);
    else
        drawn = DrawCanvasFaded(gpu, buffer, opacity < 0.0f ? 0.0f : opacity);
    buffer->Release();
    return drawn && SUCCEEDED(gpu->SwapChain->Present(0, 0));
}

BOOL GpuReadCanvas(SurfaceGpu *gpu, DWORD *pixels, UINT stride)
{
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D *staging = NULL;
    D3D11_MAPPED_SUBRESOURCE mapped;
    BOOL ok = FALSE;

    if (!gpu || !gpu->Canvas || !pixels || stride < gpu->Width * sizeof(DWORD))
        return FALSE;
    gpu->Canvas->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(gpu->Device->CreateTexture2D(&desc, NULL, &staging)))
        return FALSE;
    gpu->Context->CopyResource(staging, gpu->Canvas);
    if (SUCCEEDED(gpu->Context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        for (UINT y = 0; y < gpu->Height; y++)
            CopyMemory((BYTE *)pixels + (SIZE_T)y * stride, (const BYTE *)mapped.pData + (SIZE_T)y * mapped.RowPitch,
                       gpu->Width * sizeof(DWORD));
        gpu->Context->Unmap(staging, 0);
        ok = TRUE;
    }
    staging->Release();
    return ok;
}

}
