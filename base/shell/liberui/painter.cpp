/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GDI+ painter into premultiplied 32-bit surfaces and the type ramp fonts
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include <gdiplus.h>
#include <shellframe.h>

namespace LiberUI
{

static LONG g_StartupCount;
static ULONG_PTR g_GdiplusToken;

BOOL Startup()
{
    if (InterlockedIncrement(&g_StartupCount) > 1)
        return TRUE;

    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_GdiplusToken, &input, NULL) != Gdiplus::Ok)
    {
        InterlockedDecrement(&g_StartupCount);
        g_GdiplusToken = 0;
        return FALSE;
    }
    return TRUE;
}

void Shutdown()
{
    LONG count = InterlockedDecrement(&g_StartupCount);
    LUI_ASSERT(count >= 0);
    if (count == 0 && g_GdiplusToken)
    {
        Gdiplus::GdiplusShutdown(g_GdiplusToken);
        g_GdiplusToken = 0;
    }
}

int ScaleToDpi(int value, UINT dpi)
{
    LUI_ASSERT(dpi != 0);
    return MulDiv(value, (int)(dpi ? dpi : 96), 96);
}

static Gdiplus::Color ToColor(Argb color)
{
    return Gdiplus::Color(ArgbA(color), ArgbR(color), ArgbG(color), ArgbB(color));
}

static Gdiplus::RectF ToGdiplus(const RectF &rc)
{
    return Gdiplus::RectF(rc.X, rc.Y, rc.Width, rc.Height);
}

