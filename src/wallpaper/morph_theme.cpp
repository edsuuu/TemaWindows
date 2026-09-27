#include "wallpaper/morph_theme.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

static const char kShader[] = R"(
struct Point { float4 placement; float4 look; float4 logoEdge; };
StructuredBuffer<Point> points : register(t0);
cbuffer Constants : register(b0) { float4 block; float2 size; uint pointsPerStrip; float padding; float4 logo; };
struct V {
    float4 pos : SV_Position;
    float offset : OFFSET;
    float4 look : LOOK;
    float2 logoEdge : LOGOEDGE;
    uint part : PART;
};

// Cada faixa vira duas fitas desenhadas em seguida (a de trás primeiro), com 2 vértices por ponto: a superfície, da
// borda até a largura dela, e a borda, estreita em volta da linha. Separadas porque a superfície larga se dobra nas
// curvas fechadas (no logo a cor vem da posição e dobrar não aparece) e a linha brilhante tem que ficar só na borda de
// verdade. offset = distância até a borda, na normal (px); placement = posição e normal; look = largura com sinal,
// tom, ponta e brilho; logoEdge = quanto é logo (0..1) e força da borda.
V vs(uint id : SV_VertexID, uint instance : SV_InstanceID) {
    Point p = points[instance / 2 * pointsPerStrip + id / 2];
    V o;
    o.part = instance & 1;
    o.offset = o.part ? ((id & 1) ? 16 : -16) : ((id & 1) ? p.look.x * 1.1 : 0);
    o.look = p.look;
    o.logoEdge = p.logoEdge.xy;
    float m = o.offset;
    float2 xy = p.placement.xy + p.placement.zw * m;
    o.pos = float4(xy / float2(size.x, -size.y) * 2 + float2(-1, 1), 0, 1);
    return o;
}

// Borda: a linha brilhante (na alga um fio fino quase sem halo, como na imagem; no logo com um halo prateado) e, do lado
// de fora, uma sombrinha que separa as camadas (não no logo).
float4 Edge(float v, float w, float shine, float tip, float logoAmount, float strength) {
    float3 light = float3(0.93, 0.95, 1.0);
    float halo = lerp(0.35, 1, logoAmount);
    float3 color = light * strength * (exp(-v * v / 1.1) * 0.9 + (exp(-v * v / 20) * 0.2 + exp(-v * v / 200) * 0.05) * halo) *
                   (1 + 2.5 * shine) * tip;
    float alpha = v * sign(w) < 0 ? exp(-v * v / 60) * 0.55 * (1 - logoAmount) : 0;
    return float4(color, alpha);
}

// Superfície: o tecido some do meio para fora e é mais claro junto da borda; no logo fica cheio até o meio (as metades
// opostas se encontram), opaco, com o degradê claro em cima à esquerda e quase preto embaixo à direita (a cor vem da
// posição, então sobrepor não aparece).
float4 Surface(float v, float w, float tone, float logoAmount, float2 pixel) {
    float3 light = float3(0.93, 0.95, 1.0);
    float t = saturate(v / (sign(w) * max(abs(w), 1)));
    float surface = lerp(t < 1 ? pow(1 - t, 1.1) * smoothstep(0, 0.04, t + 0.02) : 0, smoothstep(1.08, 0.96, t), logoAmount);
    float2 c = pixel - logo.xy;
    float gradient = lerp(0.72, 0.02, pow(saturate((c.x + c.y) / (4 * logo.z) + 0.5), 0.85));
    float alpha = lerp(saturate(surface * 1.8) * 0.93, surface, logoAmount);
    float3 color = lerp(tone * (0.35 + 0.65 * saturate(1 - t * 1.2)), gradient, logoAmount) * light;
    return float4(color, alpha);
}

// Área escura suave atrás do bloco do canto (a mesma do tema golden).
float CornerShade(float2 pixel) {
    float2 e = abs(pixel - (block.xy + block.zw) / 2) - (block.zw - block.xy) / 2 - 24 + 40;
    return 1 - 0.75 * smoothstep(110, -20, length(max(e, 0)) + min(max(e.x, e.y), 0) - 40);
}

