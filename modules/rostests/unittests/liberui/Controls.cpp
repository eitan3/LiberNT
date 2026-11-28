/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test the label, icon button, toggle tile and slider controls
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <math.h>
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

static void TestButtons()
{
    StackPanel root(Orientation::Vertical, 0);
    Button *accent = new Button(L"Download", L"arrow_download_20_regular", ButtonKind::Accent, LIBER_CONTROL_MD, FALSE);
    Button *icon = new Button(NULL, L"grid_20_regular", ButtonKind::Subtle, LIBER_CONTROL_SM, FALSE);
    Button *large = new Button(L"Get", NULL, ButtonKind::Standard, LIBER_CONTROL_LG, TRUE);
    Recorder recorder;
    InputRouter input;

    accent->SetListener(&recorder);
    accent->SetId(7);
    accent->SetAlign(Align::Start, Align::Start);
    icon->SetAlign(Align::Start, Align::Start);
    large->SetAlign(Align::Start, Align::Start);
    root.AddChild(accent);
    root.AddChild(icon);
    root.AddChild(large);
    Place(root, 0, 0, 400, 200);
    input.SetRoot(&root);
    ok(accent->Bounds().bottom - accent->Bounds().top == LIBER_CONTROL_MD, "a standard button is 32 high\n");
    ok(accent->Bounds().right - accent->Bounds().left > 24 + 28, "the button fits its icon and text\n");
    ok(icon->Bounds().right - icon->Bounds().left == LIBER_CONTROL_SM &&
           icon->Bounds().bottom - icon->Bounds().top == LIBER_CONTROL_SM,
       "an icon-only button is a 24 square\n");
    ok(large->Bounds().bottom - large->Bounds().top == LIBER_CONTROL_LG && large->IsCapsule(),
       "a primary action is a 40 high capsule\n");
    ok(accent->Kind() == ButtonKind::Accent && !wcscmp(accent->Text(), L"Download"), "kind and text are kept\n");
    input.PointerDown(Pt(accent->Bounds().left + 5, accent->Bounds().top + 5));
    input.PointerUp(Pt(accent->Bounds().left + 5, accent->Bounds().top + 5));
    ok(recorder.Count == 1 && recorder.Last == ControlClicked && recorder.Source == accent, "a click is raised\n");
    accent->SetEnabled(FALSE);
    input.PointerDown(Pt(accent->Bounds().left + 5, accent->Bounds().top + 5));
    input.PointerUp(Pt(accent->Bounds().left + 5, accent->Bounds().top + 5));
    ok(recorder.Count == 1, "a disabled button does not click\n");
}

static void TestSelection()
{
    StackPanel root(Orientation::Vertical, 0);
    Switch *toggle = new Switch(L"Wi-Fi");
    CheckBox *check = new CheckBox(L"Checked");
    RadioButton *a = new RadioButton(L"Efficiency", 1);
    RadioButton *b = new RadioButton(L"Balanced", 1);
    RadioButton *other = new RadioButton(L"Other group", 2);
    Recorder recorder;

    toggle->SetListener(&recorder);
    root.AddChild(toggle);
    root.AddChild(check);
    root.AddChild(a);
    root.AddChild(b);
    root.AddChild(other);
    Place(root, 0, 0, 300, 400);
    ok(!toggle->IsOn() && toggle->KnobPosition(AnimationClock::Now()) == 0.0, "a switch starts off\n");
    toggle->OnClick();
    ok(toggle->IsOn() && recorder.Last == ControlToggled, "a click turns the switch on\n");
    ULONGLONG now = AnimationClock::Now();
    ok(toggle->KnobPosition(now) < 1.0, "the knob slides instead of jumping\n");
    ok(fabs(toggle->KnobPosition(now + LIBER_DUR_SPRING) - 1.0) < 1e-6, "the knob arrives at the end of the spring\n");
    ok(toggle->OnKeyDown(VK_SPACE) && !toggle->IsOn(), "space toggles the switch\n");

    ok(!check->IsChecked(), "a check box starts clear\n");
    check->OnClick();
    ok(check->IsChecked(), "a click checks it\n");
    check->SetIndeterminate();
    ok(check->IsIndeterminate() && !check->IsChecked(), "a check box can be indeterminate\n");
    check->OnClick();
    ok(check->IsChecked() && !check->IsIndeterminate(), "clicking an indeterminate box checks it\n");

    other->SetChecked(TRUE);
    a->OnClick();
    ok(a->IsChecked() && !b->IsChecked(), "a radio button checks\n");
    b->OnClick();
    ok(b->IsChecked() && !a->IsChecked(), "checking another radio in the group clears the first\n");
    ok(other->IsChecked(), "radios in another group are left alone\n");
}

