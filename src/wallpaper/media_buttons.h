#pragma once

#include <windows.h>
#include <vector>

void PlaceMediaButtons(std::vector<RECT> const& rects, bool visible);
int HoveredMediaButton(int monitor);
int PressedMediaButton(int monitor);
