#include <winsock2.h>
#include <ws2tcpip.h>

#include "wallpaper/light_switch.h"
#include "wallpaper/desktop.h"
#include "common/paths.h"

#include <bcrypt.h>
#include <dpapi.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <atomic>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

using namespace winrt::Windows::Data::Json;

constexpr int kPollMs = 5000;
constexpr int kTimeoutMs = 2000;
constexpr uint32_t kControl = 0x07;
constexpr uint32_t kQuery = 0x0A;

struct SwitchConfig {
    std::string id;
    std::string key;
    std::string ip;
};

static std::atomic<int> g_state{-1};
static std::atomic<int> g_target{-1};
static HANDLE g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);

// Configuração do interruptor (id, chave local e IP), criptografada para o usuário do Windows em
// cache\tuya-quarto.bin pelo tools\tuya-setup.ps1. Sem ela, o recurso fica desligado.
static bool LoadConfig(SwitchConfig& config) {
    std::ifstream file(ProjectPath(L"cache\\tuya-quarto.bin"), std::ios::binary);
    std::string data(std::istreambuf_iterator<char>(file), {});
    DATA_BLOB in{DWORD(data.size()), (BYTE*)data.data()}, out{};
    if (data.empty() || !CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return false;

    std::string text((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    try {
        auto json = JsonObject::Parse(winrt::to_hstring(text));
        config = {winrt::to_string(json.GetNamedString(L"id")), winrt::to_string(json.GetNamedString(L"key")),
                  winrt::to_string(json.GetNamedString(L"ip"))};
    } catch (...) {
        return false;
    }
    return config.key.size() == 16;
}

// AES-128-ECB com padding PKCS7 (o que o protocolo local da Tuya 3.3 usa), pela criptografia do Windows.
static std::string Aes(std::string const& key, std::string const& data, bool encrypt) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE handle = nullptr;
    std::string out;
    auto run = encrypt ? BCryptEncrypt : BCryptDecrypt;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0 &&
        BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0) == 0 &&
        BCryptGenerateSymmetricKey(algorithm, &handle, nullptr, 0, (PUCHAR)key.data(), ULONG(key.size()), 0) == 0) {
        ULONG size = 0;
        run(handle, (PUCHAR)data.data(), ULONG(data.size()), nullptr, nullptr, 0, nullptr, 0, &size, BCRYPT_BLOCK_PADDING);
        out.resize(size);
        if (run(handle, (PUCHAR)data.data(), ULONG(data.size()), nullptr, nullptr, 0, (PUCHAR)out.data(), size, &size, BCRYPT_BLOCK_PADDING))
            out.clear();
        out.resize(out.empty() ? 0 : size);
    }
    if (handle) BCryptDestroyKey(handle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return out;
}

// CRC32 (o mesmo do zip) do pacote.
static uint32_t Crc32(std::string const& data) {
    uint32_t crc = 0xFFFFFFFF;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; i++) crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
    }
    return ~crc;
}

// Inteiro de 32 bits big-endian no fim do texto.
static void Append32(std::string& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out += char(value >> shift & 0xFF);
}

// Lê um inteiro de 32 bits big-endian.
static uint32_t Read32(std::string const& data, size_t at) {
    return uint32_t(BYTE(data[at])) << 24 | uint32_t(BYTE(data[at + 1])) << 16 | uint32_t(BYTE(data[at + 2])) << 8 | BYTE(data[at + 3]);
}

// Pacote do protocolo 3.3: 55AA, sequência, comando, tamanho, carga cifrada (o comando de controle leva "3.3" + 12
// zeros na frente), CRC32 e AA55.
static std::string Packet(SwitchConfig const& config, uint32_t command, std::string const& json) {
    std::string payload = Aes(config.key, json, true);
    if (command == kControl) payload = std::string("3.3") + std::string(12, '\0') + payload;

    std::string packet;
    Append32(packet, 0x000055AA);
    Append32(packet, 1);
    Append32(packet, command);
    Append32(packet, uint32_t(payload.size() + 8));
    packet += payload;
    Append32(packet, Crc32(packet));
    Append32(packet, 0x0000AA55);
    return packet;
}

