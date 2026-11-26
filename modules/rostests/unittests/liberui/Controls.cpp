/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the label, icon button, toggle tile and slider controls
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <liberui.h>

using namespace LiberUI;

class Recorder : public IControlListener
{
public:
    Recorder() : Source(NULL), Last((ControlEvent)0), Count(0) {}
    void OnControlEvent(Widget *source, ControlEvent event)
    {
        Source = source;
        Last = event;
        Count++;
    }
    Widget *Source;
    ControlEvent Last;
    UINT Count;
};

static void Place(Widget &widget, LONG left, LONG top, LONG right, LONG bottom)
{
    SIZE available = { right - left, bottom - top };
    RECT slot = { left, top, right, bottom };
    widget.Measure(available);
    widget.Arrange(slot);
}

static POINT Pt(LONG x, LONG y)
{
    POINT pt = { x, y };
    return pt;
}

static void TestSlider()
{
    StackPanel root(Orientation::Vertical, 0);
    Slider *slider = new Slider(L"speaker_2_20_regular", 100);
    Recorder recorder;
    InputRouter input;

    slider->SetListener(&recorder);
    root.AddChild(slider);
    Place(root, 0, 0, 340, 40);
    input.SetRoot(&root);
    ok(slider->Bounds().right == 340 && slider->Bounds().bottom == LIBER_CONTROL_LG, "fat slider fills the row\n");

    ok(slider->ValueAt(0) == 0 && slider->ValueAt(20) == 0, "left inset is 0\n");
    ok(slider->ValueAt(320) == 100 && slider->ValueAt(500) == 100, "right inset is the maximum\n");
    ok(slider->ValueAt(170) == 50, "middle is 50: %d\n", slider->ValueAt(170));

    input.PointerDown(Pt(170, 20));
    ok(slider->Value() == 50 && recorder.Last == ControlValueChanged && recorder.Count == 1, "press sets the value\n");
    input.PointerMove(Pt(320, 20));
    ok(slider->Value() == 100 && recorder.Count == 2, "drag moves the value\n");
    input.PointerMove(Pt(600, 200));
    ok(slider->Value() == 100 && recorder.Count == 2, "dragging past the end does not report again\n");
    input.PointerUp(Pt(600, 200));
    input.PointerMove(Pt(20, 20));
    ok(slider->Value() == 100, "moving without a press changes nothing\n");

    ok(input.Focus() == slider, "pressing focuses the slider\n");
    ok(input.KeyDown(VK_LEFT, FALSE) && slider->Value() == 99, "Left steps down: %d\n", slider->Value());
    input.KeyDown(VK_NEXT, FALSE);
    ok(slider->Value() == 89, "Page Down steps by 10: %d\n", slider->Value());
    input.KeyDown(VK_HOME, FALSE);
    ok(slider->Value() == 0, "Home\n");
    UINT count = recorder.Count;
    input.KeyDown(VK_LEFT, FALSE);
    ok(slider->Value() == 0 && recorder.Count == count, "no report at the minimum\n");
    input.KeyDown(VK_END, FALSE);
    ok(slider->Value() == 100, "End\n");

    count = recorder.Count;
    slider->SetValue(250);
    ok(slider->Value() == 100 && recorder.Count == count, "SetValue clamps and does not report\n");
    slider->SetValue(-5);
    ok(slider->Value() == 0, "SetValue clamps below\n");
}

static void TestTiles()
{
    StackPanel root(Orientation::Horizontal, 8);
    ToggleTile *dark = new ToggleTile(L"dark_theme_20_regular", L"Dark mode", TRUE);
    ToggleTile *wifi = new ToggleTile(L"wifi_1_20_regular", L"Wi-Fi", FALSE);
    IconButton *gear = new IconButton(L"settings_20_regular", LIBER_CONTROL_MD);
    Recorder recorder;
    InputRouter input;

    dark->SetSize(150, 72);
    wifi->SetSize(150, 72);
    dark->SetListener(&recorder);
    wifi->SetListener(&recorder);
    gear->SetListener(&recorder);
    root.AddChild(dark);
    root.AddChild(wifi);
    root.AddChild(gear);
    root.SetAlign(Align::Start, Align::Start);
    Place(root, 0, 0, 400, 72);
    input.SetRoot(&root);

    input.PointerDown(Pt(10, 10));
    input.PointerUp(Pt(10, 10));
    ok(dark->IsOn() && recorder.Source == dark && recorder.Last == ControlToggled, "a toggle tile toggles\n");
    input.KeyDown(VK_SPACE, FALSE);
    ok(!dark->IsOn() && recorder.Count == 2, "Space toggles it back\n");

    input.PointerDown(Pt(170, 10));
    input.PointerUp(Pt(170, 10));
    ok(!wifi->IsOn() && recorder.Source == wifi && recorder.Last == ControlClicked, "a button tile only clicks\n");

    ok(gear->Bounds().left == 316 && gear->Bounds().right == 348, "icon button is 32 wide at %ld\n", gear->Bounds().left);
    input.PointerDown(Pt(330, 10));
    input.PointerUp(Pt(330, 10));
    ok(recorder.Source == gear && recorder.Last == ControlClicked, "icon button clicks\n");

    gear->SetEnabled(FALSE);
    UINT count = recorder.Count;
    input.PointerDown(Pt(330, 10));
    input.PointerUp(Pt(330, 10));
    ok(recorder.Count == count, "a disabled button does not click\n");

    wifi->SetLabel(L"LiberNT-5G");
    ok(!wcscmp(wifi->LabelText(), L"LiberNT-5G"), "label updated\n");
}

static void TestLabel()
{
    Label label(L"Brightness", LIBER_TYPE_BODY, LIBER_COLOR_TEXT2, TextLeft);
    SIZE available = { 300, LayoutInfinite };
    SIZE size = label.Measure(available);

    ok(size.cy == LiberTypeRamp[LIBER_TYPE_BODY].LineHeight && size.cx > 0 && size.cx < 300, "label %ld x %ld\n",
       size.cx, size.cy);
    label.SetText(NULL);
    ok(label.Text()[0] == 0, "NULL text clears\n");
    ok(!label.IsHitTestable(), "labels pass hits through\n");
}

START_TEST(Controls)
{
    LONG failures = AssertionFailures();

    TestSlider();
    TestTiles();
    TestLabel();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
}
