#pragma once

#include <windows.h>
#include <audioclient.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct Equalizer {
    static constexpr int kSamples = 2048;
    static constexpr int kBands = 28;

    float ring[kSamples] = {};
    float level[kBands] = {};
    float average[kBands] = {};
    int position = 0;

    void Read(IAudioCaptureClient* capture);
    void Update(bool active, float dt);
    void Simulate(double time, float dt);

private:
    void Approach(float const* target, float dt);
};

struct LoopbackCapture {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    ULONGLONG nextAttempt = 0;

    void Want(bool wanted, ULONGLONG now);
};

bool SpotifyAudible();
