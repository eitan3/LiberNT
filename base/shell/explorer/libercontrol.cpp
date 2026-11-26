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

#define LIBER_CC_WIDTH 372

enum
{
    CC_WIFI = 1,
    CC_BLUETOOTH,
    CC_DARK,
    CC_POWER,
    CC_BRIGHTNESS,
    CC_VOLUME,
    CC_SETTINGS
};

class CFlyoutSurface : public Surface
{
public:
    CFlyoutSurface() : m_HiddenAt(0) {}
    ULONGLONG m_HiddenAt;
    void Hide()
    {
        if (Window() && IsWindowVisible(Window()))
        {
            ShowWindow(Window(), SW_HIDE);
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
    BOOL IsOpen() const { return m_Surface.Window() != NULL && IsWindowVisible(m_Surface.Window()); }
    BOOL JustClosed() const { return m_Surface.m_HiddenAt != 0 && GetTickCount64() - m_Surface.m_HiddenAt < 250; }
    void OnControlEvent(Widget *source, ControlEvent event);

private:
    void Build();
    void Sync();
    BOOL OpenVolume();

    CFlyoutSurface m_Surface;
    ThemeTokens m_Tokens;
    CLiberPanel *m_Root;
    ToggleTile *m_Dark;
    ToggleTile *m_Power;
    Slider *m_Brightness;
    Slider *m_Volume;
    Label *m_Device;
    Label *m_Battery;
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
    : m_Root(NULL), m_Dark(NULL), m_Power(NULL), m_Brightness(NULL), m_Volume(NULL), m_Device(NULL), m_Battery(NULL)
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
    m_Device->SetText(szName);
    return TRUE;
}

void CLiberControlCenter::Build()
{
    Thickness pad = { LIBER_FLYOUT_PAD, LIBER_FLYOUT_PAD, LIBER_FLYOUT_PAD, LIBER_FLYOUT_PAD };
    GridPanel *tiles = new GridPanel(2, 8, 8);
    StackPanel *footer = new StackPanel(Orientation::Horizontal, 8);
    ToggleTile *wifi = new ToggleTile(L"wifi_1_20_regular", L"Wi-Fi", FALSE);
    ToggleTile *bluetooth = new ToggleTile(L"bluetooth_20_regular", L"Bluetooth", FALSE);
    IconButton *settings = new IconButton(L"settings_20_regular", LIBER_CONTROL_MD);

    m_Root = new CLiberPanel(Orientation::Vertical, 12, (float)LIBER_RADIUS_FLYOUT, LIBER_MATERIAL_THICK);
    m_Root->SetPadding(pad);
    m_Dark = new ToggleTile(L"dark_theme_20_regular", L"Dark mode", TRUE);
    m_Power = new ToggleTile(L"rocket_20_regular", L"Power mode", FALSE);
    m_Brightness = new Slider(L"brightness_high_20_regular", 100);
    m_Volume = new Slider(L"speaker_2_20_regular", 100);
    m_Device = new Label(L"", LIBER_TYPE_CAPTION, LIBER_COLOR_TEXT2, TextLeft | TextMiddle);
    m_Battery = new Label(L"", LIBER_TYPE_CAPTION, LIBER_COLOR_TEXT2, TextLeft | TextMiddle);

    ToggleTile *list[] = { wifi, bluetooth, m_Dark, m_Power };
    UINT ids[] = { CC_WIFI, CC_BLUETOOTH, CC_DARK, CC_POWER };
    for (UINT i = 0; i < _countof(list); i++)
    {
        list[i]->SetSize(-1, 56);
        list[i]->SetId(ids[i]);
        list[i]->SetListener(this);
        tiles->AddChild(list[i]);
    }
    m_Brightness->SetId(CC_BRIGHTNESS);
    m_Brightness->SetListener(this);
    m_Volume->SetId(CC_VOLUME);
    m_Volume->SetListener(this);
    settings->SetId(CC_SETTINGS);
    settings->SetListener(this);
    m_Battery->SetSize(LIBER_CC_WIDTH - 2 * LIBER_FLYOUT_PAD - LIBER_CONTROL_MD - 8, LIBER_CONTROL_MD);
    m_Device->SetSize(-1, 16);

    footer->AddChild(m_Battery);
    footer->AddChild(settings);
    m_Root->AddChild(tiles);
    m_Root->AddChild(m_Brightness);
    m_Root->AddChild(m_Volume);
    m_Root->AddChild(m_Device);
    m_Root->AddChild(footer);
}

void CLiberControlCenter::Sync()
{
    int percent = 0;
    SYSTEM_POWER_STATUS sps;
    WCHAR szText[64];
    float level = 0.0f;

    m_Dark->SetOn(LiberExperience_IsDark());
    int mode = TfyActivePowerMode();
    m_Power->SetLabel(mode >= 0 ? g_TfyPowerModes[mode].pszName : L"Power mode");

    m_Brightness->SetVisible(TfyBrightnessQuery(&percent));
    m_Brightness->SetValue(percent);

    BOOL volume = OpenVolume();
    m_Volume->SetVisible(volume);
    m_Device->SetVisible(volume);
    if (volume && SUCCEEDED(m_pVolume->GetMasterVolumeLevelScalar(&level)))
        m_Volume->SetValue((int)(level * 100.0f + 0.5f));

    szText[0] = 0;
    if (GetSystemPowerStatus(&sps) && sps.BatteryFlag != 128 && sps.BatteryLifePercent <= 100)
        StringCchPrintfW(szText, _countof(szText), L"%u%%", sps.BatteryLifePercent);
    m_Battery->SetText(szText);
}

BOOL CLiberControlCenter::Open(const RECT *prcAnchor)
{
    if (!m_Root)
    {
        if (!m_Surface.Create(NULL, L"Liber Control Center", WS_EX_TOPMOST | WS_EX_TOOLWINDOW))
            return FALSE;
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
    LiberShell_PlaceAbove(prcAnchor, ScaleToDpi(LIBER_CC_WIDTH, m_Surface.Dpi()), ScaleToDpi(size.cy, m_Surface.Dpi()),
                          &pt);
    m_Surface.SetPosition(pt.x, pt.y, LIBER_CC_WIDTH, size.cy);
    m_Surface.RequestLayout();
    m_Surface.Render(AnimationClock::Now());
    ShowWindow(m_Surface.Window(), SW_SHOW);
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
            Close();
            TrayNetwork_Toggle(NULL, &rc);
            break;
        case CC_BLUETOOTH:
            Close();
            TrayBluetooth_Toggle(NULL, &rc);
            break;
        case CC_DARK:
            LiberShell_SetDarkMode(m_Dark->IsOn());
            LiberShell_BuildTokens(&m_Tokens);
            m_Surface.SetTokens(&m_Tokens);
            break;
        case CC_POWER:
        {
            int mode = TfyActivePowerMode();
            int next = (mode + 1) % TFY_POWER_MODES;
            if (TfyLoadPowrProf() && g_pfnPowerSetActiveScheme(NULL, &g_TfyPowerModes[next].Scheme) == ERROR_SUCCESS)
                m_Power->SetLabel(g_TfyPowerModes[next].pszName);
            break;
        }
        case CC_BRIGHTNESS:
            TfyBrightnessSet(m_Brightness->Value());
            break;
        case CC_VOLUME:
            if (m_pVolume)
                m_pVolume->SetMasterVolumeLevelScalar(m_Volume->Value() / 100.0f, NULL);
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
