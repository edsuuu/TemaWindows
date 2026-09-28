#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

struct NowPlaying {
    std::wstring title;
    std::wstring artist;
    bool playing = false;
    std::shared_ptr<std::vector<BYTE>> cover;
    double position = 0;
    double duration = 0;
    double readAt = 0;
};

int CoverSize();
void StartMediaWatcher();
NowPlaying CurrentMedia();
void SendMediaCommand(int button);
