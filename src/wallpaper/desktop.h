#pragma once

#include <windows.h>

float DpiScale();
HWND FindWallpaperParent();
bool ComputeWallpaperHidden();
void SetWallpaperHidden(bool hidden);
bool WallpaperHidden();
void RestoreSelectionColors();
