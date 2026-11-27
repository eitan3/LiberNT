/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test hover, press, click, capture and keyboard focus routing
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <liberui.h>

using namespace LiberUI;

class Button : public Widget
{
public:
    Button(int width, int height) : Enters(0), Leaves(0), Downs(0), Ups(0), Clicks(0), Keys(0), EatKey(0)
    {
        SetSize(width, height);
        SetHitTestable(TRUE);
        SetFocusable(TRUE);
    }
    void OnPointerEnter() { Enters++; }
    void OnPointerLeave() { Leaves++; }
    void OnPointerDown(POINT pt) { UNREFERENCED_PARAMETER(pt); Downs++; }
    void OnPointerUp(POINT pt) { UNREFERENCED_PARAMETER(pt); Ups++; }
    void OnClick() { Clicks++; }
    BOOL OnKeyDown(UINT vk) { Keys++; return vk == EatKey; }
    UINT Enters, Leaves, Downs, Ups, Clicks, Keys, EatKey;
};

class FocusHost : public IWidgetHost
{
public:
    FocusHost() : Focused(NULL), Changes(0) {}
    void InvalidateRect(const RECT &rc) { UNREFERENCED_PARAMETER(rc); }
    void RequestLayout() {}
    void SetCapture(Widget *widget) { UNREFERENCED_PARAMETER(widget); }
    void OnFocusChanged(Widget *widget) { Focused = widget; Changes++; }
    void OnWidgetRemoved(Widget *widget) { UNREFERENCED_PARAMETER(widget); }
    Widget *Focused;
    UINT Changes;
};

static POINT Pt(LONG x, LONG y)
{
    POINT pt = { x, y };
    return pt;
}

static void Layout(Widget &root, int width, int height)
{
    SIZE available = { width, height };
    RECT slot = { 0, 0, width, height };
    root.Measure(available);
    root.Arrange(slot);
}

static void TestPointer()
{
    StackPanel root(Orientation::Horizontal, 10);
    Button *a = new Button(50, 50);
    Button *b = new Button(50, 50);
    Widget *label = new Widget();
    InputRouter input;

    label->SetSize(20, 20);
    label->SetAlign(Align::Start, Align::Start);
    a->AddChild(label);
    root.AddChild(a);
    root.AddChild(b);
    root.SetAlign(Align::Start, Align::Start);
    Layout(root, 200, 50);
    input.SetRoot(&root);

    ok(root.HitTest(Pt(5, 5)) == a, "a non-hit-testable label passes hits to its button\n");
    ok(root.HitTest(Pt(55, 5)) == NULL, "the gap hits nothing\n");
    ok(root.HitTest(Pt(65, 5)) == b, "hit b\n");

    input.PointerMove(Pt(10, 10));
    ok(input.Hover() == a && a->Enters == 1 && a->HasState(StateHover), "hover a\n");
    input.PointerMove(Pt(20, 10));
    ok(a->Enters == 1, "moving inside does not re-enter\n");
    input.PointerMove(Pt(70, 10));
    ok(a->Leaves == 1 && !a->HasState(StateHover) && input.Hover() == b && b->Enters == 1, "hover moves to b\n");
    input.PointerLeave();
    ok(input.Hover() == NULL && b->Leaves == 1, "leaving the surface clears hover\n");

    input.PointerDown(Pt(10, 10));
    ok(input.Pressed() == a && input.Capture() == a && a->HasState(StatePressed) && a->Downs == 1, "press a\n");
    ok(input.Focus() == a && a->HasState(StateFocused), "pressing focuses\n");
    input.PointerMove(Pt(70, 10));
    ok(input.Hover() == NULL && b->Enters == 1, "captured pointer does not hover other widgets\n");
    input.PointerUp(Pt(70, 10));
    ok(a->Ups == 1 && a->Clicks == 0 && !a->HasState(StatePressed), "release outside does not click\n");
    ok(input.Capture() == NULL && input.Hover() == b, "capture released and hover restored\n");

    input.PointerDown(Pt(10, 10));
    input.PointerUp(Pt(30, 30));
    ok(a->Clicks == 1, "release inside clicks\n");

    b->SetEnabled(FALSE);
    input.PointerDown(Pt(70, 10));
    ok(input.Pressed() == NULL && b->Downs == 0, "disabled widget ignores presses\n");
    input.PointerMove(Pt(70, 10));
    ok(input.Hover() == NULL, "disabled widget does not hover\n");
    b->SetEnabled(TRUE);

    input.PointerDown(Pt(70, 10));
    root.RemoveChild(b);
    ok(input.Pressed() == b, "router still tracks b without a host\n");
    input.Forget(b);
    ok(input.Pressed() == NULL && input.Capture() == NULL && input.Focus() != b, "Forget clears b\n");
    delete b;
}