static void TestTextField()
{
    StackPanel root(Orientation::Vertical, 0);
    TextField *field = new TextField(L"Name", TextFieldKind::Text);
    TextField *secret = new TextField(L"Password", TextFieldKind::Password);
    TextField *search = new TextField(L"Search", TextFieldKind::Search);
    Recorder recorder;
    InputRouter input;
    WCHAR shown[16];

    field->SetListener(&recorder);
    root.AddChild(field);
    root.AddChild(secret);
    root.AddChild(search);
    Place(root, 0, 0, 300, 200);
    input.SetRoot(&root);
    ok(!input.Char(L'x'), "typing without focus goes nowhere\n");
    input.SetFocus(field);
    ok(input.Char(L'L') && input.Char(L'i') && !wcscmp(field->Text(), L"Li"), "typed characters reach the field\n");
    ok(recorder.Last == ControlValueChanged && recorder.Count == 2, "each edit raises a change\n");
    ok(input.Char(L'\b') && !wcscmp(field->Text(), L"L"), "backspace removes the last character\n");
    ok(!input.Char(L'\t') && !wcscmp(field->Text(), L"L"), "control characters are not text\n");
    field->SetText(L"");
    ok(!input.Char(L'\b'), "backspace in an empty field does nothing\n");
    secret->SetText(L"abc");
    secret->DisplayText(shown, _countof(shown));
    ok(!wcscmp(secret->Text(), L"abc") && shown[0] == 0x25CF && shown[2] == 0x25CF && shown[3] == 0,
       "a password shows dots\n");
    search->SetText(L"term");
    ok(search->OnKeyDown(VK_ESCAPE) && search->Text()[0] == 0, "escape clears a search box\n");
    ok(!field->OnKeyDown(VK_ESCAPE), "escape is left to a text field's owner\n");
    for (UINT i = 0; i < 200; i++)
        field->OnChar(L'a');
    ok(lstrlenW(field->Text()) == 127, "a field stops at its capacity: %d\n", lstrlenW(field->Text()));
}

static void TestSegmentedAndTabs()
{
    StackPanel root(Orientation::Vertical, 0);
    SegmentedControl *segmented = new SegmentedControl();
    TabStrip *tabs = new TabStrip();
    Recorder recorder;

    segmented->SetListener(&recorder);
    segmented->AddOption(L"Efficiency");
    segmented->AddOption(L"Balanced");
    segmented->AddOption(L"Performance");
    tabs->AddTab(L"All");
    tabs->AddTab(L"Apps");
    tabs->AddTab(L"Documents");
    root.AddChild(segmented);
    root.AddChild(tabs);
    Place(root, 0, 0, 304, 200);
    ok(segmented->Count() == 3 && segmented->Selected() == 0, "three options, the first selected\n");
    ok(segmented->Bounds().right == 304, "given a width, the options share it\n");
    ok(segmented->OptionAt(10) == 0 && segmented->OptionAt(152) == 1 && segmented->OptionAt(300) == 2,
       "points map to options\n");
    segmented->OnPointerUp(Pt(250, 10));
    ok(segmented->Selected() == 2 && recorder.Last == ControlValueChanged, "a click selects an option\n");
    ULONGLONG now = AnimationClock::Now();
    ok(segmented->SlidePosition(now) < 2.0, "the selection block slides\n");
    ok(segmented->SlidePosition(now + LIBER_DUR_NORMAL) == 2.0, "and arrives at the option\n");
    ok(segmented->OnKeyDown(VK_LEFT) && segmented->Selected() == 1, "arrow keys move the selection\n");
    ok(!segmented->OnKeyDown(VK_UP), "other keys are not used\n");
    segmented->Select(0, FALSE);
    ok(segmented->SlidePosition(AnimationClock::Now()) == 0.0, "a silent select jumps\n");

    ok(tabs->Count() == 3 && tabs->Selected() == 0, "three tabs, the first selected\n");
    ok(tabs->TabAt(tabs->Bounds().left + 1) == 0 && tabs->TabAt(tabs->Bounds().right - 1) == 2,
       "points map to tabs\n");
    ok(tabs->OnKeyDown(VK_RIGHT) && tabs->Selected() == 1, "arrow keys move between tabs\n");
}

