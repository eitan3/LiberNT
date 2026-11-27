/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Direct3D 11 painter: shapes in shaders, text and icons as cached textures
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"
#include "gpu.h"

#include <strsafe.h>

namespace LiberUI
{

enum
{
    RasterText = 1,
    RasterIcon,
    RasterImage
};

static void ToPremultiplied(Argb color, float out[4])
{
    float a = ArgbA(color) / 255.0f;
    out[0] = ArgbR(color) / 255.0f * a;
    out[1] = ArgbG(color) / 255.0f * a;
    out[2] = ArgbB(color) / 255.0f * a;
    out[3] = a;
}

D3DPainter::D3DPainter(SurfaceGpu *gpu, UINT dpi, FontSet *fonts, const RECT &scissorPx)
    : m_Gpu(gpu), m_Fonts(fonts), m_Dpi(dpi ? dpi : 96), m_Scale((dpi ? dpi : 96) / 96.0f), m_Valid(FALSE),
      m_ClipDepth(0)
{
    ZeroMemory(m_Clips, sizeof(m_Clips));
    LUI_ASSERT(gpu && gpu->CanvasView);
    if (!gpu || !gpu->CanvasView)
        return;

    ID3D11DeviceContext *context = gpu->Context;
    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)gpu->Width, (float)gpu->Height, 0.0f, 1.0f };
    D3D11_RECT scissor = { scissorPx.left, scissorPx.top, scissorPx.right, scissorPx.bottom };

    context->OMSetRenderTargets(1, &gpu->CanvasView, NULL);
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
    gpu->Frame++;
    m_Valid = TRUE;
}

D3DPainter::~D3DPainter()
{
    LUI_ASSERT(m_ClipDepth == 0);
    if (m_Valid)
    {
        ID3D11ShaderResourceView *none = NULL;
        m_Gpu->Context->PSSetShaderResources(0, 1, &none);
    }
}

