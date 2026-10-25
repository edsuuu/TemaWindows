#pragma once

#include <windows.h>
#include <memory>
#include <string>
#include <vector>

constexpr int kMediaButtons = 4;

struct NowPlaying {
    std::wstring title;
    std::wstring artist;
    bool playing = false;
    bool shuffle = false;
    bool elsewhere = false;
    std::shared_ptr<std::vector<BYTE>> cover;
    double position = 0;
    double duration = 0;
    double readAt = 0;
};

int CoverSize();
std::shared_ptr<std::vector<BYTE>> DecodeImage(IStream* input, int size);
void StartMediaWatcher();
NowPlaying CurrentMedia();
void SendMediaCommand(int button);
