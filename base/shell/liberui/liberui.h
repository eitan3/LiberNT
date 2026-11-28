/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Design tokens, color math, animation, layout and drawing for the Liber desktop
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#include <windows.h>

#include <liberdesign/liber_tokens.h>

namespace LiberUI
{

typedef DWORD Argb;

struct Oklab
{
    double L, A, B;
};

struct Oklch
{
    double L, C, H;
};

struct LinearRgb
{
    double R, G, B;
};

inline Argb MakeArgb(BYTE a, BYTE r, BYTE g, BYTE b)
{
    return ((Argb)a << 24) | ((Argb)r << 16) | ((Argb)g << 8) | (Argb)b;
}
inline BYTE ArgbA(Argb c) { return (BYTE)(c >> 24); }
inline BYTE ArgbR(Argb c) { return (BYTE)(c >> 16); }
inline BYTE ArgbG(Argb c) { return (BYTE)(c >> 8); }
inline BYTE ArgbB(Argb c) { return (BYTE)c; }
inline COLORREF ArgbToColorRef(Argb c) { return RGB(ArgbR(c), ArgbG(c), ArgbB(c)); }

double SrgbToLinear(double value);
double LinearToSrgb(double value);
Oklab OklchToOklab(const Oklch &color);
Oklch OklabToOklch(const Oklab &color);
LinearRgb OklabToLinearRgb(const Oklab &color);
Oklab LinearRgbToOklab(const LinearRgb &color);
Argb OklchToArgb(const Oklch &color, double alpha);
Oklch ArgbToOklch(Argb color);
Argb WithAlpha(Argb color, double alpha);
Argb Premultiply(Argb color);
Argb BlendOver(Argb top, Argb bottom);
Argb Mix(Argb from, Argb to, double t);
double Luma(Argb color);
double NormalizeHue(double hue);
double HueDistance(double a, double b);

enum class ThemeMode
{
    Light,
    Dark
};

enum class SurfaceTint
{
    Off,
    Soft,
    Vivid
};

struct Accent
{
    double H, C, L;
};

struct GlassTint
{
    Oklch Color;
    double Alpha;
    Argb Value;
};

struct ThemeParams
{
    ThemeMode Mode;
    Accent AccentColor;
    SurfaceTint Tint;
};

Accent AccentPreset(LIBER_ACCENT_PRESET_ID id);
double AccentBaseLightness(double hue);
Accent AccentFromWallpaper(double hue, double chroma);
Oklch AccentInk(const Accent &accent);
double TintFactor(SurfaceTint tint, double accentChroma);
double WallpaperDim(double averageLuma);

class ThemeTokens
{
public:
    ThemeTokens();

    void Build(const ThemeParams &params);

    const ThemeParams &Params() const { return m_Params; }
    BOOL IsDark() const { return m_Params.Mode == ThemeMode::Dark; }
    double Tint() const { return m_Tint; }
    Argb Color(LIBER_COLOR id) const;
    const LIBER_GLASS &Glass() const;
    const LIBER_SHADOW &Shadow(LIBER_ELEVATION id) const;
    GlassTint Material(LIBER_MATERIAL_KIND kind, double backdropLuma) const;

private:
    Argb Evaluate(const LIBER_COLOR_DEF &def) const;

    ThemeParams m_Params;
    double m_Tint;
    Argb m_Colors[LIBER_COLOR_COUNT];
};

struct PaletteCandidate
{
    Accent AccentColor;
    double Score;
};

struct PaletteResult
{
    PaletteCandidate Candidates[LIBER_PALETTE_MAX_CANDIDATES];
    UINT Count;
    double AverageLuma;
};

BOOL ExtractPalette(const DWORD *pixels, UINT width, UINT height, UINT stride, PaletteResult *result);

enum class Easing
{
    Linear,
    Out,
    In,
    InOut,
    SpringSoft,
    Spring,
    SpringBouncy
};

double CubicBezier(const LIBER_BEZIER &curve, double t);
double SpringPosition(const LIBER_SPRING &spring, double t);
double Ease(Easing easing, double t);
BOOL IsSpring(Easing easing);

class Animation
{
public:
    Animation();

