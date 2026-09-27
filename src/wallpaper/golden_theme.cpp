#include "wallpaper/golden_theme.h"

#include <d3dcompiler.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <vector>

static const char kShader[] = R"(
Texture2D image : register(t0);
SamplerState linearSampler : register(s0);
cbuffer Constants : register(b0) { float time; float aspect; float2 padding; float4 fit; float4 block; };
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD; };

// Um triângulo que cobre o monitor inteiro.
V vs(uint id : SV_VertexID) {
    V o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

// "Preencher": cobre o monitor sem distorcer, com zoom de 4% para sobrar imagem nas bordas para o movimento.
float2 FillUv(float2 uv) {
    uv = fit.zw + uv * fit.xy;
    return 0.5 + (uv - 0.5) * 0.96;
}

// Ondas largas em direções diferentes (ciclos de ~18 a 28 s): a forma inteira flui como tecido, devagar, com um giro
// leve e uma "respiração" em volta do centro. Perto da borda da imagem (últimos 12%) o movimento some, então o
// deslocamento máximo (~0,04) nunca lê fora dela, o que viraria uma faixa esticada.
float2 Flow(float2 uv) {
    float2 p = uv * float2(aspect, 1);
    float tt = time * 0.8;
    float2 d;
    d.x = sin(p.y * 2.3 + tt * 0.35) + 0.5 * sin((p.x + p.y) * 4.1 - tt * 0.28) + 0.25 * sin(p.x * 7.0 + tt * 0.45);
    d.y = cos(p.x * 2.1 - tt * 0.31) + 0.5 * cos((p.x - p.y) * 3.7 + tt * 0.38) + 0.25 * cos(p.y * 6.3 - tt * 0.41);

    float2 c = 0.5;
    float a = 0.035 * sin(tt * 0.15);
    float2 q = (uv - c) * float2(aspect, 1);
    q = float2(q.x * cos(a) - q.y * sin(a), q.x * sin(a) + q.y * cos(a)) / float2(aspect, 1);

    float2 m = min(uv, 1 - uv);
    return lerp(uv, c + q * (1 - 0.02 * sin(tt * 0.22)) + d * 0.012, smoothstep(0.0, 0.12, min(m.x, m.y)));
}

// Área escura suave atrás do bloco do canto (música + indicadores), para nenhuma borda clara passar brilhando atrás do
// texto: retângulo arredondado 24 px maior que o bloco, 75% mais escuro dentro, degradê de ~130 px para fora.
float CornerShade(float2 pixel) {
    float2 e = abs(pixel - (block.xy + block.zw) / 2) - (block.zw - block.xy) / 2 - 24 + 40;
    float dist = length(max(e, 0)) + min(max(e.x, e.y), 0) - 40;
    return 1 - 0.75 * smoothstep(110, -20, dist);
}

// A imagem ondulando, escurecida atrás do bloco do canto.
float4 ps(V i) : SV_Target {
    float3 color = image.Sample(linearSampler, Flow(FillUv(i.uv))).rgb;
    return float4(color * CornerShade(i.pos.xy), 1);
}
)";

struct Constants {
    float time;
    float aspect;
    float padding[2];
    float fit[4];
    float block[4];
};

// Compila os shaders (erro aqui = o FundoVivo não abre) e cria o amostrador e as constantes.
bool GoldenTheme::Create(ID3D11Device* device) {
    ComPtr<ID3DBlob> vertexCode, pixelCode;
    D3DCompile(kShader, sizeof kShader - 1, nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vertexCode, nullptr);
    D3DCompile(kShader, sizeof kShader - 1, nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &pixelCode, nullptr);
    if (!vertexCode || !pixelCode) return false;

    device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader);
    device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixelShader);

    D3D11_SAMPLER_DESC samplerDesc{D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP,
                                   D3D11_TEXTURE_ADDRESS_CLAMP, 0, 1, D3D11_COMPARISON_NEVER, {}, 0, D3D11_FLOAT32_MAX};
    device->CreateSamplerState(&samplerDesc, &sampler);

    D3D11_BUFFER_DESC bufferDesc{sizeof(Constants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    device->CreateBuffer(&bufferDesc, nullptr, &constants);
    return true;
}

// Carrega a imagem do tema (assets\fundo.png), reduzida para no máximo 2560 px de largura (o monitor tem 1920; poupa
// memória de vídeo) e com mipmaps. Sem a imagem, o tema fica preto.
void GoldenTheme::Load(ID3D11Device* device, ID3D11DeviceContext* context, std::wstring const& path) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    if (!wic || FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) return;

    UINT originalWidth, originalHeight;
    decoder->GetFrame(0, &frame);
    frame->GetSize(&originalWidth, &originalHeight);
    UINT width = std::min(originalWidth, 2560u), height = UINT(uint64_t(originalHeight) * width / originalWidth);

    wic->CreateBitmapScaler(&scaler);
    scaler->Initialize(frame.Get(), width, height, WICBitmapInterpolationModeFant);
    wic->CreateFormatConverter(&converter);
    converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
    std::vector<BYTE> pixels((size_t)width * height * 4);
    converter->CopyPixels(nullptr, width * 4, (UINT)pixels.size(), pixels.data());

    D3D11_TEXTURE2D_DESC textureDesc{width, height, 0, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                     D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, D3D11_RESOURCE_MISC_GENERATE_MIPS};
    ComPtr<ID3D11Texture2D> texture;
    device->CreateTexture2D(&textureDesc, nullptr, &texture);
    context->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), width * 4, 0);
    device->CreateShaderResourceView(texture.Get(), nullptr, &image);
    context->GenerateMips(image.Get());
    imageAspect = float(width) / height;
}

// Solta a imagem: ela só fica carregada neste tema.
void GoldenTheme::Unload() {
    image = nullptr;
    imageAspect = 1;
}

// Imagem carregada?
bool GoldenTheme::Loaded() const {
    return image != nullptr;
}

// Um monitor: a imagem inteira ondulando no modo "preencher" (monitor mais largo corta em cima e embaixo; mais alto,
// nas laterais), com a área escura atrás do bloco do canto (x0 y0 x1 y1 em pixels da janela).
void GoldenTheme::Draw(ID3D11DeviceContext* context, RECT const& monitor, float time, const float block[4]) {
    float width = float(monitor.right - monitor.left), height = float(monitor.bottom - monitor.top);
    float monitorAspect = width / height;
    Constants values{time, imageAspect, {}, {}, {block[0], block[1], block[2], block[3]}};

    if (monitorAspect > imageAspect) {
        values.fit[0] = 1;
        values.fit[1] = imageAspect / monitorAspect;
    } else {
        values.fit[0] = monitorAspect / imageAspect;
        values.fit[1] = 1;
    }
    values.fit[2] = (1 - values.fit[0]) / 2;
    values.fit[3] = (1 - values.fit[1]) / 2;

    D3D11_VIEWPORT viewport{float(monitor.left), float(monitor.top), width, height, 0, 1};
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vertexShader.Get(), nullptr, 0);
    context->PSSetShader(pixelShader.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, image.GetAddressOf());
    context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->UpdateSubresource(constants.Get(), 0, nullptr, &values, 0, 0);
    context->RSSetViewports(1, &viewport);
    context->Draw(3, 0);
}