void D3DPainter::Draw(GpuShape shape, const float quad[4], const float rect[4], float radius, float width, Argb color,
                      const float arc[2], ID3D11ShaderResourceView *view, const float uv[4], BOOL copy)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    GpuDrawConstants constants;

    if (!m_Valid)
        return;
    ZeroMemory(&constants, sizeof(constants));
    CopyMemory(constants.Rect, quad, sizeof(constants.Rect));
    CopyMemory(constants.Shape, rect, sizeof(constants.Shape));
    constants.Params[0] = radius;
    constants.Params[1] = width;
    constants.Params[2] = (float)shape;
    if (arc)
    {
        constants.Arc[0] = arc[0];
        constants.Arc[1] = arc[1];
    }
    ToPremultiplied(color, constants.Color);
    if (m_ClipDepth)
    {
        const Clip &clip = m_Clips[m_ClipDepth - 1];
        CopyMemory(constants.ClipRect, clip.Rect, sizeof(constants.ClipRect));
        constants.ClipParams[0] = clip.Radius;
        constants.ClipParams[1] = clip.Enabled ? 1.0f : 0.0f;
        constants.ClipParams[2] = clip.Empty ? 1.0f : 0.0f;
    }
    constants.Target[0] = (float)m_Gpu->Width;
    constants.Target[1] = (float)m_Gpu->Height;
    if (uv)
        CopyMemory(constants.UvRect, uv, sizeof(constants.UvRect));

    if (FAILED(m_Gpu->Context->Map(m_Gpu->Constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    CopyMemory(mapped.pData, &constants, sizeof(constants));
    m_Gpu->Context->Unmap(m_Gpu->Constants, 0);

    const float factor[4] = { 0, 0, 0, 0 };
    m_Gpu->Context->OMSetBlendState(copy ? m_Gpu->BlendCopy : m_Gpu->BlendOver, factor, 0xFFFFFFFF);
    m_Gpu->Context->PSSetShaderResources(0, 1, &view);
    m_Gpu->Context->Draw(4, 0);
}

void D3DPainter::Clear(Argb color)
{
    float quad[4] = { 0, 0, (float)m_Gpu->Width, (float)m_Gpu->Height };
    UINT depth = m_ClipDepth;

    m_ClipDepth = 0;
    Draw(GpuFillRound, quad, quad, 0.0f, 0.0f, color, NULL, NULL, NULL, TRUE);
    m_ClipDepth = depth;
}

void D3DPainter::FillRect(const RectF &rc, Argb color)
{
    FillRoundRect(rc, 0.0f, color);
}

void D3DPainter::FillRoundRect(const RectF &rc, float radius, Argb color)
{
    if (rc.Width <= 0.0f || rc.Height <= 0.0f)
        return;
    float shape[4] = { Px(rc.X), Px(rc.Y), Px(rc.X + rc.Width), Px(rc.Y + rc.Height) };
    float quad[4] = { shape[0] - 1, shape[1] - 1, shape[2] + 1, shape[3] + 1 };
    Draw(GpuFillRound, quad, shape, Px(radius), 0.0f, color, NULL, NULL, NULL, FALSE);
}

void D3DPainter::StrokeRoundRect(const RectF &rc, float radius, Argb color, float width)
{
    if (rc.Width <= width || rc.Height <= width)
        return;
    float shape[4] = { Px(rc.X), Px(rc.Y), Px(rc.X + rc.Width), Px(rc.Y + rc.Height) };
    float quad[4] = { shape[0] - 1, shape[1] - 1, shape[2] + 1, shape[3] + 1 };
    Draw(GpuStrokeRound, quad, shape, Px(radius), Px(width), color, NULL, NULL, NULL, FALSE);
}

void D3DPainter::FillEllipse(const RectF &rc, Argb color)
{
    if (rc.Width <= 0.0f || rc.Height <= 0.0f)
        return;
    float shape[4] = { Px(rc.X), Px(rc.Y), Px(rc.X + rc.Width), Px(rc.Y + rc.Height) };
    float quad[4] = { shape[0] - 1, shape[1] - 1, shape[2] + 1, shape[3] + 1 };
    Draw(GpuFillEllipse, quad, shape, 0.0f, 0.0f, color, NULL, NULL, NULL, FALSE);
}

void D3DPainter::StrokeArc(const RectF &rc, float startDegrees, float sweepDegrees, Argb color, float width)
{
    const float radians = 3.14159265f / 180.0f;

    if (sweepDegrees == 0.0f || rc.Width <= 0.0f)
        return;
    if (sweepDegrees < 0.0f)
    {
        startDegrees += sweepDegrees;
        sweepDegrees = -sweepDegrees;
    }
    if (sweepDegrees > 360.0f)
        sweepDegrees = 360.0f;
    float shape[4] = { Px(rc.X), Px(rc.Y), Px(rc.X + rc.Width), Px(rc.Y + rc.Height) };
    float pad = Px(width) / 2 + 1;
    float quad[4] = { shape[0] - pad, shape[1] - pad, shape[2] + pad, shape[3] + pad };
    float arc[2] = { startDegrees * radians, sweepDegrees * radians };
    Draw(GpuStrokeArc, quad, shape, 0.0f, Px(width), color, arc, NULL, NULL, FALSE);
}

void D3DPainter::Line(PointF from, PointF to, Argb color, float width)
{
    float shape[4] = { Px(from.X), Px(from.Y), Px(to.X), Px(to.Y) };
    float pad = Px(width) / 2 + 1;
    float quad[4] = { min(shape[0], shape[2]) - pad, min(shape[1], shape[3]) - pad, max(shape[0], shape[2]) + pad,
                      max(shape[1], shape[3]) + pad };
    Draw(GpuLine, quad, shape, 0.0f, Px(width), color, NULL, NULL, NULL, FALSE);
}

ID3D11ShaderResourceView *D3DPainter::Raster(LPCWSTR key, UINT width, UINT height, UINT kind, LPCWSTR text,
                                             LIBER_TYPE style, UINT flags, HICON icon, const RectF &local)
{
    GpuCacheEntry *empty = NULL, *oldest = NULL, *slot;

    for (UINT i = 0; i < GpuCacheEntries; i++)
    {
        GpuCacheEntry &entry = m_Gpu->Cache[i];
        if (entry.View && entry.Width == width && entry.Height == height && !lstrcmpW(entry.Key, key))
        {
            entry.Used = m_Gpu->Frame;
            return entry.View;
        }
        if (!entry.View)
        {
            if (!empty)
                empty = &entry;
        }
        else if (!oldest || entry.Used < oldest->Used)
        {
            oldest = &entry;
        }
    }
    slot = empty ? empty : oldest;
    LUI_ASSERT(slot != NULL);
    if (!slot)
        return NULL;

    DWORD *bits = (DWORD *)LocalAlloc(LPTR, (SIZE_T)width * height * sizeof(DWORD));
    if (!bits)
        return NULL;
    {
        GdiplusPainter raster(bits, (int)width, (int)height, (int)width * 4, m_Dpi, m_Fonts);
        if (!raster.IsValid())
        {
            LocalFree(bits);
            return NULL;
        }
        if (kind == RasterText)
            raster.Text(text, local, style, 0xFFFFFFFF, flags);
        else if (kind == RasterIcon)
            raster.Icon(text, local, 0xFFFFFFFF);
        else
            raster.Image(icon, local);
    }

    D3D11_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = { bits, width * (UINT)sizeof(DWORD), 0 };
    ID3D11Texture2D *texture = NULL;
    ID3D11ShaderResourceView *view = NULL;
    if (SUCCEEDED(m_Gpu->Device->CreateTexture2D(&desc, &data, &texture)))
    {
        m_Gpu->Device->CreateShaderResourceView(texture, NULL, &view);
        texture->Release();
    }
    LocalFree(bits);
    if (!view)
        return NULL;

    if (slot->View)
        slot->View->Release();
    StringCchCopyW(slot->Key, _countof(slot->Key), key);
    slot->View = view;
    slot->Width = width;
    slot->Height = height;
    slot->Used = m_Gpu->Frame;
    return view;
}

static BOOL PixelBox(const RectF &rc, float scale, float box[4], UINT *width, UINT *height)
{
    box[0] = floorf(rc.X * scale);
    box[1] = floorf(rc.Y * scale);
    box[2] = ceilf((rc.X + rc.Width) * scale);
    box[3] = ceilf((rc.Y + rc.Height) * scale);
    if (box[2] <= box[0] || box[3] <= box[1] || box[2] - box[0] > 4096 || box[3] - box[1] > 4096)
        return FALSE;
    *width = (UINT)(box[2] - box[0]);
    *height = (UINT)(box[3] - box[1]);
    return TRUE;
}

static ULONGLONG HashText(LPCWSTR text)
{
    ULONGLONG hash = 1469598103934665603ULL;

    for (; text && *text; text++)
    {
        hash ^= (ULONGLONG)(USHORT)*text;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void D3DPainter::Mask(UINT kind, LPCWSTR name, LPCWSTR text, LIBER_TYPE style, UINT flags, HICON icon, const RectF &rc,
                      Argb color)
{
    float box[4], uv[4] = { 0, 0, 1, 1 };
    UINT width, height;
    WCHAR key[GpuCacheKeyChars];

    if (!PixelBox(rc, m_Scale, box, &width, &height))
        return;
    float offsetX = rc.X * m_Scale - box[0];
    float offsetY = rc.Y * m_Scale - box[1];
    RectF local = { offsetX / m_Scale, offsetY / m_Scale, rc.Width, rc.Height };
    StringCchPrintfW(key, _countof(key), L"%u|%u|%u|%u|%d|%d|%u|%p|%I64x|%I64x|%u", kind, (UINT)style, flags, m_Dpi,
                     (int)(offsetX * 4.0f + 0.5f), (int)(offsetY * 4.0f + 0.5f), (UINT)lstrlenW(name ? name : L""),
                     icon, HashText(name), HashText(m_Fonts ? m_Fonts->Family() : L""),
                     (UINT)(rc.Width * 4.0f + 0.5f));
    ID3D11ShaderResourceView *view = Raster(key, width, height, kind, text, style, flags, icon, local);
    if (view)
        Draw(kind == RasterImage ? GpuImage : GpuMask, box, box, 0.0f, 0.0f, color, NULL, view, uv, FALSE);
}

void D3DPainter::Text(LPCWSTR text, const RectF &rc, LIBER_TYPE style, Argb color, UINT flags)
{
    if (!text || !*text || !m_Fonts)
        return;
    Mask(RasterText, text, text, style, flags, NULL, rc, color);
}

SIZE D3DPainter::MeasureText(LPCWSTR text, LIBER_TYPE style, float maxWidth)
{
    DWORD pixel = 0;
    GdiplusPainter measure(&pixel, 1, 1, 4, 96, m_Fonts);
    return measure.MeasureText(text, style, maxWidth);
}

void D3DPainter::Icon(LPCWSTR name, const RectF &rc, Argb color)
{
    if (!name)
        return;
    Mask(RasterIcon, name, name, LIBER_TYPE_BODY, 0, NULL, rc, color);
}

void D3DPainter::Image(HICON icon, const RectF &rc)
{
    if (!icon)
        return;
    Mask(RasterImage, NULL, NULL, LIBER_TYPE_BODY, 0, icon, rc, 0xFFFFFFFF);
}

void D3DPainter::PushClip(const RectF &rc, float radius)
{
    LUI_ASSERT(m_ClipDepth < _countof(m_Clips));
    if (m_ClipDepth >= _countof(m_Clips))
        return;

    Clip clip;
    clip.Rect[0] = Px(rc.X);
    clip.Rect[1] = Px(rc.Y);
    clip.Rect[2] = Px(rc.X + rc.Width);
    clip.Rect[3] = Px(rc.Y + rc.Height);
    clip.Radius = Px(radius);
    clip.Enabled = TRUE;
    clip.Empty = FALSE;
    if (m_ClipDepth)
    {
        const Clip &outer = m_Clips[m_ClipDepth - 1];
        BOOL inside = clip.Rect[0] >= outer.Rect[0] && clip.Rect[1] >= outer.Rect[1] && clip.Rect[2] <= outer.Rect[2] &&
                      clip.Rect[3] <= outer.Rect[3];
        clip.Rect[0] = max(clip.Rect[0], outer.Rect[0]);
        clip.Rect[1] = max(clip.Rect[1], outer.Rect[1]);
        clip.Rect[2] = min(clip.Rect[2], outer.Rect[2]);
        clip.Rect[3] = min(clip.Rect[3], outer.Rect[3]);
        if (!inside)
            clip.Radius = max(clip.Radius, outer.Radius);
        clip.Empty = outer.Empty;
    }
    if (clip.Rect[2] <= clip.Rect[0] || clip.Rect[3] <= clip.Rect[1] || rc.Width <= 0.0f || rc.Height <= 0.0f)
        clip.Empty = TRUE;
    m_Clips[m_ClipDepth++] = clip;
}

void D3DPainter::PopClip()
{
    LUI_ASSERT(m_ClipDepth > 0);
    if (m_ClipDepth > 0)
        m_ClipDepth--;
}

}