    void Set(double value);
    void Start(double from, double to, UINT durationMs, Easing easing, ULONGLONG now, UINT delayMs = 0);
    void Retarget(double to, ULONGLONG now);
    void Finish();
    double Value(ULONGLONG now) const;
    double Velocity(ULONGLONG now) const;
    double Target() const { return m_To; }
    BOOL IsRunning(ULONGLONG now) const;

private:
    double Progress(ULONGLONG now) const;

    double m_From;
    double m_To;
    double m_Velocity;
    ULONGLONG m_Start;
    UINT m_Duration;
    UINT m_Delay;
    Easing m_Easing;
    BOOL m_Running;
};

BOOL AnimationsEnabled();

class IAnimationClient
{
public:
    virtual BOOL OnAnimationFrame(ULONGLONG now) = 0;

protected:
    ~IAnimationClient() {}
};

class AnimationClock
{
public:
    static AnimationClock &Instance();

    void Add(IAnimationClient *client);
    void Remove(IAnimationClient *client);
    BOOL Contains(IAnimationClient *client) const;
    UINT Count() const { return m_Count; }
    UINT Tick(ULONGLONG now);
    static ULONGLONG Now();

private:
    AnimationClock();
    void StartTimer();
    void StopTimer();
    static void CALLBACK TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time);

    enum { MaxClients = 64 };
    IAnimationClient *m_Clients[MaxClients];
    UINT m_Count;
    UINT_PTR m_Timer;
};

struct PointF
{
    float X, Y;
};

struct RectF
{
    float X, Y, Width, Height;
};

inline RectF ToRectF(const RECT &rc)
{
    RectF r = { (float)rc.left, (float)rc.top, (float)(rc.right - rc.left), (float)(rc.bottom - rc.top) };
    return r;
}

enum TextFlags
{
    TextLeft = 0x0,
    TextCenter = 0x1,
    TextRight = 0x2,
    TextTop = 0x0,
    TextMiddle = 0x4,
    TextBottom = 0x8,
    TextEllipsis = 0x10,
    TextWrap = 0x20,
};

class IPainter
{
public:
    virtual ~IPainter() {}
    virtual void Clear(Argb color) = 0;
    virtual void FillRect(const RectF &rc, Argb color) = 0;
    virtual void FillRoundRect(const RectF &rc, float radius, Argb color) = 0;
    virtual void FillGradient(const RectF &rc, float radius, PointF from, PointF to, Argb fromColor, Argb toColor) = 0;
    virtual void FillShadow(const RectF &rc, float radius, float blur, Argb color, const RectF *hole = NULL) = 0;
    virtual void StrokeRoundRect(const RectF &rc, float radius, Argb color, float width) = 0;
    virtual void FillEllipse(const RectF &rc, Argb color) = 0;
    virtual void StrokeArc(const RectF &rc, float startDegrees, float sweepDegrees, Argb color, float width) = 0;
    virtual void Line(PointF from, PointF to, Argb color, float width) = 0;
    virtual void Text(LPCWSTR text, const RectF &rc, LIBER_TYPE style, Argb color, UINT flags) = 0;
    virtual SIZE MeasureText(LPCWSTR text, LIBER_TYPE style, float maxWidth) = 0;
    virtual void Icon(LPCWSTR name, const RectF &rc, Argb color) = 0;
    virtual void Image(HICON icon, const RectF &rc) = 0;
    virtual void PushClip(const RectF &rc, float radius) = 0;
    virtual void PopClip() = 0;
};

class FontSet
{
public:
    FontSet();
    ~FontSet();

    BOOL SetFamily(LPCWSTR family);
    LPCWSTR Family() const { return m_Family; }
    BOOL UsesFallback() const { return m_Fallback; }
    void *Format(LIBER_TYPE style);
    float Tracking(LIBER_TYPE style) const;
    void Reset();
    UINT LoadedCount() const;

private:
    WCHAR m_Family[LF_FACESIZE];
    BOOL m_Fallback;
    void *m_Formats[LIBER_TYPE_COUNT];
};

BOOL Startup();
void Shutdown();
SIZE MeasureText(LPCWSTR text, LIBER_TYPE style, float maxWidth);