static void RoundPath(Gdiplus::GraphicsPath &path, const RectF &rc, float radius)
{
    float r = radius;
    if (r > rc.Width / 2.0f)
        r = rc.Width / 2.0f;
    if (r > rc.Height / 2.0f)
        r = rc.Height / 2.0f;
    if (r <= 0.0f)
    {
        path.AddRectangle(ToGdiplus(rc));
        return;
    }

    float d = r * 2.0f;
    path.AddArc(rc.X, rc.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rc.X + rc.Width - d, rc.Y + rc.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rc.X, rc.Y + rc.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

FontSet::FontSet()
    : m_Fallback(FALSE)
{
    ZeroMemory(m_Fonts, sizeof(m_Fonts));
    ZeroMemory(m_Faces, sizeof(m_Faces));
    ZeroMemory(m_Styles, sizeof(m_Styles));
    lstrcpynW(m_Family, LIBER_FONT_UI, LF_FACESIZE);
}

FontSet::~FontSet()
{
    Reset();
}

void FontSet::Reset()
{
    for (UINT i = 0; i < LIBER_TYPE_COUNT; i++)
    {
        delete (Gdiplus::Font *)m_Fonts[i];
        m_Fonts[i] = NULL;
    }
}

static BOOL FamilyAvailable(LPCWSTR family)
{
    Gdiplus::FontFamily test(family, NULL);
    return test.GetLastStatus() == Gdiplus::Ok && test.IsAvailable();
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

void FontSet::Face(LIBER_TYPE style, LPCWSTR *face, INT *fontStyle) const
{
    LUI_ASSERT(style >= 0 && style < LIBER_TYPE_COUNT);
    if (style < 0 || style >= LIBER_TYPE_COUNT)
        style = LIBER_TYPE_BODY;
    LUI_ASSERT(m_Fonts[style] != NULL);
    *face = m_Faces[style];
    *fontStyle = m_Styles[style];
}

float FontSet::Tracking(LIBER_TYPE style) const
{
    LUI_ASSERT(style >= 0 && style < LIBER_TYPE_COUNT);
    if (style < 0 || style >= LIBER_TYPE_COUNT)
        return 0.0f;
    const LIBER_TYPE_STYLE &ramp = LiberTypeRamp[style];
    return ramp.Size * ramp.TrackingPermille / 1000.0f;
}

void *FontSet::Font(LIBER_TYPE style)
{
    LUI_ASSERT(style >= 0 && style < LIBER_TYPE_COUNT);
    if (style < 0 || style >= LIBER_TYPE_COUNT)
        style = LIBER_TYPE_BODY;
    if (m_Fonts[style])
        return m_Fonts[style];

    const LIBER_TYPE_STYLE &ramp = LiberTypeRamp[style];
    WCHAR face[LF_FACESIZE + 16];
    INT fontStyle = Gdiplus::FontStyleRegular;

    lstrcpynW(face, m_Family, LF_FACESIZE);
    if (ramp.Weight >= 600)
    {
        WCHAR semibold[LF_FACESIZE + 16];
        wsprintfW(semibold, L"%s Semibold", m_Family);
        if (FamilyAvailable(semibold))
            lstrcpynW(face, semibold, LF_FACESIZE);
        else if (!lstrcmpiW(m_Family, LIBER_FONT_UI) && FamilyAvailable(LIBER_FONT_UI_SEMIBOLD))
            lstrcpynW(face, LIBER_FONT_UI_SEMIBOLD, LF_FACESIZE);
        else
            fontStyle = Gdiplus::FontStyleBold;
    }
    if (!FamilyAvailable(face))
        lstrcpynW(face, L"Tahoma", LF_FACESIZE);

    Gdiplus::FontFamily family(face, NULL);
    Gdiplus::Font *font = new Gdiplus::Font(&family, (Gdiplus::REAL)ramp.Size, fontStyle, Gdiplus::UnitPixel);
    if (font && font->GetLastStatus() != Gdiplus::Ok)
    {
        delete font;
        font = NULL;
    }
    m_Fonts[style] = font;
    lstrcpynW(m_Faces[style], face, LF_FACESIZE);
    m_Styles[style] = fontStyle;
    return font;
}

#define GRAPHICS ((Gdiplus::Graphics *)m_Graphics)

GdiplusPainter::GdiplusPainter(void *bits, int width, int height, int stride, UINT dpi, FontSet *fonts)
    : m_Bitmap(NULL), m_Graphics(NULL), m_Fonts(fonts), m_ClipDepth(0)
{
    LUI_ASSERT(bits && width > 0 && height > 0 && stride >= width * 4 && dpi != 0);
    if (!bits || width <= 0 || height <= 0 || stride < width * 4)
        return;

    Gdiplus::Bitmap *bitmap = new Gdiplus::Bitmap(width, height, stride, PixelFormat32bppPARGB, (BYTE *)bits);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
    {
        delete bitmap;
        return;
    }

    Gdiplus::Graphics *graphics = Gdiplus::Graphics::FromImage(bitmap);
    if (!graphics || graphics->GetLastStatus() != Gdiplus::Ok)
    {
        delete graphics;
        delete bitmap;
        return;
    }

    graphics->SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    graphics->ScaleTransform(dpi / 96.0f, dpi / 96.0f);

    m_Bitmap = bitmap;
    m_Graphics = graphics;
}

GdiplusPainter::~GdiplusPainter()
{
    LUI_ASSERT(m_ClipDepth == 0);
    while (m_ClipDepth)
        PopClip();
    delete GRAPHICS;
    delete (Gdiplus::Bitmap *)m_Bitmap;
}

void GdiplusPainter::Clear(Argb color)
{
    if (!m_Graphics)
        return;

    Gdiplus::RectF bounds;
    Gdiplus::SolidBrush brush(ToColor(color));
    GRAPHICS->GetVisibleClipBounds(&bounds);
    GRAPHICS->SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
    GRAPHICS->FillRectangle(&brush, bounds);
    GRAPHICS->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
}

void GdiplusPainter::FillRect(const RectF &rc, Argb color)
{
    if (!m_Graphics)
        return;
    Gdiplus::SolidBrush brush(ToColor(color));
    GRAPHICS->FillRectangle(&brush, ToGdiplus(rc));
}

void GdiplusPainter::FillRoundRect(const RectF &rc, float radius, Argb color)
{
    if (!m_Graphics || rc.Width <= 0.0f || rc.Height <= 0.0f)
        return;
    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
    Gdiplus::SolidBrush brush(ToColor(color));
    RoundPath(path, rc, radius);
    GRAPHICS->FillPath(&brush, &path);
}

void GdiplusPainter::StrokeRoundRect(const RectF &rc, float radius, Argb color, float width)
{
    if (!m_Graphics || rc.Width <= width || rc.Height <= width)
        return;
    RectF inset = { rc.X + width / 2.0f, rc.Y + width / 2.0f, rc.Width - width, rc.Height - width };
    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
    Gdiplus::Pen pen(ToColor(color), width);
    RoundPath(path, inset, radius - width / 2.0f);
    GRAPHICS->DrawPath(&pen, &path);
}

void GdiplusPainter::FillEllipse(const RectF &rc, Argb color)
{
    if (!m_Graphics)
        return;
    Gdiplus::SolidBrush brush(ToColor(color));
    GRAPHICS->FillEllipse(&brush, ToGdiplus(rc));
}

void GdiplusPainter::StrokeArc(const RectF &rc, float startDegrees, float sweepDegrees, Argb color, float width)
{
    if (!m_Graphics || sweepDegrees == 0.0f)
        return;
    Gdiplus::Pen pen(ToColor(color), width);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    GRAPHICS->DrawArc(&pen, ToGdiplus(rc), startDegrees, sweepDegrees);
}

void GdiplusPainter::Line(PointF from, PointF to, Argb color, float width)
{
    if (!m_Graphics)
        return;
    Gdiplus::Pen pen(ToColor(color), width);
    GRAPHICS->DrawLine(&pen, from.X, from.Y, to.X, to.Y);
}

static void SetupFormat(Gdiplus::StringFormat &format, UINT flags)
{
    format.SetAlignment((flags & TextCenter) ? Gdiplus::StringAlignmentCenter
                        : (flags & TextRight) ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentNear);
    format.SetLineAlignment((flags & TextMiddle) ? Gdiplus::StringAlignmentCenter
                            : (flags & TextBottom) ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentNear);
    format.SetTrimming((flags & TextEllipsis) ? Gdiplus::StringTrimmingEllipsisCharacter : Gdiplus::StringTrimmingNone);
    if (!(flags & TextWrap))
        format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
}

void GdiplusPainter::Text(LPCWSTR text, const RectF &rc, LIBER_TYPE style, Argb color, UINT flags)
{
    if (!m_Graphics || !text || !*text || !m_Fonts)
        return;

    Gdiplus::Font *font = (Gdiplus::Font *)m_Fonts->Font(style);
    if (!font)
        return;

    /* FIXME: the type ramp's tracking is not applied, GDI+ has no letter spacing */
    Gdiplus::StringFormat format;
    Gdiplus::SolidBrush brush(ToColor(color));
    SetupFormat(format, flags);
    GRAPHICS->DrawString(text, -1, font, ToGdiplus(rc), &format, &brush);
}

SIZE GdiplusPainter::MeasureText(LPCWSTR text, LIBER_TYPE style, float maxWidth)
{
    SIZE size = { 0, 0 };

    LUI_ASSERT(style >= 0 && style < LIBER_TYPE_COUNT);
    if (style < 0 || style >= LIBER_TYPE_COUNT)
        style = LIBER_TYPE_BODY;
    size.cy = LiberTypeRamp[style].LineHeight;
    if (!m_Graphics || !text || !*text || !m_Fonts)
        return size;

    if (!m_Fonts->Font(style))
        return size;

    LPCWSTR face;
    INT fontStyle;
    m_Fonts->Face(style, &face, &fontStyle);

    Gdiplus::GpFontFamily *family = NULL;
    Gdiplus::GpFont *font = NULL;
    Gdiplus::GpStringFormat *format = NULL;
    Gdiplus::GpGraphics *graphics = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    INT formatFlags = Gdiplus::StringFormatFlagsMeasureTrailingSpaces;

    if (maxWidth <= 0.0f)
        formatFlags |= Gdiplus::StringFormatFlagsNoWrap;
    if (dc && Gdiplus::DllExports::GdipCreateFromHDC(dc, &graphics) == Gdiplus::Ok &&
        Gdiplus::DllExports::GdipCreateFontFamilyFromName(face, NULL, &family) == Gdiplus::Ok &&
        Gdiplus::DllExports::GdipCreateFont(family, (Gdiplus::REAL)LiberTypeRamp[style].Size, fontStyle,
                                            Gdiplus::UnitPixel, &font) == Gdiplus::Ok &&
        Gdiplus::DllExports::GdipCreateStringFormat(formatFlags, LANG_NEUTRAL, &format) == Gdiplus::Ok)
    {
        Gdiplus::RectF layout(0.0f, 0.0f, maxWidth > 0.0f ? maxWidth : 100000.0f, 100000.0f);
        Gdiplus::RectF bounds;
        INT fitted = 0, lines = 0;

        Gdiplus::DllExports::GdipSetTextRenderingHint(graphics, Gdiplus::TextRenderingHintAntiAliasGridFit);
        if (Gdiplus::DllExports::GdipMeasureString(graphics, text, -1, font, &layout, format, &bounds, &fitted,
                                                   &lines) == Gdiplus::Ok)
        {
            size.cx = (LONG)ceil(bounds.Width);
            size.cy = LiberTypeRamp[style].LineHeight * (lines > 0 ? lines : 1);
        }
    }

    if (format)
        Gdiplus::DllExports::GdipDeleteStringFormat(format);
    if (font)
        Gdiplus::DllExports::GdipDeleteFont(font);
    if (family)
        Gdiplus::DllExports::GdipDeleteFontFamily(family);
    if (graphics)
        Gdiplus::DllExports::GdipDeleteGraphics(graphics);
    if (dc)
        DeleteDC(dc);
    return size;
}

void GdiplusPainter::Icon(LPCWSTR name, const RectF &rc, Argb color)
{
    float viewBox = 0.0f;

    if (!m_Graphics || !name)
        return;

    Gdiplus::GraphicsPath *path = ShellFrame::IconPath(name, &viewBox);
    if (!path || viewBox <= 0.0f)
        return;

    float scale = (rc.Width < rc.Height ? rc.Width : rc.Height) / viewBox;
    Gdiplus::SolidBrush brush(ToColor(color));
    Gdiplus::GraphicsState state = GRAPHICS->Save();
    GRAPHICS->TranslateTransform(rc.X + (rc.Width - viewBox * scale) / 2.0f, rc.Y + (rc.Height - viewBox * scale) / 2.0f);
    GRAPHICS->ScaleTransform(scale, scale);
    GRAPHICS->FillPath(&brush, path);
    GRAPHICS->Restore(state);
}

void GdiplusPainter::PushClip(const RectF &rc, float radius)
{
    LUI_ASSERT(m_ClipDepth < MaxClips);
    if (!m_Graphics || m_ClipDepth >= MaxClips)
        return;

    m_Clips[m_ClipDepth++] = GRAPHICS->Save();
    Gdiplus::GraphicsPath path(Gdiplus::FillModeAlternate);
    RoundPath(path, rc, radius);
    GRAPHICS->SetClip(&path, Gdiplus::CombineModeIntersect);
}

void GdiplusPainter::PopClip()
{
    LUI_ASSERT(m_ClipDepth > 0);
    if (!m_Graphics || m_ClipDepth == 0)
        return;
    GRAPHICS->Restore(m_Clips[--m_ClipDepth]);
}

}