// Nas pontas, dois pedaços que se cruzam em degradê somam de volta a cobertura inteira. Saída pré-multiplicada ("por
// cima").
float4 ps(V i) : SV_Target {
    float4 result;
    if (i.part) result = Edge(i.offset, i.look.x, i.look.w, i.look.z, i.logoEdge.x, i.logoEdge.y);
    else result = Surface(i.offset, i.look.x, i.look.y, i.logoEdge.x, i.pos.xy);

    float alpha = 1 - pow(1 - result.a, i.look.z);
    float3 color = i.part ? result.rgb : result.rgb * alpha;
    return float4(color * CornerShade(i.pos.xy), alpha);
}
)";

constexpr int kFloatsPerPoint = 12;
constexpr float kPi = 3.14159265f;
constexpr float kTipAlgae = 0.09f;
constexpr float kTipLogo = 0.074f;
constexpr float kWaveAlgae = 1;
constexpr float kWaveLogo = 0.12f;

struct StripScratch {
    std::vector<float> x, y, side, tone, width, tip, wave, length, strength, logo;

    explicit StripScratch(int n) : x(n), y(n), side(n), tone(n), width(n), tip(n), wave(n), length(n), strength(n), logo(n) {}
};

// Smoothstep de a até b.
static float Smooth(float a, float b, float v) {
    v = std::clamp((v - a) / (b - a), 0.f, 1.f);
    return v * v * (3 - 2 * v);
}

// Lê assets\fundo-morph.bin ('FORM' versão 2, gerado pelos scripts do morph): para cada uma das faixas, as duas formas
// (alga e logo) com [x, y, tom, força da borda] por ponto em alturas de tela, mais [lado, largura] da faixa em cada
// forma. As bordas prateadas vêm da alga e as arestas dos quatro quadrados do logo, já pareadas faixa a faixa.
bool MorphTheme::ReadShapes(std::wstring const& path) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") || !file) return false;

    int header[5] = {};
    bool ok = fread(header, 4, 5, file) == 5 && header[0] == 0x4D524F46 && header[1] == 2 && header[4] == 2 && header[2] > 0 && header[3] > 2;
    if (ok) {
        strips = header[2];
        pointsPerStrip = header[3];
        for (int s = 0; s < 2; s++) {
            shapes[s].resize(strips * pointsPerStrip * 4);
            stripProps[s].resize(strips * 2);
        }
        for (int i = 0; i < strips && ok; i++)
            for (int s = 0; s < 2 && ok; s++)
                ok = fread(&shapes[s][i * pointsPerStrip * 4], 16, pointsPerStrip, file) == size_t(pointsPerStrip) &&
                     fread(&stripProps[s][i * 2], 4, 2, file) == 2;
    }

    fclose(file);
    return ok;
}

// Shaders, o buffer dos pontos (atualizado a cada quadro), as constantes, a mistura pré-multiplicada e o rasterizador
// sem descarte de faces.
bool MorphTheme::CreatePipeline(ID3D11Device* device) {
    ComPtr<ID3DBlob> vertexCode, pixelCode;
    D3DCompile(kShader, sizeof kShader - 1, nullptr, nullptr, nullptr, "vs", "vs_5_0", 0, 0, &vertexCode, nullptr);
    D3DCompile(kShader, sizeof kShader - 1, nullptr, nullptr, nullptr, "ps", "ps_5_0", 0, 0, &pixelCode, nullptr);
    if (!vertexCode || !pixelCode) return false;

    device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vertexShader);
    device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pixelShader);

    UINT count = UINT(strips * pointsPerStrip);
    D3D11_BUFFER_DESC bufferDesc{count * 48, D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE, D3D11_CPU_ACCESS_WRITE,
                                 D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 48};
    device->CreateBuffer(&bufferDesc, nullptr, &pointBuffer);
    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{DXGI_FORMAT_UNKNOWN, D3D11_SRV_DIMENSION_BUFFER};
    viewDesc.Buffer.NumElements = count;
    device->CreateShaderResourceView(pointBuffer.Get(), &viewDesc, &pointView);

    D3D11_BUFFER_DESC constantsDesc{48, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    device->CreateBuffer(&constantsDesc, nullptr, &constants);

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0] = {TRUE, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA,
                                 D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
    device->CreateBlendState(&blendDesc, &blend);

    D3D11_RASTERIZER_DESC rasterizerDesc{D3D11_FILL_SOLID, D3D11_CULL_NONE};
    rasterizerDesc.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&rasterizerDesc, &rasterizer);
    points.resize(count * kFloatsPerPoint);
    return vertexShader && pixelShader && pointView;
}

