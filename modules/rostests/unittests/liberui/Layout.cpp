/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test widget measure and arrange with stack and grid panels
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <liberui.h>

using namespace LiberUI;

class TestHost : public IWidgetHost
{
public:
    TestHost() : Layouts(0), Removed(NULL) { SetRectEmpty(&Dirty); }
    void InvalidateRect(const RECT &rc) { UnionRect(&Dirty, &Dirty, &rc); }
    void RequestLayout() { Layouts++; }
    void SetCapture(Widget *widget) { UNREFERENCED_PARAMETER(widget); }
    void OnFocusChanged(Widget *widget) { UNREFERENCED_PARAMETER(widget); }
    void OnWidgetRemoved(Widget *widget) { Removed = widget; }
    RECT Dirty;
    UINT Layouts;
    Widget *Removed;
};

static Widget *Box(int width, int height)
{
    Widget *w = new Widget();
    w->SetSize(width, height);
    return w;
}

static void CheckRect(const RECT &rc, LONG l, LONG t, LONG r, LONG b, const char *what)
{
    ok(rc.left == l && rc.top == t && rc.right == r && rc.bottom == b, "%s is (%ld,%ld,%ld,%ld), expected (%ld,%ld,%ld,%ld)\n",
       what, rc.left, rc.top, rc.right, rc.bottom, l, t, r, b);
}

static void Layout(Widget &root, int width, int height)
{
    SIZE available = { width, height };
    RECT slot = { 0, 0, width, height };
    root.Measure(available);
    root.Arrange(slot);
}

static void TestVerticalStack()
{
    StackPanel stack(Orientation::Vertical, LIBER_GROUP_GAP);
    Thickness pad = { LIBER_FLYOUT_PAD, LIBER_FLYOUT_PAD, LIBER_FLYOUT_PAD, LIBER_FLYOUT_PAD };
    Widget *a = Box(-1, LIBER_SETTING_ROW_HEIGHT);
    Widget *b = Box(-1, LIBER_SETTING_ROW_HEIGHT);
    Widget *c = Box(100, LIBER_SETTING_CHILD_ROW_HEIGHT);

    stack.SetPadding(pad);
    stack.AddChild(a);
    stack.AddChild(b);
    stack.AddChild(c);
    c->SetAlign(Align::End, Align::Stretch);

    SIZE available = { 372, LayoutInfinite };
    SIZE size = stack.Measure(available);
    ok(size.cy == 12 + 64 + 2 + 64 + 2 + 48 + 12, "stack height %ld\n", size.cy);
    ok(size.cx == 12 + 100 + 12, "stack width %ld\n", size.cx);

    RECT slot = { 0, 0, 372, size.cy };
    stack.Arrange(slot);
    CheckRect(a->Bounds(), 12, 12, 360, 76, "row a");
    CheckRect(b->Bounds(), 12, 78, 360, 142, "row b");
    CheckRect(c->Bounds(), 260, 144, 360, 192, "row c");
    ok(stack.ChildCount() == 3, "child count\n");

    b->SetVisible(FALSE);
    Layout(stack, 372, LayoutInfinite);
    ok(stack.Desired().cy == 12 + 64 + 2 + 48 + 12, "hidden rows take no space, height %ld\n", stack.Desired().cy);
    CheckRect(c->Bounds(), 260, 78, 360, 126, "row c after hiding b");
    ok(IsRectEmpty(&b->Bounds()), "hidden row has empty bounds\n");
}

static void TestHorizontalStack()
{
    StackPanel stack(Orientation::Horizontal, 4);
    Widget *a = Box(LIBER_DOCK_CELL, LIBER_DOCK_CELL);
    Widget *b = Box(LIBER_DOCK_CELL, LIBER_DOCK_CELL);
    Widget *c = Box(20, 10);
    Thickness margin = { 2, 0, 3, 0 };

    c->SetAlign(Align::Stretch, Align::Center);
    c->SetMargin(margin);
    stack.AddChild(a);
    stack.AddChild(b);
    stack.AddChild(c);
    stack.SetAlign(Align::Center, Align::Center);

    Layout(stack, 400, LIBER_DOCK_HEIGHT);
    ok(stack.Desired().cx == 48 + 4 + 48 + 4 + 25 && stack.Desired().cy == 48, "dock size %ld x %ld\n",
       stack.Desired().cx, stack.Desired().cy);
    LONG left = (400 - 129) / 2;
    CheckRect(stack.Bounds(), left, 6, left + 129, 54, "centered dock");
    CheckRect(a->Bounds(), left, 6, left + 48, 54, "cell a");
    CheckRect(b->Bounds(), left + 52, 6, left + 100, 54, "cell b");
    CheckRect(c->Bounds(), left + 106, 25, left + 126, 35, "margin and centering");
}