static void TestKeyboard()
{
    FocusHost host;
    StackPanel root(Orientation::Vertical, 0);
    GridPanel *grid = new GridPanel(3, 0, 0);
    Button *top = new Button(10, 10);
    Button *cells[6];
    InputRouter input;

    root.SetHost(&host);
    root.AddChild(top);
    root.AddChild(grid);
    for (int i = 0; i < 6; i++)
    {
        cells[i] = new Button(10, 10);
        grid->AddChild(cells[i]);
    }
    cells[4]->SetFocusable(FALSE);
    Layout(root, 30, 30);
    input.SetRoot(&root);

    ok(input.KeyDown(VK_TAB, FALSE) && input.Focus() == top, "Tab from nothing focuses the first widget\n");
    ok(host.Focused == top && host.Changes == 1, "host told about focus\n");
    input.KeyDown(VK_TAB, FALSE);
    ok(input.Focus() == cells[0], "Tab goes into the grid\n");
    input.KeyDown(VK_TAB, TRUE);
    ok(input.Focus() == top, "Shift+Tab goes back\n");
    input.KeyDown(VK_TAB, TRUE);
    ok(input.Focus() == cells[5], "Shift+Tab wraps to the last widget\n");
    input.KeyDown(VK_TAB, FALSE);
    ok(input.Focus() == top, "Tab wraps to the first widget\n");

    input.SetFocus(cells[0]);
    input.KeyDown(VK_RIGHT, FALSE);
    ok(input.Focus() == cells[1], "Right in the grid\n");
    input.KeyDown(VK_DOWN, FALSE);
    ok(input.Focus() == cells[1], "Down onto a non-focusable cell finds nothing further and keeps focus\n");
    input.SetFocus(cells[2]);
    input.KeyDown(VK_DOWN, FALSE);
    ok(input.Focus() == cells[5], "Down a row\n");
    input.KeyDown(VK_LEFT, FALSE);
    ok(input.Focus() == cells[3], "Left skips the non-focusable cell\n");
    input.KeyDown(VK_UP, FALSE);
    ok(input.Focus() == cells[0], "Up a row\n");
    ok(!input.KeyDown(VK_LEFT, FALSE) || input.Focus() != cells[0], "Left at the start of the grid\n");
    input.SetFocus(cells[0]);
    input.KeyDown(VK_UP, FALSE);
    ok(input.Focus() == top, "Up out of the grid reaches the stack\n");

    UINT clicks = top->Clicks;
    ok(input.KeyDown(VK_RETURN, FALSE) && top->Clicks == clicks + 1, "Enter clicks the focused widget\n");
    ok(input.KeyDown(VK_SPACE, FALSE) && top->Clicks == clicks + 2, "Space clicks the focused widget\n");
    ok(!input.KeyDown(VK_ESCAPE, FALSE), "Escape is left to the surface\n");
    top->EatKey = VK_ESCAPE;
    ok(input.KeyDown(VK_ESCAPE, FALSE), "a widget may handle a key first\n");

    ok(!input.SetFocus(cells[4]), "non-focusable widget refuses focus\n");
    cells[5]->SetVisible(FALSE);
    input.SetFocus(cells[3]);
    input.KeyDown(VK_TAB, FALSE);
    ok(input.Focus() == top, "hidden widgets are skipped by Tab\n");

    input.SetFocus(cells[3]);
    grid->SetVisible(FALSE);
    ok(NextInTree(&root, NULL, TRUE) == top, "hidden subtree is skipped\n");

    clicks = cells[3]->Clicks;
    ok(!input.KeyDown(VK_RETURN, FALSE) && cells[3]->Clicks == clicks, "Enter does not click a hidden focused widget\n");
    ok(input.Focus() == NULL && !cells[3]->HasState(StateFocused), "a hidden widget loses focus on the next key\n");
    root.SetHost(NULL);
}

START_TEST(Input)
{
    LONG failures = AssertionFailures();

    TestPointer();
    TestKeyboard();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
}