class Widget;

const int LayoutInfinite = 0x3FFFFFFF;

class IWidgetHost
{
public:
    virtual void InvalidateRect(const RECT &rc) = 0;
    virtual void RequestLayout() = 0;
    virtual void SetCapture(Widget *widget) = 0;
    virtual void OnFocusChanged(Widget *widget) = 0;
    virtual void OnWidgetRemoved(Widget *widget) = 0;
    virtual void RequestFrames(UINT durationMs) { UNREFERENCED_PARAMETER(durationMs); }

protected:
    ~IWidgetHost() {}
};

enum class Align
{
    Start,
    Center,
    End,
    Stretch
};

enum class Orientation
{
    Horizontal,
    Vertical
};

enum class FocusMove
{
    Left,
    Right,
    Up,
    Down
};

struct Thickness
{
    int Left, Top, Right, Bottom;
};

struct PaintContext
{
    IPainter &Painter;
    const ThemeTokens &Tokens;
    ULONGLONG Now;
    double Backdrop;
};

enum WidgetState
{
    StateHover = 0x1,
    StatePressed = 0x2,
    StateFocused = 0x4,
    StateDisabled = 0x8,
    StateChecked = 0x10,
    StateHidden = 0x20,
};

class Widget
{
public:
    Widget();
    virtual ~Widget();

    void AddChild(Widget *child);
    void RemoveChild(Widget *child);
    Widget *Parent() const { return m_Parent; }
    Widget *FirstChild() const { return m_FirstChild; }
    Widget *Next() const { return m_Next; }
    UINT ChildCount() const;
    IWidgetHost *Host() const;
    void SetHost(IWidgetHost *host);

    const RECT &Bounds() const { return m_Bounds; }
    SIZE Desired() const { return m_Desired; }
    void SetSize(int width, int height);
    void SetMinSize(int width, int height);
    void SetMargin(const Thickness &margin) { m_Margin = margin; }
    const Thickness &Margin() const { return m_Margin; }
    void SetAlign(Align horizontal, Align vertical);
    Align HorizontalAlign() const { return m_HAlign; }
    Align VerticalAlign() const { return m_VAlign; }

    UINT State() const { return m_State; }
    BOOL HasState(UINT state) const { return (m_State & state) != 0; }
    void SetState(UINT state, BOOL on);
    BOOL IsVisible() const { return !HasState(StateHidden); }
    BOOL IsEnabled() const;
    void SetVisible(BOOL visible);
    void SetEnabled(BOOL enabled);
    void SetFocusable(BOOL focusable) { m_Focusable = focusable; }
    void SetHitTestable(BOOL hitTestable) { m_HitTestable = hitTestable; }
    BOOL IsHitTestable() const { return m_HitTestable; }
    BOOL IsAncestorOf(const Widget *widget) const;
    BOOL IsFocusable() const { return m_Focusable && IsVisible() && IsEnabled(); }
    BOOL IsVisibleInTree() const;

    SIZE Measure(SIZE available);
    void Arrange(const RECT &slot);
    void Paint(PaintContext &context);
    Widget *HitTest(POINT pt);
    void Invalidate();
    void InvalidateLayout();

    virtual void OnPointerEnter() {}
    virtual void OnPointerLeave() {}
    virtual void OnPointerDown(POINT pt) { UNREFERENCED_PARAMETER(pt); }
    virtual void OnPointerUp(POINT pt) { UNREFERENCED_PARAMETER(pt); }
    virtual void OnPointerMove(POINT pt) { UNREFERENCED_PARAMETER(pt); }
    virtual void OnClick() {}
    virtual BOOL OnContextMenu(POINT screen) { UNREFERENCED_PARAMETER(screen); return FALSE; }
    virtual BOOL OnKeyDown(UINT vk) { UNREFERENCED_PARAMETER(vk); return FALSE; }
    virtual BOOL OnChar(WCHAR ch) { UNREFERENCED_PARAMETER(ch); return FALSE; }
    virtual void OnSiblingSelected(Widget *selected, UINT group)
    {
        UNREFERENCED_PARAMETER(selected);
        UNREFERENCED_PARAMETER(group);
    }
    virtual Widget *MoveFocus(Widget *from, FocusMove move);
    void Animate(UINT durationMs);
    void SetTooltip(LPCWSTR text);
    LPCWSTR Tooltip() const { return m_Tooltip; }

protected:
    virtual SIZE MeasureOverride(SIZE available);
    virtual void ArrangeOverride(const RECT &content);
    virtual void PaintOverride(PaintContext &context) { UNREFERENCED_PARAMETER(context); }

