/*
 * PROJECT:     LiberNT Liber UI - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test easing curves, springs, animations and the animation clock
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include <apitest.h>

#include <liberui.h>

#include <math.h>

using namespace LiberUI;

static void TestBezier()
{
    static const LIBER_BEZIER Linear = { 0.0f, 0.0f, 1.0f, 1.0f };
    static const LIBER_BEZIER CssEase = { 0.25f, 0.1f, 0.25f, 1.0f };

    for (int i = 0; i <= 10; i++)
        ok(fabs(CubicBezier(Linear, i / 10.0) - i / 10.0) < 1e-6, "linear bezier at %d\n", i);
    ok(fabs(CubicBezier(CssEase, 0.5) - 0.8024033877) < 1e-5, "CSS ease at 0.5 = %f\n", CubicBezier(CssEase, 0.5));
    ok(CubicBezier(LiberEaseOut, -1.0) == 0.0 && CubicBezier(LiberEaseOut, 2.0) == 1.0, "bezier clamps\n");

    const Easing curves[] = { Easing::Out, Easing::In, Easing::InOut };
    for (UINT c = 0; c < _countof(curves); c++)
    {
        double last = 0.0;
        BOOL monotonic = TRUE;
        for (int i = 1; i <= 100; i++)
        {
            double v = Ease(curves[c], i / 100.0);
            if (v < last - 1e-9)
                monotonic = FALSE;
            last = v;
        }
        ok(monotonic, "curve %u is monotonic\n", c);
        ok(Ease(curves[c], 1.0) == 1.0 && Ease(curves[c], 0.0) == 0.0, "curve %u endpoints\n", c);
    }
    ok(Ease(Easing::Out, 0.2) > 0.6, "ease-out starts fast: %f\n", Ease(Easing::Out, 0.2));
    ok(Ease(Easing::In, 0.2) < 0.1, "ease-in starts slow: %f\n", Ease(Easing::In, 0.2));
}

static void CheckSpring(const LIBER_SPRING &spring, const float *table, const char *name)
{
    double worst = 0.0;
    for (UINT i = 0; i < LIBER_SPRING_TABLE_SIZE; i++)
    {
        double t = (double)i / (LIBER_SPRING_TABLE_SIZE - 1);
        double e = fabs(SpringPosition(spring, t) - table[i]);
        if (e > worst)
            worst = e;
    }
    ok(worst < 0.001, "%s spring differs from the spec table by %f\n", name, worst);
}

static void TestSprings()
{
    CheckSpring(LiberSpringSoft, LiberSpringSoftTable, "soft");
    CheckSpring(LiberSpringStandard, LiberSpringStandardTable, "standard");
    CheckSpring(LiberSpringBouncy, LiberSpringBouncyTable, "bouncy");

    double peak = 0.0;
    for (int i = 0; i <= 1000; i++)
    {
        double v = Ease(Easing::SpringBouncy, i / 1000.0);
        if (v > peak)
            peak = v;
    }
    ok(peak > 1.19 && peak < 1.22, "bouncy overshoot %f\n", peak);
    ok(Ease(Easing::Spring, 1.0) == 1.0 && Ease(Easing::Spring, 0.0) == 0.0, "spring endpoints\n");
    ok(IsSpring(Easing::SpringSoft) && !IsSpring(Easing::Out), "IsSpring\n");
}

static void TestAnimation()
{
    Animation a;

    a.Set(5.0);
    ok(a.Value(0) == 5.0 && !a.IsRunning(0), "Set\n");

    if (!AnimationsEnabled())
    {
        a.Start(0.0, 10.0, 200, Easing::Linear, 1000);
        ok(a.Value(1000) == 10.0 && !a.IsRunning(1000), "animations off jump to the end\n");
        skip("client area animations are off\n");
        return;
    }

    a.Start(0.0, 10.0, 200, Easing::Linear, 1000);
    ok(a.IsRunning(1000) && a.Value(1000) == 0.0, "start value\n");
    ok(fabs(a.Value(1100) - 5.0) < 1e-9, "linear midpoint %f\n", a.Value(1100));
    ok(fabs(a.Velocity(1100) - 0.05) < 1e-6, "linear velocity %f\n", a.Velocity(1100));
    ok(a.Value(1200) == 10.0 && !a.IsRunning(1200), "end value\n");
    ok(a.Value(5000) == 10.0, "after the end\n");

    a.Start(0.0, 10.0, 100, Easing::Linear, 0, 50);
    ok(a.Value(40) == 0.0 && a.IsRunning(40) && a.Velocity(40) == 0.0, "delay holds the start\n");
    ok(fabs(a.Value(100) - 5.0) < 1e-9, "delayed midpoint %f\n", a.Value(100));

    a.Start(3.0, 3.0, 100, Easing::Out, 0);
    ok(!a.IsRunning(0), "equal endpoints do not run\n");
    a.Start(0.0, 1.0, 0, Easing::Out, 0);
    ok(!a.IsRunning(0) && a.Value(0) == 1.0, "zero duration jumps\n");

    a.Start(0.0, 100.0, LIBER_DUR_SPRING, Easing::Spring, 0);
    double before = a.Value(150);
    double velocity = a.Velocity(150);
    ok(velocity > 0.0, "spring moving forward %f\n", velocity);
    a.Retarget(0.0, 150);
    ok(fabs(a.Value(150) - before) < 1e-9, "retarget keeps the value: %f vs %f\n", a.Value(150), before);
    ok(fabs(a.Velocity(150) - velocity) < 1e-6 * fabs(velocity) + 1e-9, "retarget keeps the velocity: %f vs %f\n",
       a.Velocity(150), velocity);
    ok(a.Target() == 0.0 && a.Value(152) > before, "momentum carries on briefly\n");
    ok(a.Value(150 + LIBER_DUR_SPRING) == 0.0, "spring settles on the new target\n");

    a.Start(0.0, 10.0, 100, Easing::Out, 0);
    a.Finish();
    ok(!a.IsRunning(0) && a.Value(0) == 10.0, "Finish\n");

    a.Start(0.0, 10.0, 100, Easing::Out, 0);
    double mid = a.Value(50);
    a.Retarget(20.0, 50);
    ok(fabs(a.Value(50) - mid) < 1e-9 && a.IsRunning(50), "bezier retarget starts from the current value\n");
}

class CountingClient : public IAnimationClient
{
public:
    CountingClient(UINT frames) : Frames(0), Limit(frames) {}
    BOOL OnAnimationFrame(ULONGLONG now)
    {
        UNREFERENCED_PARAMETER(now);
        return ++Frames < Limit;
    }
    UINT Frames;
    UINT Limit;
};

class RemovingClient : public IAnimationClient
{
public:
    RemovingClient(IAnimationClient *victim) : Victim(victim) {}
    BOOL OnAnimationFrame(ULONGLONG now)
    {
        UNREFERENCED_PARAMETER(now);
        AnimationClock::Instance().Remove(Victim);
        return TRUE;
    }
    IAnimationClient *Victim;
};

static void TestClock()
{
    AnimationClock &clock = AnimationClock::Instance();
    CountingClient once(1), three(3), victim(100);
    RemovingClient remover(&victim);
    UINT base = clock.Count();

    clock.Add(&once);
    clock.Add(&three);
    clock.Add(&three);
    ok(clock.Count() == base + 2, "duplicates are ignored, %u clients\n", clock.Count());
    clock.Tick(16);
    ok(once.Frames == 1 && !clock.Contains(&once), "finished client removed\n");
    clock.Tick(32);
    clock.Tick(48);
    ok(three.Frames == 3 && !clock.Contains(&three), "three frames then removed\n");
    clock.Tick(64);
    ok(three.Frames == 3, "no frames after removal\n");

    clock.Add(&remover);
    clock.Add(&victim);
    clock.Tick(80);
    ok(!clock.Contains(&victim) && victim.Frames == 0, "a client removed during a tick is not called\n");
    clock.Remove(&remover);
    ok(clock.Count() == base, "clock empty again\n");
}

START_TEST(Anim)
{
    LONG failures = AssertionFailures();

    TestBezier();
    TestSprings();
    TestAnimation();
    TestClock();
    ok(AssertionFailures() == failures, "%ld assertions failed\n", AssertionFailures() - failures);
}
