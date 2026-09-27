#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct StripScratch;

struct MorphTheme {
    static constexpr float kLoopSeconds = 28;

    bool Load(ID3D11Device* device, std::wstring const& path);
    void Update(float time, float monitorWidth, float monitorHeight, float originX, float originY);
    void Draw(ID3D11DeviceContext* context, float targetWidth, float targetHeight, const float block[4]);

private:
    int strips = 0;
    int pointsPerStrip = 0;
    std::vector<float> shapes[2];
    std::vector<float> stripProps[2];
    std::vector<float> points;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11Buffer> pointBuffer;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11ShaderResourceView> pointView;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> rasterizer;
    float logoX = 0;
    float logoY = 0;
    float logoHalfSide = 0;

    bool ReadShapes(std::wstring const& path);
    bool CreatePipeline(ID3D11Device* device);
    std::vector<float> StripDelays(int to) const;
    void Interpolate(int strip, float time, int from, int to, float start, float delay, StripScratch& scratch) const;
    void Ripple(int strip, float time, StripScratch const& scratch);
    void ShapeStrip(int strip, StripScratch& scratch);
    void SmoothWidths(int strip);
    void ToPixels(int strip, float time, float monitorWidth, float monitorHeight, float originX, float originY);
};