    static SIZE MeasureChildMax(Widget *parent, SIZE available);

private:
    friend class InputRouter;

    Widget *m_Parent;
    Widget *m_FirstChild;
    Widget *m_LastChild;
    Widget *m_Next;
    Widget *m_Prev;
    IWidgetHost *m_Host;
    RECT m_Bounds;
    SIZE m_Desired;
    SIZE m_Fixed;
    SIZE m_Min;
    Thickness m_Margin;
    Align m_HAlign;
    Align m_VAlign;
    UINT m_State;
    BOOL m_Focusable;
    BOOL m_HitTestable;
    WCHAR m_Tooltip[64];
};

class Panel : public Widget
{
public:
    Panel();

    void SetPadding(const Thickness &padding) { m_Padding = padding; }
    const Thickness &Padding() const { return m_Padding; }

protected:
    SIZE MeasureOverride(SIZE available);
    void ArrangeOverride(const RECT &content);
    RECT ContentRect(const RECT &content) const;
    SIZE Shrink(SIZE available) const;
    SIZE Grow(SIZE size) const;

    Thickness m_Padding;
};

class StackPanel : public Panel
{
public:
    StackPanel(Orientation orientation, int gap);

    Orientation GetOrientation() const { return m_Orientation; }
    int Gap() const { return m_Gap; }
    Widget *MoveFocus(Widget *from, FocusMove move);

protected:
    SIZE MeasureOverride(SIZE available);
    void ArrangeOverride(const RECT &content);

private:
    Orientation m_Orientation;
    int m_Gap;
};

class GridPanel : public Panel
{
public:
    GridPanel(UINT columns, int columnGap, int rowGap);

    UINT Columns() const { return m_Columns; }
    Widget *MoveFocus(Widget *from, FocusMove move);

protected:
    SIZE MeasureOverride(SIZE available);
    void ArrangeOverride(const RECT &content);

private:
    UINT m_Columns;
    int m_ColumnGap;
    int m_RowGap;
};

enum ControlEvent
{
    ControlClicked = 1,
    ControlValueChanged,
    ControlToggled
};

class IControlListener
{
public:
    virtual void OnControlEvent(Widget *source, ControlEvent event) = 0;

protected:
    ~IControlListener() {}
};

class Control : public Widget
{
public:
    Control();

    void SetListener(IControlListener *listener) { m_Listener = listener; }
    void SetId(UINT id) { m_Id = id; }
    UINT Id() const { return m_Id; }

protected:
    void Raise(ControlEvent event);

    IControlListener *m_Listener;
    UINT m_Id;
};

class Label : public Widget
{
public:
    Label(LPCWSTR text, LIBER_TYPE style, LIBER_COLOR color, UINT flags);

    void SetText(LPCWSTR text);
    LPCWSTR Text() const { return m_Text; }
    LIBER_TYPE Style() const { return m_Style; }

protected:
    SIZE MeasureOverride(SIZE available);
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Text[128];
    LIBER_TYPE m_Style;
    LIBER_COLOR m_Color;
    UINT m_Flags;
};

class IconButton : public Control
{
public:
    IconButton(LPCWSTR icon, int size);

    void SetIcon(LPCWSTR icon);
    LPCWSTR IconName() const { return m_Icon; }
    void OnClick();

protected:
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Icon[64];
};

class ToggleTile : public Control
{
public:
    ToggleTile(LPCWSTR icon, LPCWSTR label, BOOL toggles);

    BOOL IsOn() const { return HasState(StateChecked); }
    void SetOn(BOOL on) { SetState(StateChecked, on); }
    void SetLabel(LPCWSTR label);
    LPCWSTR LabelText() const { return m_Label; }
    void OnClick();

protected:
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Icon[64];
    WCHAR m_Label[64];
    BOOL m_Toggles;
};

