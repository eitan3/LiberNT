/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Easing curves, damped springs and the shared animation clock
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#include "precomp.h"

namespace LiberUI
{

static double BezierCoordinate(double u, double p1, double p2)
{
    double v = 1.0 - u;
    return 3.0 * v * v * u * p1 + 3.0 * v * u * u * p2 + u * u * u;
}

static double BezierSlope(double u, double p1, double p2)
{
    double v = 1.0 - u;
    return 3.0 * v * v * p1 + 6.0 * v * u * (p2 - p1) + 3.0 * u * u * (1.0 - p2);
}

double CubicBezier(const LIBER_BEZIER &curve, double t)
{
    LUI_ASSERT(curve.X1 >= 0.0f && curve.X1 <= 1.0f && curve.X2 >= 0.0f && curve.X2 <= 1.0f);

    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;

    double u = t;
    for (int i = 0; i < 8; i++)
    {
        double error = BezierCoordinate(u, curve.X1, curve.X2) - t;
        if (fabs(error) < 1e-7)
            return BezierCoordinate(u, curve.Y1, curve.Y2);
        double slope = BezierSlope(u, curve.X1, curve.X2);
        if (fabs(slope) < 1e-6)
            break;
        u -= error / slope;
        if (u < 0.0 || u > 1.0)
            break;
    }

    double low = 0.0, high = 1.0;
    u = t;
    for (int i = 0; i < 64; i++)
    {
        double x = BezierCoordinate(u, curve.X1, curve.X2);
        if (fabs(x - t) < 1e-7)
            break;
        if (x < t)
            low = u;
        else
            high = u;
        u = (low + high) / 2.0;
    }
    return BezierCoordinate(u, curve.Y1, curve.Y2);
}

static void SpringTerms(const LIBER_SPRING &spring, double *decay, double *damped)
{
    LUI_ASSERT(spring.Zeta > 0.0f && spring.Zeta < 1.0f && spring.Omega > 0.0f);
    *decay = spring.Zeta * spring.Omega;
    *damped = spring.Omega * sqrt(1.0 - (double)spring.Zeta * spring.Zeta);
}

static double SpringOffset(const LIBER_SPRING &spring, double y0, double v0, double t, double *velocity)
{
    double decay, damped;

    SpringTerms(spring, &decay, &damped);

    double a = y0;
    double b = (v0 + decay * y0) / damped;
    double e = exp(-decay * t);
    double c = cos(damped * t);
    double s = sin(damped * t);

    if (velocity)
        *velocity = e * ((-decay * a + damped * b) * c + (-decay * b - damped * a) * s);
    return e * (a * c + b * s);
}

double SpringPosition(const LIBER_SPRING &spring, double t)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;
    return 1.0 + SpringOffset(spring, -1.0, 0.0, t, NULL);
}

static const LIBER_SPRING *SpringFor(Easing easing)
{
    switch (easing)
    {
        case Easing::SpringSoft:
            return &LiberSpringSoft;
        case Easing::Spring:
            return &LiberSpringStandard;
        case Easing::SpringBouncy:
            return &LiberSpringBouncy;
        default:
            return NULL;
    }
}

BOOL IsSpring(Easing easing)
{
    return SpringFor(easing) != NULL;
}

double Ease(Easing easing, double t)
{
    if (t <= 0.0)
        return 0.0;
    if (t >= 1.0)
        return 1.0;

    switch (easing)
    {
        case Easing::Linear:
            return t;
        case Easing::Out:
            return CubicBezier(LiberEaseOut, t);
        case Easing::In:
            return CubicBezier(LiberEaseIn, t);
        case Easing::InOut:
            return CubicBezier(LiberEaseInOut, t);
        default:
            break;
    }

    const LIBER_SPRING *spring = SpringFor(easing);
    LUI_ASSERT(spring != NULL);
    return spring ? SpringPosition(*spring, t) : t;
}

BOOL AnimationsEnabled()
{
    BOOL enabled = TRUE;

    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
        return TRUE;
    return enabled;
}

Animation::Animation()
    : m_From(0.0), m_To(0.0), m_Velocity(0.0), m_Start(0), m_Duration(0), m_Delay(0), m_Easing(Easing::Linear),
      m_Running(FALSE)
{
}

void Animation::Set(double value)
{
    m_From = m_To = value;
    m_Velocity = 0.0;
    m_Running = FALSE;
}

void Animation::Start(double from, double to, UINT durationMs, Easing easing, ULONGLONG now, UINT delayMs)
{
    m_From = from;
    m_To = to;
    m_Velocity = 0.0;
    m_Start = now;
    m_Duration = durationMs;
    m_Delay = delayMs;
    m_Easing = easing;
    m_Running = durationMs != 0 && from != to && AnimationsEnabled();
}

