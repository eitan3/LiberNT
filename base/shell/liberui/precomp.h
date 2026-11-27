/*
 * PROJECT:     LiberNT Liber UI
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private declarations of the Liber UI library
 * COPYRIGHT:   Copyright 2026 Eitan Hadar <eitanhadar4@gmail.com>
 */

#pragma once

#include "liberui.h"

#include <math.h>

namespace LiberUI
{

void AssertFailed(const char *file, int line, const char *condition);

struct SurfaceGpu;
SurfaceGpu *GpuCreate();
BOOL GpuAttach(SurfaceGpu *gpu, HWND hwnd);
void GpuDestroy(SurfaceGpu *gpu);

}

#define LUI_ASSERT(cond) \
    do { if (!(cond)) LiberUI::AssertFailed(__FILE__, __LINE__, #cond); } while (0)