class Slider : public Control
{
public:
    Slider(LPCWSTR icon, int maximum);

    int Value() const { return m_Value; }
    int Maximum() const { return m_Maximum; }
    void SetValue(int value);
    int ValueAt(LONG x) const;
    void SetIcon(LPCWSTR icon);
    void OnPointerDown(POINT pt);
    void OnPointerMove(POINT pt);
    BOOL OnKeyDown(UINT vk);

protected:
    void PaintOverride(PaintContext &context);

private:
    void Change(int value);

    WCHAR m_Icon[64];
    int m_Value;
    int m_Maximum;
};

enum class ButtonKind
{
    Accent,
    Standard,
    Subtle,
    Danger
};

class Button : public Control
{
public:
    Button(LPCWSTR text, LPCWSTR icon, ButtonKind kind, int height, BOOL capsule);

    void SetText(LPCWSTR text);
    LPCWSTR Text() const { return m_Text; }
    ButtonKind Kind() const { return m_Kind; }
    BOOL IsCapsule() const { return m_Capsule; }
    void OnClick();

protected:
    SIZE MeasureOverride(SIZE available);
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Text[64];
    WCHAR m_Icon[64];
    ButtonKind m_Kind;
    BOOL m_Capsule;
    int m_Height;
};

#define LIBER_SWITCH_WIDTH  44
#define LIBER_SWITCH_HEIGHT 26

class Switch : public Control
{
public:
    explicit Switch(LPCWSTR label);

    BOOL IsOn() const { return HasState(StateChecked); }
    void SetOn(BOOL on);
    double KnobPosition(ULONGLONG now) const;
    void OnClick();
    BOOL OnKeyDown(UINT vk);

protected:
    SIZE MeasureOverride(SIZE available);
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Label[64];
    Animation m_Knob;
};

class CheckBox : public Control
{
public:
    explicit CheckBox(LPCWSTR label);

    BOOL IsChecked() const { return HasState(StateChecked); }
    BOOL IsIndeterminate() const { return m_Indeterminate; }
    void SetChecked(BOOL checked);
    void SetIndeterminate();
    void OnClick();
    BOOL OnKeyDown(UINT vk);

protected:
    SIZE MeasureOverride(SIZE available);
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Label[64];
    BOOL m_Indeterminate;
};

class RadioButton : public Control
{
public:
    RadioButton(LPCWSTR label, UINT group);

    BOOL IsChecked() const { return HasState(StateChecked); }
    UINT Group() const { return m_Group; }
    void SetChecked(BOOL checked);
    void OnClick();
    BOOL OnKeyDown(UINT vk);
    void OnSiblingSelected(Widget *selected, UINT group);

protected:
    SIZE MeasureOverride(SIZE available);
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Label[64];
    UINT m_Group;
};

enum class TextFieldKind
{
    Text,
    Password,
    Search
};

class TextField : public Control
{
public:
    TextField(LPCWSTR placeholder, TextFieldKind kind);

    LPCWSTR Text() const { return m_Text; }
    void SetText(LPCWSTR text);
    void DisplayText(LPWSTR out, UINT cch) const;
    BOOL OnChar(WCHAR ch);
    BOOL OnKeyDown(UINT vk);

protected:
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Placeholder[64];
    WCHAR m_Text[128];
    TextFieldKind m_Kind;
    UINT m_Length;
};

class SegmentedControl : public Control
{
public:
    SegmentedControl();

    UINT AddOption(LPCWSTR text);
    UINT Count() const { return m_Count; }
    UINT Selected() const { return m_Selected; }
    double SlidePosition(ULONGLONG now) const { return m_Slide.Value(now); }
    void Select(UINT index, BOOL animate);
    UINT OptionAt(LONG x) const;
    void OnPointerUp(POINT pt);
    BOOL OnKeyDown(UINT vk);

protected:
    SIZE MeasureOverride(SIZE available);
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Options[6][32];
    UINT m_Count;
    UINT m_Selected;
    Animation m_Slide;
};

class TabStrip : public Control
{
public:
    TabStrip();