// Conecta no interruptor (porta 6668) com tempo-limite de 2 s para conectar, enviar e receber.
static SOCKET Connect(std::string const& ip) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{AF_INET, htons(6668)};
    inet_pton(AF_INET, ip.c_str(), &address.sin_addr);

    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    connect(s, (sockaddr*)&address, sizeof address);
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(s, &writable);
    timeval wait{0, kTimeoutMs * 1000};
    if (select(0, nullptr, &writable, nullptr, &wait) != 1) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    nonBlocking = 0;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    DWORD timeout = kTimeoutMs;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof timeout);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof timeout);
    return s;
}

// Manda um comando e devolve o JSON decifrado da resposta ("" se não respondeu ou veio sem carga).
static std::string Exchange(SwitchConfig const& config, uint32_t command, std::string const& json) {
    SOCKET s = Connect(config.ip);
    if (s == INVALID_SOCKET) return "";

    std::string packet = Packet(config, command, json), response(4096, '\0');
    int received = send(s, packet.data(), int(packet.size()), 0) == int(packet.size()) ? recv(s, response.data(), int(response.size()), 0) : 0;
    closesocket(s);
    if (received < 28 || Read32(response, 0) != 0x000055AA) return "";

    size_t length = Read32(response, 12);
    if (16 + length > size_t(received) || length < 12) return "";
    std::string payload = response.substr(20, length - 12);
    if (payload.size() >= 15 && payload.compare(0, 2, "3.") == 0) payload.erase(0, 15);
    return payload.empty() ? "" : Aes(config.key, payload, false);
}

// Estado do DP 1 (o liga/desliga) numa resposta: 1 ligado, 0 desligado, -1 se não veio.
static int SwitchFrom(std::string const& json) {
    try {
        auto dps = JsonObject::Parse(winrt::to_hstring(json)).GetNamedObject(L"dps");
        return dps.HasKey(L"1") ? int(dps.GetNamedBoolean(L"1")) : -1;
    } catch (...) {
        return -1;
    }
}

// Pede o estado atual (comando de consulta).
static int QuerySwitch(SwitchConfig const& config) {
    std::string t = std::to_string(time(nullptr));
    return SwitchFrom(Exchange(config, kQuery,
                               "{\"gwId\":\"" + config.id + "\",\"devId\":\"" + config.id + "\",\"uid\":\"" + config.id + "\",\"t\":\"" + t + "\"}"));
}

// Liga ou desliga (comando de controle no DP 1).
static void SetSwitch(SwitchConfig const& config, bool on) {
    std::string t = std::to_string(time(nullptr));
    Exchange(config, kControl,
             "{\"devId\":\"" + config.id + "\",\"uid\":\"\",\"t\":\"" + t + "\",\"dps\":{\"1\":" + (on ? "true" : "false") + "}}");
}

// Thread do interruptor: com o fundo visível, consulta o estado a cada 5 s; um clique acorda a thread na hora, manda
// o estado pedido e consulta de novo. Sem configuração, não faz nada.
static void LightLoop() {
    winrt::init_apartment();
    WSADATA wsa;
    SwitchConfig config;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) || !LoadConfig(config)) return;

    for (;;) {
        if (int target = g_target.exchange(-1); target >= 0) {
            SetSwitch(config, target == 1);
            Sleep(300);
        }
        if (!WallpaperHidden()) g_state = QuerySwitch(config);
        WaitForSingleObject(g_wake, kPollMs);
    }
}

// Começa a acompanhar o interruptor numa thread à parte.
void StartLightService() {
    std::thread(LightLoop).detach();
}

// Estado da luz: 1 ligada, 0 desligada, -1 desconhecido (sem configuração ou sem resposta).
int LightState() {
    return g_state;
}

// Inverte a luz (o estado na tela já troca; a thread confirma pela rede logo depois).
void ToggleLight() {
    int state = g_state;
    if (state < 0) return;

    g_target = 1 - state;
    g_state = 1 - state;
    SetEvent(g_wake);
}
