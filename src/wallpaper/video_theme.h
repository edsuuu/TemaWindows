#pragma once

#include <windows.h>
#include <d3d11.h>
#include <mfmediaengine.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

struct VideoTheme {
    bool Open(ID3D11Device* device, std::wstring const& path);
    void Close();
    void Pause(bool paused);
    ID3D11ShaderResourceView* Frame();
    float Aspect() const;

private:
    ComPtr<IMFMediaEngine> engine;
    ComPtr<IMFDXGIDeviceManager> manager;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    DWORD width = 0;
    DWORD height = 0;
    bool paused = false;
    bool ready = false;
};