static void TestGrid()
{
    GridPanel grid(6, 8, 8);
    Widget *cells[8];

    for (int i = 0; i < 8; i++)
    {
        cells[i] = Box(-1, i == 7 ? 90 : 80);
        grid.AddChild(cells[i]);
    }

    Layout(grid, 6 * 100 + 5 * 8, LayoutInfinite);
    ok(grid.Desired().cy == 80 + 8 + 90, "grid height %ld\n", grid.Desired().cy);
    CheckRect(cells[0]->Bounds(), 0, 0, 100, 80, "cell 0");
    CheckRect(cells[5]->Bounds(), 540, 0, 640, 80, "cell 5");
    CheckRect(cells[6]->Bounds(), 0, 88, 100, 168, "cell 6 in a taller row");
    CheckRect(cells[7]->Bounds(), 108, 88, 208, 178, "cell 7");

    cells[2]->SetVisible(FALSE);
    Layout(grid, 640, LayoutInfinite);
    CheckRect(cells[3]->Bounds(), 216, 0, 316, 80, "hidden cell does not take a slot");
    CheckRect(cells[7]->Bounds(), 0, 88, 100, 178, "next row reflows");

    GridPanel unbounded(3, 4, 4);
    unbounded.AddChild(Box(30, 10));
    unbounded.AddChild(Box(40, 10));
    SIZE available = { LayoutInfinite, LayoutInfinite };
    SIZE size = unbounded.Measure(available);
    ok(size.cx == 3 * 40 + 2 * 4 && size.cy == 10, "unbounded grid %ld x %ld\n", size.cx, size.cy);
}

static void TestHostNotifications()
{
    TestHost host;
    StackPanel *root = new StackPanel(Orientation::Vertical, 0);
    Widget *child = Box(50, 20);

    root->SetHost(&host);
    root->AddChild(child);
    ok(host.Layouts >= 1, "adding a child requests layout\n");
    Layout(*root, 100, 100);
    ok(child->Host() == &host, "children find the host\n");

    SetRectEmpty(&host.Dirty);
    child->SetState(StateHover, TRUE);
    CheckRect(child->Bounds(), 0, 0, 50, 20, "fixed width child");
    CheckRect(host.Dirty, 0, 0, 50, 20, "state change invalidates the widget");
    ok(child->HasState(StateHover) && child->State() == StateHover, "state set\n");

    SetRectEmpty(&host.Dirty);
    child->SetState(StateHover, TRUE);
    ok(IsRectEmpty(&host.Dirty), "unchanged state does not invalidate\n");

    root->SetEnabled(FALSE);
    ok(!child->IsEnabled(), "disabled parent disables children\n");
    root->SetEnabled(TRUE);

    UINT layouts = host.Layouts;
    root->RemoveChild(child);
    ok(host.Removed == child && host.Layouts > layouts && root->ChildCount() == 0, "removal is reported\n");
    ok(child->Parent() == NULL && child->Host() == NULL, "removed child is detached\n");
    delete child;

    root->SetHost(NULL);
    delete root;
}

static void TestTreeRules()
{
    Widget *a = new Widget();
    Widget *b = new Widget();
    LONG failures = AssertionFailures();

    a->AddChild(b);
    b->AddChild(a);
    ok(a->Parent() == NULL && b->Parent() == a, "a cycle is refused\n");
    ok(AssertionFailures() > failures, "the refused cycle asserts\n");
    a->AddChild(b);
    ok(a->ChildCount() == 1, "adding a child twice is refused\n");
    ok(a->IsAncestorOf(b) && !b->IsAncestorOf(a) && a->IsAncestorOf(a), "IsAncestorOf\n");
    failures = AssertionFailures();
    a->AddChild(NULL);
    ok(a->ChildCount() == 1 && AssertionFailures() > failures, "a NULL child is refused\n");
    delete a;
}

START_TEST(Layout)
{
    LONG failures = AssertionFailures();

    TestVerticalStack();
    TestHorizontalStack();
    TestGrid();
    TestHostNotifications();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
    TestTreeRules();
}