void Animation::Retarget(double to, ULONGLONG now)
{
    if (!IsRunning(now))
    {
        Start(Value(now), to, m_Duration, m_Easing, now);
        return;
    }

    double value = Value(now);
    double velocity = Velocity(now);
    UINT duration = m_Duration;

    Start(value, to, duration, m_Easing, now);
    if (m_Running && IsSpring(m_Easing))
        m_Velocity = velocity * duration;
}

void Animation::Finish()
{
    m_From = m_To;
    m_Velocity = 0.0;
    m_Running = FALSE;
}

double Animation::Progress(ULONGLONG now) const
{
    if (now < m_Start + m_Delay)
        return 0.0;
    double t = (double)(now - m_Start - m_Delay) / m_Duration;
    return t > 1.0 ? 1.0 : t;
}

BOOL Animation::IsRunning(ULONGLONG now) const
{
    return m_Running && Progress(now) < 1.0;
}

double Animation::Value(ULONGLONG now) const
{
    if (!m_Running)
        return m_To;

    double t = Progress(now);
    if (t >= 1.0)
        return m_To;

    const LIBER_SPRING *spring = SpringFor(m_Easing);
    if (spring)
        return m_To + SpringOffset(*spring, m_From - m_To, m_Velocity, t, NULL);
    return m_From + (m_To - m_From) * Ease(m_Easing, t);
}

double Animation::Velocity(ULONGLONG now) const
{
    if (!m_Running)
        return 0.0;

    double t = Progress(now);
    if (t >= 1.0 || now < m_Start + m_Delay)
        return 0.0;

    const LIBER_SPRING *spring = SpringFor(m_Easing);
    if (spring)
    {
        double velocity;
        SpringOffset(*spring, m_From - m_To, m_Velocity, t, &velocity);
        return velocity / m_Duration;
    }

    double h = 1e-4;
    double t0 = t > h ? t - h : 0.0;
    double t1 = t + h < 1.0 ? t + h : 1.0;
    return (m_To - m_From) * (Ease(m_Easing, t1) - Ease(m_Easing, t0)) / ((t1 - t0) * m_Duration);
}

AnimationClock::AnimationClock()
    : m_Count(0), m_Timer(0)
{
    ZeroMemory(m_Clients, sizeof(m_Clients));
}

AnimationClock &AnimationClock::Instance()
{
    static AnimationClock clock;
    return clock;
}

ULONGLONG AnimationClock::Now()
{
    return GetTickCount64();
}

BOOL AnimationClock::Contains(IAnimationClient *client) const
{
    for (UINT i = 0; i < m_Count; i++)
    {
        if (m_Clients[i] == client)
            return TRUE;
    }
    return FALSE;
}

void AnimationClock::Add(IAnimationClient *client)
{
    LUI_ASSERT(client != NULL);
    if (!client || Contains(client))
        return;

    LUI_ASSERT(m_Count < MaxClients);
    if (m_Count >= MaxClients)
        return;

    m_Clients[m_Count++] = client;
    if (m_Count == 1)
        StartTimer();
}

void AnimationClock::Remove(IAnimationClient *client)
{
    for (UINT i = 0; i < m_Count; i++)
    {
        if (m_Clients[i] != client)
            continue;

        m_Clients[i] = m_Clients[--m_Count];
        m_Clients[m_Count] = NULL;
        if (m_Count == 0)
            StopTimer();
        return;
    }
}

UINT AnimationClock::Tick(ULONGLONG now)
{
    IAnimationClient *clients[MaxClients];
    UINT count = m_Count;

    CopyMemory(clients, m_Clients, count * sizeof(clients[0]));
    for (UINT i = 0; i < count; i++)
    {
        if (!Contains(clients[i]))
            continue;
        if (!clients[i]->OnAnimationFrame(now))
            Remove(clients[i]);
    }
    return m_Count;
}

void CALLBACK AnimationClock::TimerProc(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(msg);
    UNREFERENCED_PARAMETER(id);
    UNREFERENCED_PARAMETER(time);
    Instance().Tick(Now());
}

void AnimationClock::StartTimer()
{
    LUI_ASSERT(m_Timer == 0);
    m_Timer = ::SetTimer(NULL, 0, USER_TIMER_MINIMUM > 15 ? USER_TIMER_MINIMUM : 15, TimerProc);
}

void AnimationClock::StopTimer()
{
    if (m_Timer)
    {
        ::KillTimer(NULL, m_Timer);
        m_Timer = 0;
    }
}

}