static void TestNavigationAndFeedback()
{
    StackPanel list(Orientation::Vertical, 2);
    NavItem *home = new NavItem(L"home_20_regular", L"home_20_filled", L"Home", NULL);
    NavItem *docs = new NavItem(L"document_20_regular", NULL, L"Documents", L"12");

    list.AddChild(home);
    list.AddChild(docs);
    Place(list, 0, 0, 248, 200);
    home->SetSelected(TRUE);
    ok(home->IsSelected() && !docs->IsSelected(), "one item is selected\n");
    docs->OnClick();
    ok(docs->IsSelected() && !home->IsSelected(), "selecting another item clears the first\n");
    ok(home->Bounds().bottom - home->Bounds().top == 36, "navigation items are 36 high\n");

    InfoBar info(InfoSeverity::Info, L"Update available", L"LiberNT 26.12 preview.");
    InfoBar ok1(InfoSeverity::Success, L"Protected", L"");
    InfoBar warn(InfoSeverity::Caution, L"Storage", L"12 GB left.");
    InfoBar error(InfoSeverity::Critical, L"Network", L"");
    ok(info.Background() == LIBER_COLOR_INFO_BG && !wcscmp(info.IconName(), L"info_20_filled"), "info bar\n");
    ok(ok1.Foreground() == LIBER_COLOR_SUCCESS && !wcscmp(ok1.IconName(), L"checkmark_circle_20_filled"),
       "success bar\n");
    ok(warn.Background() == LIBER_COLOR_CAUTION_BG && !wcscmp(warn.IconName(), L"warning_20_filled"), "caution bar\n");
    ok(error.Foreground() == LIBER_COLOR_CRITICAL && !wcscmp(error.IconName(), L"error_circle_20_filled"),
       "critical bar\n");

    DialogPanel dialog(L"Delete file?", L"This cannot be undone.");
    Recorder recorder;
    Button *cancel = dialog.AddButton(L"Cancel", 1, FALSE, &recorder);
    Button *remove = dialog.AddButton(L"Delete", 2, TRUE, &recorder);
    ok(cancel->Kind() == ButtonKind::Standard && remove->Kind() == ButtonKind::Accent && remove->Id() == 2,
       "a dialog has one accent action\n");
    ok(!remove->IsCapsule(), "dialog buttons keep the small radius\n");
    Place(dialog, 0, 0, 800, 600);
    ok(dialog.Bounds().right - dialog.Bounds().left == LIBER_DIALOG_WIDTH, "a dialog has its fixed width\n");
    remove->OnClick();
    ok(recorder.Source == remove && recorder.Last == ControlClicked, "dialog buttons report to the owner\n");

    Widget plain;
    ok(plain.Tooltip()[0] == 0, "no tooltip by default\n");
    plain.SetTooltip(L"Open a real menu");
    ok(!wcscmp(plain.Tooltip(), L"Open a real menu"), "a tooltip is kept\n");
    plain.SetTooltip(NULL);
    ok(plain.Tooltip()[0] == 0, "a tooltip can be removed\n");
}

START_TEST(Controls)
{
    LONG failures = AssertionFailures();

    TestSlider();
    TestTiles();
    TestLabel();
    TestButtons();
    TestSelection();
    TestTextField();
    TestSegmentedAndTabs();
    TestNavigationAndFeedback();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
}