// Tema "loop": as mesmas faixas de tecido da alga mudam de forma, ponto a ponto, até virar o logo do Windows e voltam
// (28 s sem emenda), tudo em vetor. Carregado só quando o tema é escolhido.
bool MorphTheme::Load(ID3D11Device* device, std::wstring const& path) {
    return ReadShapes(path) && CreatePipeline(device);
}

// Atraso de cada faixa pela posição dela no destino (da esquerda para a direita): elas saem uma depois da outra, na
// ordem em que chegam, então a forma nova vai se montando enquanto a velha se desfaz.
std::vector<float> MorphTheme::StripDelays(int to) const {
    std::vector<float> center(strips), delays(strips);
    float lowest = 1e9f, highest = -1e9f;

    for (int i = 0; i < strips; i++) {
        float sum = 0;
        for (int j = 0; j < pointsPerStrip; j++)
            sum += shapes[to][(i * pointsPerStrip + j) * 4] + 0.3f * shapes[to][(i * pointsPerStrip + j) * 4 + 1];
        center[i] = sum / pointsPerStrip;
        lowest = std::min(lowest, center[i]);
        highest = std::max(highest, center[i]);
    }

    for (int i = 0; i < strips; i++) delays[i] = 3.4f * (center[i] - lowest) / std::max(highest - lowest, 1e-3f);
    return delays;
}

// Posição, tom, força da borda, lado, largura, ponta e ondulação de cada ponto da faixa no instante `time`. A mudança
// corre ao longo da faixa (smootherstep), com um desvio em arco no meio do caminho (o tecido "escorre"). A faixa viaja
// fina e só abre no preenchimento do quadrado quando chega (e fecha logo que sai); nas pontas, na alga a faixa some aos
// poucos como uma folha, no logo os pedaços passam um pouco um para dentro do outro. O logo ondula bem pouco.
void MorphTheme::Interpolate(int strip, float time, int from, int to, float start, float delay, StripScratch& s) const {
    const float* propsFrom = &stripProps[0][strip * 2];
    const float* propsTo = &stripProps[1][strip * 2];

    for (int j = 0; j < pointsPerStrip; j++) {
        float along = float(j) / (pointsPerStrip - 1);
        float u = from == to ? 0 : std::clamp((time - start - delay - 0.8f * along) / 2.8f, 0.f, 1.f);
        float e = u * u * u * (u * (u * 6 - 15) + 10);
        const float* a = &shapes[from][(strip * pointsPerStrip + j) * 4];
        const float* b = &shapes[to][(strip * pointsPerStrip + j) * 4];

        float bx = a[0] + (b[0] - a[0]) * e, by = a[1] + (b[1] - a[1]) * e;
        float arc = sinf(kPi * e) * 0.05f;
        s.x[j] = bx + arc * sinf(by * 3.1f + strip * 0.7f + from * 2.3f);
        s.y[j] = by + arc * cosf(bx * 2.7f - strip * 0.5f + from * 1.1f);
        s.tone[j] = a[2] + (b[2] - a[2]) * e;
        s.strength[j] = a[3] + (b[3] - a[3]) * e;

        s.logo[j] = from == 1 ? 1 - Smooth(0, 0.4f, e) : to == 1 ? Smooth(0.6f, 1, e) : 0;
        float travel = from == 1 ? 1 - e : to == 1 ? e : 0;
        s.side[j] = propsFrom[0] + (propsTo[0] - propsFrom[0]) * travel;
        s.width[j] = propsFrom[1] + (propsTo[1] - propsFrom[1]) * s.logo[j];
        s.tip[j] = std::min(1.f, std::min(along, 1 - along) / (kTipAlgae + (kTipLogo - kTipAlgae) * s.logo[j]));
        s.wave[j] = kWaveAlgae + (kWaveLogo - kWaveAlgae) * s.logo[j];
    }
}

