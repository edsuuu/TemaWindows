#pragma once

#include <windows.h>
#include <vector>

enum class Zone { Media = 0, Light = 1 };

void PlaceClickZones(Zone zone, std::vector<RECT> const& rects, bool visible);
int HoveredMediaButton(int monitor);
int PressedMediaButton(int monitor);
bool LightHovered(int monitor);