    UINT AddTab(LPCWSTR text);
    UINT Count() const { return m_Count; }
    UINT Selected() const { return m_Selected; }
    void Select(UINT index);
    UINT TabAt(LONG x) const;
    void OnPointerUp(POINT pt);
    BOOL OnKeyDown(UINT vk);

protected:
    SIZE MeasureOverride(SIZE available);
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Tabs[8][32];
    UINT m_Count;
    UINT m_Selected;
};

#define LIBER_NAV_GROUP 0xFFFFFFFFu

class NavItem : public Control
{
public:
    NavItem(LPCWSTR icon, LPCWSTR selectedIcon, LPCWSTR text, LPCWSTR badge);

    BOOL IsSelected() const { return HasState(StateChecked); }
    void SetSelected(BOOL selected);
    void OnClick();
    void OnSiblingSelected(Widget *selected, UINT group);

protected:
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Icon[64];
    WCHAR m_SelectedIcon[64];
    WCHAR m_Text[64];
    WCHAR m_Badge[16];
};

enum class InfoSeverity
{
    Info,
    Success,
    Caution,
    Critical
};

class InfoBar : public Widget
{
public:
    InfoBar(InfoSeverity severity, LPCWSTR title, LPCWSTR message);

    InfoSeverity Severity() const { return m_Severity; }
    LIBER_COLOR Background() const;
    LIBER_COLOR Foreground() const;
    LPCWSTR IconName() const;

protected:
    void PaintOverride(PaintContext &context);

private:
    InfoSeverity m_Severity;
    WCHAR m_Title[64];
    WCHAR m_Message[128];
};

#define LIBER_DIALOG_WIDTH 440

class DialogPanel : public StackPanel
{
public:
    DialogPanel(LPCWSTR title, LPCWSTR message);

    Button *AddButton(LPCWSTR text, UINT id, BOOL primary, IControlListener *listener);

protected:
    void PaintOverride(PaintContext &context);

private:
    Label *m_Title;
    Label *m_Message;
    StackPanel *m_Buttons;
};

class TooltipPanel : public Widget
{
public:
    TooltipPanel();

    void SetText(LPCWSTR text);
    LPCWSTR Text() const { return m_Text; }
    void SetGlass(BOOL glass) { m_Glass = glass; }

protected:
    SIZE MeasureOverride(SIZE available);
    void PaintOverride(PaintContext &context);

private:
    WCHAR m_Text[64];
    BOOL m_Glass;
};

#define LIBER_TOOLTIP_DELAY 450

class InputRouter
{
public:
    InputRouter();

    void SetRoot(Widget *root);
    Widget *Root() const { return m_Root; }
    Widget *Hover() const { return m_Hover; }
    Widget *Pressed() const { return m_Pressed; }
    Widget *Focus() const { return m_Focus; }
    Widget *Capture() const { return m_Capture; }

    void PointerMove(POINT pt);
    void PointerLeave();
    void PointerDown(POINT pt);
    void PointerUp(POINT pt);
    BOOL KeyDown(UINT vk, BOOL shift);
    BOOL Char(WCHAR ch);
    BOOL SetFocus(Widget *widget);
    BOOL MoveFocusInOrder(BOOL forward);
    void SetCapture(Widget *widget) { m_Capture = widget; }
    void Forget(Widget *widget);

private:
    void SetHover(Widget *widget);

    Widget *m_Root;
    Widget *m_Hover;
    Widget *m_Pressed;
    Widget *m_Focus;
    Widget *m_Capture;
};

Widget *NextInTree(Widget *root, Widget *from, BOOL forward);

Thickness ShadowMargin(const LIBER_SHADOW &shadow);

class Surface : public IWidgetHost, public IAnimationClient
{
public:
    Surface();
    virtual ~Surface();

    BOOL Create(HWND owner, LPCWSTR title, DWORD exStyle);
    void Destroy();
    HWND Window() const { return m_hwnd; }
    UINT Dpi() const { return m_Dpi; }
    void SetDpi(UINT dpi);
    void SetTokens(const ThemeTokens *tokens);
    void SetRoot(Widget *root);
    Widget *Root() const { return m_Root; }
    InputRouter &Input() { return m_Input; }
    FontSet &Fonts() { return m_Fonts; }

