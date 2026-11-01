#pragma once

#include <windows.h>

float DpiScale();
HWND FindWallpaperParent();
bool ComputeWallpaperHidden(bool apps);
void SetWallpaperHidden(bool hidden);
bool WallpaperHidden();
void RestoreSelectionColors();