// Ondulação por cima de tudo, sempre: um campo de deslocamento no espaço (não ao longo de cada faixa), então pedaços que
// se emendam ondulam juntos, qualquer que seja o sentido deles. Frequências inteiras no loop: fecha sem emenda.
void MorphTheme::Ripple(int strip, float time, StripScratch const& s) {
    const float w = 2 * kPi / kLoopSeconds;

    for (int j = 0; j < pointsPerStrip; j++) {
        float o = s.wave[j], x = s.x[j], y = s.y[j];
        float* p = &points[(strip * pointsPerStrip + j) * kFloatsPerPoint];
        p[0] = x + o * (0.0055f * sinf(x * 5.3f + y * 3.1f - 9 * w * time) + 0.003f * sinf(-x * 2.9f + y * 7.7f + 6 * w * time));
        p[1] = y + o * (0.0055f * sinf(-x * 3.7f + y * 4.9f + 8 * w * time) + 0.003f * sinf(x * 6.1f + y * 2.3f - 5 * w * time));
    }
}

// Normal e largura final de cada ponto. Faixa solta (alga): a largura cai até 0 nas pontas, pelo comprimento até a
// ponta mais perto. Curva fechada para o lado da superfície: a largura não passa do raio, senão a fita se dobra (no logo
// não precisa: a cor vem da posição e dobrar não aparece).
void MorphTheme::ShapeStrip(int strip, StripScratch& s) {
    auto X = [&](int j) { return points[(strip * pointsPerStrip + j) * kFloatsPerPoint]; };
    auto Y = [&](int j) { return points[(strip * pointsPerStrip + j) * kFloatsPerPoint + 1]; };
    int last = pointsPerStrip - 1;

    s.length[0] = 0;
    for (int j = 1; j < pointsPerStrip; j++) s.length[j] = s.length[j - 1] + hypotf(X(j) - X(j - 1), Y(j) - Y(j - 1));

    for (int j = 0; j < pointsPerStrip; j++) {
        float* p = &points[(strip * pointsPerStrip + j) * kFloatsPerPoint];
        int a = std::max(j - 1, 0), b = std::min(j + 1, last);
        float tx = X(b) - X(a), ty = Y(b) - Y(a), n = std::max(hypotf(tx, ty), 1e-6f);
        float W = s.width[j] * s.side[j];

        float end = std::min(1.f, std::min(s.length[j], s.length[last] - s.length[j]) / std::max(1.3f * fabsf(W), 1e-4f));
        W *= 1 - (1 - s.logo[j]) * (1 - sqrtf(end));
        if (j > 0 && j < last) {
            float ax = p[0] - X(a), ay = p[1] - Y(a), cx = X(b) - p[0], cy = Y(b) - p[1];
            float k = 2 * (ax * cy - ay * cx) / std::max(hypotf(ax, ay) * hypotf(cx, cy) * n, 1e-9f);
            if (k * W > 0) W += (std::copysign(std::min(fabsf(W), 0.8f / fabsf(k)), W) - W) * (1 - s.logo[j]);
        }

        p[2] = -ty / n;
        p[3] = tx / n;
        p[4] = W;
        p[5] = s.tone[j];
        p[6] = s.tip[j];
        p[8] = s.logo[j];
        p[9] = s.strength[j];
    }
}