    void SetPosition(int x, int y, int widthDip, int heightDip);
    void SetInputShape(float radiusDip);
    void SetElevation(int elevation, float radiusDip);
    void SetMaterial(int material, Argb tint, BYTE alpha, float radiusDip);
    int Material() const { return m_Material; }
    void SetMaterialFade(BOOL fade);
    void SetBackdropLuma(double luma);
    double BackdropLuma(ULONGLONG now) const;
    double BackdropTarget() const { return m_BackdropTarget; }
    void SetOpacity(float opacity);
    float Opacity() const { return m_Opacity; }
    float OffsetY() const { return m_OffsetY; }
    void TransitionIn(float riseDip, UINT durationMs);
    void TransitionOut(float dropDip, UINT durationMs);
    BOOL IsTransitioning() const { return m_Transition; }
    BOOL IsClosing() const { return m_Transition && m_HideAtEnd; }
    BOOL OnAnimationFrame(ULONGLONG now);
    Surface *ShadowSurface() const { return m_Shadow; }
    SIZE SizeDip() const { return m_SizeDip; }
    BOOL Render(ULONGLONG now);
    BOOL ReadPixels(DWORD *pixels, UINT stride) const;
    SIZE PixelSize() const { return m_SizePx; }
    BOOL HasDirty() const { return !IsRectEmpty(&m_Dirty); }
    const RECT &Dirty() const { return m_Dirty; }
    POINT PixelToDip(POINT pt) const;
    BOOL IsGpu() const { return m_Gpu != NULL; }
    UINT PresentCount() const { return m_Presents; }
    BOOL RecreateGpu();

    void InvalidateRect(const RECT &rc);
    void RequestLayout();
    void SetCapture(Widget *widget);
    void OnFocusChanged(Widget *widget);
    void OnWidgetRemoved(Widget *widget);
    void RequestFrames(UINT durationMs);
    Surface *TooltipSurface() const { return m_Tooltip; }

protected:
    virtual LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    BOOL EnsureBuffer();
    void FreeBuffer();
    void Layout();
    BOOL Present();
    BOOL CreateGpuWindow();
    BOOL RenderPass(ULONGLONG now, BOOL *lost);
    void ApplyInputShape();
    void UpdateShadow(BOOL show);
    void ApplyMaterial();
    BOOL StepBackdrop(ULONGLONG now);
    void ApplyBackdropTint(ULONGLONG now);
    void ArmTooltip();
    void ShowTooltip();
    void HideTooltip();
    LONG OffsetPx() const;

    HWND m_hwnd;
    UINT m_Dpi;
    const ThemeTokens *m_Tokens;
    Widget *m_Root;
    InputRouter m_Input;
    FontSet m_Fonts;
    POINT m_Position;
    SIZE m_SizeDip;
    SIZE m_SizePx;
    RECT m_Dirty;
    BOOL m_LayoutPending;
    BOOL m_Tracking;
    struct SurfaceGpu *m_Gpu;
    UINT m_Presents;
    RECT m_Placed;
    HWND m_Owner;
    WCHAR m_Title[64];
    DWORD m_ExStyle;
    float m_InputRadius;
    float m_Opacity;
    float m_OffsetY;
    Animation m_FadeAnimation;
    Animation m_MoveAnimation;
    BOOL m_Transition;
    BOOL m_HideAtEnd;
    Surface *m_Shadow;
    class ShadowWidget *m_ShadowWidget;
    int m_Elevation;
    float m_ShadowRadius;
    int m_Material;
    Argb m_MaterialTint;
    BYTE m_MaterialAlpha;
    float m_MaterialRadius;
    BOOL m_MaterialFade;
    ULONGLONG m_FramesUntil;
    Surface *m_Tooltip;
    TooltipPanel *m_TooltipPanel;
    Widget *m_TooltipFor;
    Animation m_BackdropAnimation;
    double m_BackdropTarget;
    BOOL m_BackdropKnown;
    BOOL m_BackdropAnimating;
    BOOL m_Recovering;
};

int ScaleToDpi(int value, UINT dpi);
LONG AssertionFailures();

}
