/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DirectWrite type ramp formats and glyph rasterization for the Direct3D painter
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"
#include "raster.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#endif
#include <dwrite_1.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace LiberUI
{

typedef HRESULT(WINAPI *PFN_LIBER_DWRITECREATEFACTORY)(DWRITE_FACTORY_TYPE, REFIID, IUnknown **);

static LONG g_StartupCount;
static HMODULE g_DWriteModule;
static IDWriteFactory *g_DWrite;
static FontSet *g_MeasureFonts;

BOOL Startup()
{
    if (InterlockedIncrement(&g_StartupCount) > 1)
        return TRUE;

    PFN_LIBER_DWRITECREATEFACTORY create;
    g_DWriteModule = LoadLibraryW(L"dwrite.dll");
    create = g_DWriteModule ? (PFN_LIBER_DWRITECREATEFACTORY)GetProcAddress(g_DWriteModule, "DWriteCreateFactory") : NULL;
    if (!create || FAILED(create(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&g_DWrite)))
    {
        g_DWrite = NULL;
        if (g_DWriteModule)
            FreeLibrary(g_DWriteModule);
        g_DWriteModule = NULL;
        InterlockedDecrement(&g_StartupCount);
        return FALSE;
    }
    return TRUE;
}

void Shutdown()
{
    LONG count = InterlockedDecrement(&g_StartupCount);
    LUI_ASSERT(count >= 0);
    if (count == 0)
    {
        delete g_MeasureFonts;
        g_MeasureFonts = NULL;
        if (g_DWrite)
            g_DWrite->Release();
        g_DWrite = NULL;
        if (g_DWriteModule)
            FreeLibrary(g_DWriteModule);
        g_DWriteModule = NULL;
    }
}

int ScaleToDpi(int value, UINT dpi)
{
    LUI_ASSERT(dpi != 0);
    return MulDiv(value, (int)(dpi ? dpi : 96), 96);
}

static BOOL FamilyAvailable(LPCWSTR family)
{
    IDWriteFontCollection *collection = NULL;
    UINT32 index = 0;
    BOOL exists = FALSE;

    if (!g_DWrite || !family || !*family || FAILED(g_DWrite->GetSystemFontCollection(&collection, FALSE)))
        return FALSE;
    if (FAILED(collection->FindFamilyName(family, &index, &exists)))
        exists = FALSE;
    collection->Release();
    return exists;
}

FontSet::FontSet() : m_Fallback(FALSE)
{
    ZeroMemory(m_Formats, sizeof(m_Formats));
    lstrcpynW(m_Family, LIBER_FONT_UI, LF_FACESIZE);
}

FontSet::~FontSet()
{
    Reset();
}

UINT FontSet::LoadedCount() const
{
    UINT count = 0;
    for (UINT i = 0; i < LIBER_TYPE_COUNT; i++)
        count += m_Formats[i] != NULL;
    return count;
}

void FontSet::Reset()
{
    for (UINT i = 0; i < LIBER_TYPE_COUNT; i++)
    {
        if (m_Formats[i])
            ((IDWriteTextFormat *)m_Formats[i])->Release();
        m_Formats[i] = NULL;
    }
}

BOOL FontSet::SetFamily(LPCWSTR family)
{
    Reset();
    if (family && *family && FamilyAvailable(family))
    {
        lstrcpynW(m_Family, family, LF_FACESIZE);
        m_Fallback = FALSE;
        return TRUE;
    }
    lstrcpynW(m_Family, LIBER_FONT_UI, LF_FACESIZE);
    m_Fallback = TRUE;
    return FALSE;
}

float FontSet::Tracking(LIBER_TYPE style) const
{
    LUI_ASSERT(style >= 0 && style < LIBER_TYPE_COUNT);
    if (style < 0 || style >= LIBER_TYPE_COUNT)
        return 0.0f;
    const LIBER_TYPE_STYLE &ramp = LiberTypeRamp[style];
    return ramp.Size * ramp.TrackingPermille / 1000.0f;
}

void *FontSet::Format(LIBER_TYPE style)
{
    LUI_ASSERT(style >= 0 && style < LIBER_TYPE_COUNT);
    if (style < 0 || style >= LIBER_TYPE_COUNT)
        style = LIBER_TYPE_BODY;
    if (m_Formats[style] || !g_DWrite)
        return m_Formats[style];

    const LIBER_TYPE_STYLE &ramp = LiberTypeRamp[style];
    LPCWSTR family = FamilyAvailable(m_Family) ? m_Family : FamilyAvailable(LIBER_FONT_UI) ? LIBER_FONT_UI : L"Tahoma";
    IDWriteTextFormat *format = NULL;
    if (FAILED(g_DWrite->CreateTextFormat(family, NULL, (DWRITE_FONT_WEIGHT)ramp.Weight, DWRITE_FONT_STYLE_NORMAL,
                                          DWRITE_FONT_STRETCH_NORMAL, (FLOAT)ramp.Size, L"", &format)))
    {
        return NULL;
    }
    format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, (FLOAT)ramp.LineHeight, ramp.LineHeight * 0.8f);
    m_Formats[style] = format;
    return format;
}