// Suaviza a largura ao longo da faixa (três passadas, no lugar).
void MorphTheme::SmoothWidths(int strip) {
    for (int pass = 0; pass < 3; pass++)
        for (int j = 1; j < pointsPerStrip - 1; j++) {
            float* w = &points[(strip * pointsPerStrip + j) * kFloatsPerPoint + 4];
            *w = (w[-kFloatsPerPoint] + 2 * w[0] + w[kFloatsPerPoint]) / 4;
        }
}

// Para pixels do monitor, centrado na largura dele, e o brilho que corre pelas bordas do logo pronto (11 a 17 s).
void MorphTheme::ToPixels(int strip, float time, float monitorWidth, float monitorHeight, float originX, float originY) {
    for (int j = 0; j < pointsPerStrip; j++) {
        float* p = &points[(strip * pointsPerStrip + j) * kFloatsPerPoint];
        float diagonal = ((p[0] - 8 / 9.f) + (p[1] - 0.5f)) / 0.76f + 0.5f;
        float sweep = (time - 11) / 6 * 1.6f - 0.3f;
        p[7] = time > 11 && time < 17 ? expf(-powf((diagonal - sweep) / 0.08f, 2)) * p[8] : 0;
        p[0] = originX + monitorWidth / 2 + (p[0] - 8 / 9.f) * monitorHeight;
        p[1] = originY + p[1] * monitorHeight;
        p[4] *= monitorHeight;
    }
}

// Roteiro (s): alga 0-4 | alga -> logo 4-11 | logo 11-17 (um brilho corre pelas bordas) | logo -> alga 17-24 |
// alga 24-28. Calcula os pontos de todas as faixas para um monitor (largura, altura e canto em pixels).
void MorphTheme::Update(float time, float monitorWidth, float monitorHeight, float originX, float originY) {
    const float starts[2] = {4, 17};
    int from = 0, to = 0;
    float start = 0;
    if (time >= starts[0] && time < starts[1]) from = 0, to = 1, start = starts[0];
    else if (time >= starts[1]) from = 1, to = 0, start = starts[1];

    std::vector<float> delays = StripDelays(to);
    StripScratch scratch(pointsPerStrip);
    for (int i = 0; i < strips; i++) {
        Interpolate(i, time, from, to, start, delays[i], scratch);
        Ripple(i, time, scratch);
        ShapeStrip(i, scratch);
        SmoothWidths(i);
        ToPixels(i, time, monitorWidth, monitorHeight, originX, originY);
    }

    logoX = originX + monitorWidth / 2;
    logoY = originY + monitorHeight / 2;
    logoHalfSide = 0.19f * monitorHeight;
}

// Desenha por cima do que já estiver no alvo (o fundo preto): 2 fitas por faixa, na ordem (a de trás primeiro).
void MorphTheme::Draw(ID3D11DeviceContext* context, float targetWidth, float targetHeight, const float block[4]) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context->Map(pointBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, points.data(), points.size() * 4);
    context->Unmap(pointBuffer.Get(), 0);

    struct {
        float block[4], size[2];
        UINT pointsPerStrip;
        float padding, logo[4];
    } values{{block[0], block[1], block[2], block[3]}, {targetWidth, targetHeight}, UINT(pointsPerStrip), 0, {logoX, logoY, logoHalfSide, 0}};
    context->UpdateSubresource(constants.Get(), 0, nullptr, &values, 0, 0);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(vertexShader.Get(), nullptr, 0);
    context->VSSetShaderResources(0, 1, pointView.GetAddressOf());
    context->PSSetShader(pixelShader.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
    context->OMSetBlendState(blend.Get(), nullptr, 0xFFFFFFFF);
    context->RSSetState(rasterizer.Get());
    context->DrawInstanced(2 * pointsPerStrip, 2 * strips, 0, 0);

    ID3D11ShaderResourceView* none = nullptr;
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    context->RSSetState(nullptr);
    context->VSSetShaderResources(0, 1, &none);
}
