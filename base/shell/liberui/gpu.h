/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private Direct3D 11 state of a surface and its GPU painter
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

namespace LiberUI
{

enum GpuShape
{
    GpuFillRound = 0,
    GpuStrokeRound,
    GpuFillEllipse,
    GpuStrokeArc,
    GpuLine,
    GpuMask,
    GpuImage
};

struct GpuDrawConstants
{
    float Rect[4];
    float Shape[4];
    float Params[4];
    float Arc[4];
    float Color[4];
    float ClipRect[4];
    float ClipParams[4];
    float Target[4];
    float UvRect[4];
};

enum
{
    GpuCacheEntries = 96,
    GpuCacheKeyChars = 160
};

struct GpuCacheEntry
{
    WCHAR Key[GpuCacheKeyChars];
    ID3D11ShaderResourceView *View;
    UINT Width;
    UINT Height;
    ULONGLONG Used;
};

struct SurfaceGpu
{
    HMODULE D3d11;
    HMODULE Dcomp;
    HMODULE Compiler;
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    IDXGIDevice *DxgiDevice;
    IDXGIFactory2 *Factory;
    IDCompositionDevice *Composition;
    IDCompositionTarget *Target;
    IDCompositionVisual *Visual;
    IDXGISwapChain1 *SwapChain;
    ID3D11Texture2D *Canvas;
    ID3D11RenderTargetView *CanvasView;
    ID3D11VertexShader *VertexShader;
    ID3D11PixelShader *PixelShader;
    ID3D11Buffer *Constants;
    ID3D11BlendState *BlendOver;
    ID3D11BlendState *BlendCopy;
    ID3D11RasterizerState *Rasterizer;
    ID3D11SamplerState *Sampler;
    UINT Width;
    UINT Height;
    ULONGLONG Frame;
    GpuCacheEntry Cache[GpuCacheEntries];
};

class D3DPainter : public IPainter
{
public:
    D3DPainter(SurfaceGpu *gpu, UINT dpi, FontSet *fonts, const RECT &scissorPx);
    ~D3DPainter();

    BOOL IsValid() const { return m_Valid; }
    void Clear(Argb color);
    void FillRect(const RectF &rc, Argb color);
    void FillRoundRect(const RectF &rc, float radius, Argb color);
    void StrokeRoundRect(const RectF &rc, float radius, Argb color, float width);
    void FillEllipse(const RectF &rc, Argb color);
    void StrokeArc(const RectF &rc, float startDegrees, float sweepDegrees, Argb color, float width);
    void Line(PointF from, PointF to, Argb color, float width);
    void Text(LPCWSTR text, const RectF &rc, LIBER_TYPE style, Argb color, UINT flags);
    SIZE MeasureText(LPCWSTR text, LIBER_TYPE style, float maxWidth);
    void Icon(LPCWSTR name, const RectF &rc, Argb color);
    void Image(HICON icon, const RectF &rc);
    void PushClip(const RectF &rc, float radius);
    void PopClip();

private:
    struct Clip
    {
        float Rect[4];
        float Radius;
        BOOL Enabled;
        BOOL Empty;
    };

    float Px(float dip) const { return dip * m_Scale; }
    void Mask(UINT kind, LPCWSTR name, LPCWSTR text, LIBER_TYPE style, UINT flags, HICON icon, const RectF &rc,
              Argb color);
    void Draw(GpuShape shape, const float quad[4], const float rect[4], float radius, float width, Argb color,
              const float arc[2], ID3D11ShaderResourceView *view, const float uv[4], BOOL copy);
    ID3D11ShaderResourceView *Raster(LPCWSTR key, UINT width, UINT height, UINT kind, LPCWSTR text, LIBER_TYPE style,
                                     UINT flags, HICON icon, const RectF &local);

    SurfaceGpu *m_Gpu;
    FontSet *m_Fonts;
    UINT m_Dpi;
    float m_Scale;
    BOOL m_Valid;
    Clip m_Clips[16];
    UINT m_ClipDepth;
};

BOOL GpuEnsureCanvas(SurfaceGpu *gpu, UINT width, UINT height, BOOL *recreated);
BOOL GpuHasCanvas(const SurfaceGpu *gpu);
BOOL GpuDeviceLost(SurfaceGpu *gpu);
BOOL GpuPresentCanvas(SurfaceGpu *gpu);
BOOL GpuReadCanvas(SurfaceGpu *gpu, DWORD *pixels, UINT stride);

}
