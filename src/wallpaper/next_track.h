#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

struct NextTrack {
    std::wstring after;
    std::wstring title;
    std::wstring artist;
    std::shared_ptr<std::vector<BYTE>> cover;
};

int NextCoverSize();
void StartNextTrackService();
NextTrack CurrentNextTrack();
