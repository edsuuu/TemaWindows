#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

struct GoldenTheme {
    bool Create(ID3D11Device* device);
    void Load(ID3D11Device* device, ID3D11DeviceContext* context, std::wstring const& path);
    void Unload();
    bool Loaded() const;
    void Draw(ID3D11DeviceContext* context, RECT const& monitor, float time, const float block[4]);

private:
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11ShaderResourceView> image;
    float imageAspect = 1;
};
