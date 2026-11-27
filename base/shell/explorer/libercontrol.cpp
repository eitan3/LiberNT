/*
 * PROJECT:     LiberNT Explorer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Liber control center flyout
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include "audiodev.h"
#include "brightness.h"
#include "experience.h"
#include "libershell.h"
#include "powermode.h"

using namespace LiberUI;

#define LIBER_CC_WIDTH      384
#define LIBER_CC_HALF       176
#define LIBER_CC_ROW_H      46
#define LIBER_CC_SIDE_H     71
#define LIBER_CC_MINI_W     84
#define LIBER_CC_SLIDER_H   80
#define LIBER_CC_ORB        32

enum
{
    CC_WIFI = 1,
    CC_BLUETOOTH,
    CC_AIRPLANE,
    CC_FOCUS,
    CC_POWER,
    CC_DARK,
    CC_BRIGHTNESS,
    CC_VOLUME,
    CC_BATTERY,
    CC_SETTINGS
};

static void PaintOrb(PaintContext &context, const RectF &orb, LPCWSTR icon, BOOL on)
{
    const ThemeTokens &tokens = context.Tokens;

    context.Painter.FillEllipse(orb, tokens.Color(on ? LIBER_COLOR_ACCENT : LIBER_COLOR_FILL_TILE_HOVER));
    RectF glyph = { orb.X + (orb.Width - 20.0f) / 2, orb.Y + (orb.Height - 20.0f) / 2, 20.0f, 20.0f };
    context.Painter.Icon(icon, glyph, tokens.Color(on ? LIBER_COLOR_ON_ACCENT : LIBER_COLOR_TEXT1));
}

class CCard : public StackPanel
{
public:
    CCard(Orientation orientation, int width, int height) : StackPanel(orientation, 0)
    {
        SetSize(width, height);
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        context.Painter.FillRoundRect(ToRectF(Bounds()), (float)LIBER_RADIUS_TILE,
                                      context.Tokens.Color(LIBER_COLOR_FILL_TILE));
    }
};

class COrbItem : public Control
{
public:
    COrbItem(UINT id, LPCWSTR icon, LPCWSTR title, BOOL chevron, int width, int height, float radius, float inset)
        : m_On(FALSE), m_Chevron(chevron), m_Radius(radius), m_Inset(inset)
    {
        SetId(id);
        StringCchCopyW(m_Icon, _countof(m_Icon), icon);
        StringCchCopyW(m_Title, _countof(m_Title), title);
        m_Caption[0] = 0;
        SetSize(width, height);
    }

    void Set(BOOL on, LPCWSTR caption)
    {
        m_On = on;
        StringCchCopyW(m_Caption, _countof(m_Caption), caption ? caption : L"");
        Invalidate();
    }

    BOOL IsOn() const { return m_On; }
    void OnClick() { Raise(ControlClicked); }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillRoundRect(rc, m_Radius, tokens.Color(HasState(StatePressed)
                                                                         ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                         : LIBER_COLOR_FILL_SUBTLE_HOVER));
        RectF orb = { rc.X + m_Inset, rc.Y + (rc.Height - LIBER_CC_ORB) / 2, (float)LIBER_CC_ORB, (float)LIBER_CC_ORB };
        PaintOrb(context, orb, m_Icon, m_On);
        float textX = orb.X + LIBER_CC_ORB + 10.0f;
        float textW = rc.X + rc.Width - textX - (m_Chevron ? 6.0f + 16.0f : 6.0f);
        float top = rc.Y + (rc.Height - 34.0f) / 2;
        RectF title = { textX, top, textW, 18.0f };
        RectF caption = { textX, top + 18.0f, textW, 16.0f };
        context.Painter.Text(m_Title, title, LIBER_TYPE_CONTROL_TITLE, tokens.Color(LIBER_COLOR_TEXT1),
                             TextMiddle | TextEllipsis);
        context.Painter.Text(m_Caption, caption, LIBER_TYPE_CAPTION, tokens.Color(LIBER_COLOR_TEXT2),
                             TextMiddle | TextEllipsis);
        if (m_Chevron)
        {
            RectF chevron = { rc.X + rc.Width - 6.0f - 16.0f, rc.Y + (rc.Height - 16.0f) / 2, 16.0f, 16.0f };
            context.Painter.Icon(L"chevron_right_16_regular", chevron, tokens.Color(LIBER_COLOR_TEXT3));
        }
    }

private:
    WCHAR m_Icon[64];
    WCHAR m_Title[32];
    WCHAR m_Caption[64];
    BOOL m_On;
    BOOL m_Chevron;
    float m_Radius;
    float m_Inset;
};

class CFocusCard : public COrbItem
{
public:
    CFocusCard() : COrbItem(CC_FOCUS, L"alert_snooze_20_regular", L"Focus", FALSE, LIBER_CC_HALF, LIBER_CC_SIDE_H,
                            (float)LIBER_RADIUS_TILE, 12.0f)
    {
    }

protected:
    void PaintOverride(PaintContext &context)
    {
        context.Painter.FillRoundRect(ToRectF(Bounds()), (float)LIBER_RADIUS_TILE,
                                      context.Tokens.Color(LIBER_COLOR_FILL_TILE));
        COrbItem::PaintOverride(context);
    }
};

class CMiniTile : public Control
{
public:
    CMiniTile(UINT id, LPCWSTR icon, LPCWSTR label) : m_On(FALSE)
    {
        SetId(id);
        StringCchCopyW(m_Icon, _countof(m_Icon), icon);
        SetLabel(label);
        SetSize(LIBER_CC_MINI_W, LIBER_CC_SIDE_H);
    }

    void SetLabel(LPCWSTR label)
    {
        StringCchCopyW(m_Label, _countof(m_Label), label ? label : L"");
        Invalidate();
    }

    void SetOn(BOOL on)
    {
        m_On = on;
        Invalidate();
    }

    BOOL IsOn() const { return m_On; }
    void OnClick() { Raise(ControlClicked); }

protected:
    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        context.Painter.FillRoundRect(rc, (float)LIBER_RADIUS_TILE,
                                      tokens.Color(HasState(StateHover) || HasState(StatePressed)
                                                       ? LIBER_COLOR_FILL_TILE_HOVER
                                                       : LIBER_COLOR_FILL_TILE));
        RectF orb = { rc.X + (rc.Width - LIBER_CC_ORB) / 2, rc.Y + 11.0f, (float)LIBER_CC_ORB, (float)LIBER_CC_ORB };
        PaintOrb(context, orb, m_Icon, m_On);
        RectF label = { rc.X + 4.0f, orb.Y + LIBER_CC_ORB + 4.0f, rc.Width - 8.0f, 14.0f };
        context.Painter.Text(m_Label, label, LIBER_TYPE_TILE_LABEL, tokens.Color(LIBER_COLOR_TEXT1),
                             TextCenter | TextMiddle | TextEllipsis);
    }

private:
    WCHAR m_Icon[64];
    WCHAR m_Label[32];
    BOOL m_On;
};

class CSliderCard : public StackPanel
{
public:
    CSliderCard(UINT id, LPCWSTR title, LPCWSTR icon, IControlListener *listener) : StackPanel(Orientation::Vertical, 8)
    {
        Thickness pad = { 12, 10, 12, 12 };
        Panel *head = new Panel();

        SetPadding(pad);
        SetSize(-1, LIBER_CC_SLIDER_H);
        m_Title = new Label(title, LIBER_TYPE_CONTROL_TITLE, LIBER_COLOR_TEXT1, TextLeft | TextMiddle);
        m_Title->SetAlign(Align::Start, Align::Center);
        m_Value = new Label(L"", LIBER_TYPE_CAPTION, LIBER_COLOR_TEXT2, TextRight | TextMiddle);
        m_Value->SetAlign(Align::End, Align::Center);
        head->SetSize(-1, 18);
        head->AddChild(m_Title);
        head->AddChild(m_Value);
        m_Slider = new Slider(icon, 100);
        m_Slider->SetSize(-1, 32);
        m_Slider->SetId(id);
        m_Slider->SetListener(listener);
        AddChild(head);
        AddChild(m_Slider);
    }

    Slider *Track() const { return m_Slider; }
    void SetValueText(LPCWSTR text) { m_Value->SetText(text); }

protected:
    void PaintOverride(PaintContext &context)
    {
        context.Painter.FillRoundRect(ToRectF(Bounds()), (float)LIBER_RADIUS_TILE,
                                      context.Tokens.Color(LIBER_COLOR_FILL_TILE));
    }

private:
    Label *m_Title;
    Label *m_Value;
    Slider *m_Slider;
};

class CBatteryButton : public LiberUI::Control
{
public:
    CBatteryButton()
    {
        m_Text[0] = 0;
        SetId(CC_BATTERY);
        SetSize(-1, LIBER_CONTROL_MD);
    }

    void SetText(LPCWSTR text)
    {
        StringCchCopyW(m_Text, _countof(m_Text), text ? text : L"");
        SetVisible(m_Text[0] != 0);
        InvalidateLayout();
        Invalidate();
    }

    void OnClick() { Raise(ControlClicked); }

protected:
    SIZE MeasureOverride(SIZE available)
    {
        SIZE size = { min(available.cx, (LONG)(12 + 20 + 8 + lstrlenW(m_Text) * 8 + 12)), LIBER_CONTROL_MD };
        return size;
    }

    void PaintOverride(PaintContext &context)
    {
        RectF rc = ToRectF(Bounds());
        const ThemeTokens &tokens = context.Tokens;

        if (HasState(StateHover) || HasState(StatePressed))
            context.Painter.FillRoundRect(rc, rc.Height / 2, tokens.Color(HasState(StatePressed)
                                                                              ? LIBER_COLOR_FILL_SUBTLE_PRESSED
                                                                              : LIBER_COLOR_FILL_SUBTLE_HOVER));
        RectF icon = { rc.X + 13.0f, rc.Y + (rc.Height - 20.0f) / 2, 20.0f, 20.0f };
        context.Painter.Icon(L"battery_10_20_regular", icon, tokens.Color(LIBER_COLOR_TEXT1));
        RectF text = { icon.X + 28.0f, rc.Y, rc.Width - 41.0f, rc.Height };
        context.Painter.Text(m_Text, text, LIBER_TYPE_BODY, tokens.Color(LIBER_COLOR_TEXT1), TextMiddle);
    }

private:
    WCHAR m_Text[16];
};

class CFlyoutSurface : public Surface
{
public:
    CFlyoutSurface() : m_HiddenAt(0) {}
    ULONGLONG m_HiddenAt;
    void Hide()
    {
        if (Window() && IsWindowVisible(Window()) && !IsClosing())
        {
            TransitionOut(24.0f, 150);
            m_HiddenAt = GetTickCount64();
        }
    }

protected:
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
};

class CLiberControlCenter final : public IControlListener
{
public:
    CLiberControlCenter();
    ~CLiberControlCenter();

    BOOL Open(const RECT *prcAnchor);
    void Close();
    BOOL IsOpen() const { return m_Surface.Window() && IsWindowVisible(m_Surface.Window()) && !m_Surface.IsClosing(); }
    BOOL JustClosed() const { return m_Surface.m_HiddenAt != 0 && GetTickCount64() - m_Surface.m_HiddenAt < 250; }
    void OnControlEvent(Widget *source, ControlEvent event);

private:
    void Build();
    void Sync();
    BOOL OpenVolume();
    void UpdateValues();

    CFlyoutSurface m_Surface;
    ThemeTokens m_Tokens;
    CLiberPanel *m_Root;
    COrbItem *m_Wifi;
    COrbItem *m_Bluetooth;
    COrbItem *m_Airplane;
    CFocusCard *m_Focus;
    CMiniTile *m_Power;
    CMiniTile *m_Dark;
    CSliderCard *m_Display;
    CSliderCard *m_Sound;
    CBatteryButton *m_Battery;
    CComPtr<IAudioEndpointVolume> m_pVolume;
};

static CLiberControlCenter *g_pControlCenter;

LRESULT CFlyoutSurface::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if ((msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) || (msg == WM_KEYDOWN && wParam == VK_ESCAPE))
    {
        Hide();
        return 0;
    }
    return Surface::HandleMessage(msg, wParam, lParam);
}

CLiberControlCenter::CLiberControlCenter()
    : m_Root(NULL), m_Wifi(NULL), m_Bluetooth(NULL), m_Airplane(NULL), m_Focus(NULL), m_Power(NULL), m_Dark(NULL),
      m_Display(NULL), m_Sound(NULL), m_Battery(NULL)
{
}

CLiberControlCenter::~CLiberControlCenter()
{
    m_Surface.SetRoot(NULL);
    m_Surface.Destroy();
    delete m_Root;
}

BOOL CLiberControlCenter::OpenVolume()
{
    CComPtr<IMMDeviceEnumerator> pEnum;
    CComPtr<IMMDevice> pDevice;
    WCHAR szName[128];

    m_pVolume.Release();
    if (FAILED(CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, IID_IMMDeviceEnumerator,
                                (void **)&pEnum)) ||
        FAILED(pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, &pDevice)) ||
        FAILED(pDevice->Activate(IID_IAudioEndpointVolume, CLSCTX_INPROC_SERVER, NULL, (void **)&m_pVolume)))
    {
        m_pVolume.Release();
        return FALSE;
    }
    TfyGetDeviceFriendlyName(pDevice, szName, _countof(szName));
    m_Sound->SetValueText(szName);
    return TRUE;
}

void CLiberControlCenter::Build()
{
    Thickness pad = { LIBER_FLYOUT_PAD, LIBER_FLYOUT_PAD, LIBER_FLYOUT_PAD, LIBER_FLYOUT_PAD };
    StackPanel *top = new StackPanel(Orientation::Horizontal, 8);
    CCard *conn = new CCard(Orientation::Vertical, LIBER_CC_HALF, 3 * LIBER_CC_ROW_H + 12);
    StackPanel *side = new StackPanel(Orientation::Vertical, 8);
    StackPanel *minis = new StackPanel(Orientation::Horizontal, 8);
    Panel *footer = new Panel();
    IconButton *settings = new IconButton(L"settings_20_regular", LIBER_CONTROL_MD);
    Thickness connPad = { 6, 6, 6, 6 };

    m_Root = new CLiberPanel(Orientation::Vertical, 8, (float)LIBER_RADIUS_FLYOUT, LIBER_MATERIAL_THICK);
    m_Root->SetPadding(pad);

    conn->SetPadding(connPad);
    m_Wifi = new COrbItem(CC_WIFI, L"wifi_1_20_regular", L"Wi-Fi", TRUE, -1, LIBER_CC_ROW_H,
                          (float)LIBER_RADIUS_TILE_ITEM, 6.0f);
    m_Bluetooth = new COrbItem(CC_BLUETOOTH, L"bluetooth_20_regular", L"Bluetooth", FALSE, -1, LIBER_CC_ROW_H,
                               (float)LIBER_RADIUS_TILE_ITEM, 6.0f);
    m_Airplane = new COrbItem(CC_AIRPLANE, L"airplane_20_regular", L"Airplane mode", FALSE, -1, LIBER_CC_ROW_H,
                              (float)LIBER_RADIUS_TILE_ITEM, 6.0f);
    conn->AddChild(m_Wifi);
    conn->AddChild(m_Bluetooth);
    conn->AddChild(m_Airplane);

    m_Focus = new CFocusCard();
    m_Power = new CMiniTile(CC_POWER, L"rocket_20_regular", L"Power mode");
    m_Dark = new CMiniTile(CC_DARK, L"dark_theme_20_regular", L"Dark mode");
    minis->AddChild(m_Power);
    minis->AddChild(m_Dark);
    side->AddChild(m_Focus);
    side->AddChild(minis);
    top->AddChild(conn);
    top->AddChild(side);

    m_Display = new CSliderCard(CC_BRIGHTNESS, L"Display", L"brightness_high_20_regular", this);
    m_Sound = new CSliderCard(CC_VOLUME, L"Sound", L"speaker_2_20_regular", this);

    m_Battery = new CBatteryButton();
    m_Battery->SetAlign(Align::Start, Align::Center);
    settings->SetId(CC_SETTINGS);
    settings->SetAlign(Align::End, Align::Center);
    footer->SetSize(-1, LIBER_CONTROL_MD);
    footer->AddChild(m_Battery);
    footer->AddChild(settings);

    Widget *controls[] = { m_Wifi, m_Bluetooth, m_Airplane, m_Focus, m_Power, m_Dark, m_Battery, settings };
    for (UINT i = 0; i < _countof(controls); i++)
        static_cast<Control *>(controls[i])->SetListener(this);

    m_Root->AddChild(top);
    m_Root->AddChild(m_Display);
    m_Root->AddChild(m_Sound);
    m_Root->AddChild(footer);
}

void CLiberControlCenter::UpdateValues()
{
    WCHAR szText[16];

    StringCchPrintfW(szText, _countof(szText), L"%d%%", m_Display->Track()->Value());
    m_Display->SetValueText(szText);
}

void CLiberControlCenter::Sync()
{
    int percent = 0;
    SYSTEM_POWER_STATUS sps;
    WCHAR szText[64];
    float level = 0.0f;
    DWORD airplane = 0, cb = sizeof(airplane);

    m_Wifi->Set(TRUE, L"Network and internet");
    m_Bluetooth->Set(FALSE, L"Devices");
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ReactOS\\Explorer", L"AirplaneMode", RRF_RT_REG_DWORD, NULL,
                     &airplane, &cb) != ERROR_SUCCESS)
    {
        airplane = 0;
    }
    m_Airplane->Set(airplane != 0, airplane ? L"On" : L"Off");
    m_Focus->Set(TrayNotifications_IsHidden(), TrayNotifications_IsHidden() ? L"On" : L"Off");
    m_Dark->SetOn(LiberExperience_IsDark());
    int mode = TfyActivePowerMode();
    m_Power->SetLabel(mode >= 0 ? g_TfyPowerModes[mode].pszName : L"Power mode");
    m_Power->SetOn(mode == TFY_POWER_MODES - 1);

    m_Display->SetVisible(TfyBrightnessQuery(&percent));
    m_Display->Track()->SetValue(percent);

    BOOL volume = OpenVolume();
    m_Sound->SetVisible(volume);
    if (volume && SUCCEEDED(m_pVolume->GetMasterVolumeLevelScalar(&level)))
        m_Sound->Track()->SetValue((int)(level * 100.0f + 0.5f));

    szText[0] = 0;
    if (GetSystemPowerStatus(&sps) && sps.BatteryFlag != 128 && sps.BatteryLifePercent <= 100)
        StringCchPrintfW(szText, _countof(szText), L"%u%%", sps.BatteryLifePercent);
    m_Battery->SetText(szText);
    UpdateValues();
}

BOOL CLiberControlCenter::Open(const RECT *prcAnchor)
{
    if (!m_Root)
    {
        if (!m_Surface.Create(NULL, L"Liber Control Center", WS_EX_TOPMOST | WS_EX_TOOLWINDOW))
            return FALSE;
        m_Surface.SetInputShape((float)LIBER_RADIUS_FLYOUT);
        m_Surface.SetElevation(LIBER_ELEVATION_E32, (float)LIBER_RADIUS_FLYOUT);
        Build();
        m_Surface.SetRoot(m_Root);
    }

    LiberShell_BuildTokens(&m_Tokens);
    LiberShell_ApplyFont(&m_Surface);
    m_Surface.SetTokens(&m_Tokens);
    Sync();

    SIZE available = { LIBER_CC_WIDTH, LayoutInfinite };
    SIZE size = m_Root->Measure(available);
    POINT pt;
    LiberShell_PlaceFlyout(prcAnchor, ScaleToDpi(LIBER_CC_WIDTH, m_Surface.Dpi()), ScaleToDpi(size.cy, m_Surface.Dpi()),
                           FALSE, &pt);
    m_Surface.SetPosition(pt.x, pt.y, LIBER_CC_WIDTH, size.cy);
    m_Surface.RequestLayout();
    m_Surface.Render(AnimationClock::Now());
    ShowWindow(m_Surface.Window(), SW_SHOW);
    m_Surface.TransitionIn(32.0f, 300);
    SetForegroundWindow(m_Surface.Window());
    return TRUE;
}

void CLiberControlCenter::Close()
{
    m_Surface.Hide();
}

void CLiberControlCenter::OnControlEvent(Widget *source, ControlEvent event)
{
    Control *control = static_cast<Control *>(source);
    RECT rc;

    UNREFERENCED_PARAMETER(event);
    GetWindowRect(m_Surface.Window(), &rc);
    switch (control->Id())
    {
        case CC_WIFI:
        case CC_AIRPLANE:
            Close();
            TrayNetwork_Toggle(NULL, &rc);
            break;
        case CC_BLUETOOTH:
            Close();
            TrayBluetooth_Toggle(NULL, &rc);
            break;
        case CC_FOCUS:
            TrayNotifications_SetHidden(!TrayNotifications_IsHidden());
            m_Focus->Set(TrayNotifications_IsHidden(), TrayNotifications_IsHidden() ? L"On" : L"Off");
            break;
        case CC_DARK:
            if (LiberShell_SetDarkMode(!m_Dark->IsOn()))
                m_Dark->SetOn(!m_Dark->IsOn());
            LiberShell_BuildTokens(&m_Tokens);
            m_Surface.SetTokens(&m_Tokens);
            break;
        case CC_POWER:
        {
            int mode = TfyActivePowerMode();
            int next = (mode + 1) % TFY_POWER_MODES;
            if (TfyLoadPowrProf() && g_pfnPowerSetActiveScheme(NULL, &g_TfyPowerModes[next].Scheme) == ERROR_SUCCESS)
            {
                m_Power->SetLabel(g_TfyPowerModes[next].pszName);
                m_Power->SetOn(next == TFY_POWER_MODES - 1);
            }
            break;
        }
        case CC_BRIGHTNESS:
            TfyBrightnessSet(m_Display->Track()->Value());
            UpdateValues();
            break;
        case CC_VOLUME:
            if (m_pVolume)
                m_pVolume->SetMasterVolumeLevelScalar(m_Sound->Track()->Value() / 100.0f, NULL);
            break;
        case CC_BATTERY:
            Close();
            TrayPower_Toggle(NULL, &rc);
            break;
        case CC_SETTINGS:
            Close();
            ShellExecuteW(NULL, NULL, L"ms-settings:", NULL, NULL, SW_SHOWNORMAL);
            break;
    }
}

VOID LiberControlCenter_Toggle(IN const RECT *prcAnchor)
{
    if (!g_pControlCenter)
        g_pControlCenter = new CLiberControlCenter();
    if (!g_pControlCenter)
        return;
    if (g_pControlCenter->IsOpen())
        g_pControlCenter->Close();
    else if (!g_pControlCenter->JustClosed())
        g_pControlCenter->Open(prcAnchor);
}

BOOL LiberControlCenter_IsOpen(VOID)
{
    return g_pControlCenter && g_pControlCenter->IsOpen();
}

VOID LiberControlCenter_Close(VOID)
{
    delete g_pControlCenter;
    g_pControlCenter = NULL;
}