static IDWriteTextLayout *CreateLayout(FontSet *fonts, LPCWSTR text, LIBER_TYPE style, UINT flags, float width,
                                       float height)
{
    IDWriteTextFormat *format = fonts ? (IDWriteTextFormat *)fonts->Format(style) : NULL;
    IDWriteTextLayout *layout = NULL;

    if (!g_DWrite || !format || !text)
        return NULL;
    if (FAILED(g_DWrite->CreateTextLayout(text, (UINT32)lstrlenW(text), format, width > 0.0f ? width : 0.0f,
                                          height > 0.0f ? height : 0.0f, &layout)))
    {
        return NULL;
    }
    layout->SetTextAlignment((flags & TextCenter)  ? DWRITE_TEXT_ALIGNMENT_CENTER
                             : (flags & TextRight) ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                   : DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment((flags & TextMiddle)   ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                  : (flags & TextBottom) ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                                         : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    layout->SetWordWrapping((flags & TextWrap) ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (flags & TextEllipsis)
    {
        IDWriteInlineObject *sign = NULL;
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        if (SUCCEEDED(g_DWrite->CreateEllipsisTrimmingSign(format, &sign)))
        {
            layout->SetTrimming(&trimming, sign);
            sign->Release();
        }
    }
    float tracking = fonts->Tracking(style);
    if (tracking != 0.0f)
    {
        IDWriteTextLayout1 *layout1 = NULL;
        DWRITE_TEXT_RANGE all = { 0, (UINT32)lstrlenW(text) };
        if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), (void **)&layout1)))
        {
            layout1->SetCharacterSpacing(tracking / 2.0f, tracking / 2.0f, 0.0f, all);
            layout1->Release();
        }
    }
    return layout;
}

SIZE MeasureTextDip(FontSet *fonts, LPCWSTR text, LIBER_TYPE style, float maxWidth)
{
    SIZE size = { 0, 0 };
    DWRITE_TEXT_METRICS metrics;

    LUI_ASSERT(style >= 0 && style < LIBER_TYPE_COUNT);
    if (style < 0 || style >= LIBER_TYPE_COUNT)
        style = LIBER_TYPE_BODY;
    size.cy = LiberTypeRamp[style].LineHeight;
    if (!text || !*text)
        return size;
    IDWriteTextLayout *layout = CreateLayout(fonts, text, style, maxWidth > 0.0f ? TextWrap : 0,
                                             maxWidth > 0.0f ? maxWidth : 100000.0f, 100000.0f);
    if (!layout)
        return size;
    if (SUCCEEDED(layout->GetMetrics(&metrics)))
    {
        size.cx = (LONG)ceilf(metrics.widthIncludingTrailingWhitespace);
        size.cy = (LONG)ceilf(max(metrics.height, (float)LiberTypeRamp[style].LineHeight));
    }
    layout->Release();
    return size;
}

SIZE MeasureText(LPCWSTR text, LIBER_TYPE style, float maxWidth)
{
    LUI_ASSERT(g_StartupCount > 0 || !g_MeasureFonts);
    if (!g_MeasureFonts && g_DWrite)
        g_MeasureFonts = new FontSet();
    return MeasureTextDip(g_MeasureFonts, text, style, maxWidth);
}

class GlyphRasterizer : public IDWriteTextRenderer
{
public:
    GlyphRasterizer(BYTE *coverage, UINT width, UINT height, float scale)
        : m_Coverage(coverage), m_Width(width), m_Height(height), m_Scale(scale), m_Runs(0)
    {
    }

    UINT Runs() const { return m_Runs; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object)
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) ||
            riid == __uuidof(IDWriteTextRenderer))
        {
            *object = this;
            return S_OK;
        }
        *object = NULL;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void *context, BOOL *disabled)
    {
        UNREFERENCED_PARAMETER(context);
        *disabled = FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void *context, DWRITE_MATRIX *transform)
    {
        static const DWRITE_MATRIX Identity = { 1, 0, 0, 1, 0, 0 };
        UNREFERENCED_PARAMETER(context);
        *transform = Identity;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void *context, FLOAT *pixelsPerDip)
    {
        UNREFERENCED_PARAMETER(context);
        *pixelsPerDip = m_Scale;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void *context, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mode,
                                           const DWRITE_GLYPH_RUN *run, const DWRITE_GLYPH_RUN_DESCRIPTION *description,
                                           IUnknown *effect)
    {
        IDWriteGlyphRunAnalysis *analysis = NULL;
        RECT bounds;

        UNREFERENCED_PARAMETER(context);
        UNREFERENCED_PARAMETER(description);
        UNREFERENCED_PARAMETER(effect);
        if (FAILED(g_DWrite->CreateGlyphRunAnalysis(run, m_Scale, NULL, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, mode, x,
                                                    y, &analysis)))
        {
            return S_OK;
        }
        if (SUCCEEDED(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds)) &&
            bounds.right > bounds.left && bounds.bottom > bounds.top)
        {
            UINT w = (UINT)(bounds.right - bounds.left), h = (UINT)(bounds.bottom - bounds.top);
            BYTE *rgb = (BYTE *)LocalAlloc(LPTR, (SIZE_T)w * h * 3);
            if (rgb && SUCCEEDED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, rgb, w * h * 3)))
            {
                for (UINT row = 0; row < h; row++)
                {
                    LONG py = bounds.top + (LONG)row;
                    if (py < 0 || py >= (LONG)m_Height)
                        continue;
                    for (UINT col = 0; col < w; col++)
                    {
                        LONG px = bounds.left + (LONG)col;
                        if (px < 0 || px >= (LONG)m_Width)
                            continue;
                        const BYTE *p = rgb + ((SIZE_T)row * w + col) * 3;
                        UINT value = ((UINT)p[0] + p[1] + p[2] + 1) / 3;
                        BYTE &out = m_Coverage[(SIZE_T)py * m_Width + px];
                        out = (BYTE)min(255u, (UINT)out + value);
                    }
                }
                m_Runs++;
            }
            if (rgb)
                LocalFree(rgb);
        }
        analysis->Release();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void *, FLOAT, FLOAT, const DWRITE_UNDERLINE *, IUnknown *) { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void *, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH *, IUnknown *)
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void *context, FLOAT x, FLOAT y, IDWriteInlineObject *object, BOOL sideways,
                                               BOOL rtl, IUnknown *effect)
    {
        return object ? object->Draw(context, this, x, y, sideways, rtl, effect) : S_OK;
    }

private:
    BYTE *m_Coverage;
    UINT m_Width;
    UINT m_Height;
    float m_Scale;
    UINT m_Runs;
};

BOOL RasterTextCoverage(FontSet *fonts, LPCWSTR text, LIBER_TYPE style, UINT flags, const RectF &local, float scale,
                        BYTE *coverage, UINT width, UINT height)
{
    LUI_ASSERT(coverage && width && height && scale > 0.0f);
    if (!coverage || !text || !*text || scale <= 0.0f)
        return FALSE;
    IDWriteTextLayout *layout = CreateLayout(fonts, text, style, flags, local.Width, local.Height);
    if (!layout)
        return FALSE;
    GlyphRasterizer rasterizer(coverage, width, height, scale);
    HRESULT hr = layout->Draw(NULL, &rasterizer, local.X, local.Y);
    layout->Release();
    return SUCCEEDED(hr) && rasterizer.Runs() > 0;
}

}
