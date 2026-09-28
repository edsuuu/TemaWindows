#include "wallpaper/weather.h"
#include "wallpaper/desktop.h"
#include "wallpaper/http.h"
#include "wallpaper/timing.h"
#include "common/paths.h"
#include "common/registry.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

constexpr ULONGLONG kRefreshMs = 30 * 60000;
constexpr ULONGLONG kRetryMs = 5 * 60000;

static std::mutex g_mutex;
static std::string g_json;
static std::atomic<int> g_version{0};

// Publica uma resposta nova para o painel.
static void Publish(std::string json) {
    std::lock_guard lock(g_mutex);
    g_json = std::move(json);
    g_version++;
}

// Lê o cache (aparece já ao abrir) e devolve quando baixar de novo: ainda novo, só quando vencer os 30 min.
static ULONGLONG LoadCache(std::wstring const& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    FILE* file;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) || _wfopen_s(&file, path.c_str(), L"rb")) return 0;

    std::string json(attributes.nFileSizeLow, 0);
    json.resize(fread(json.data(), 1, json.size(), file));
    fclose(file);

    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULONGLONG ageMs = (ToU64(now) - ToU64(attributes.ftLastWriteTime)) / 10000;
    Publish(std::move(json));
    return GetTickCount64() + (ageMs < kRefreshMs ? kRefreshMs - ageMs : 0);
}

// Thread do clima (Open-Meteo): baixa a cada 30 min, só com o fundo visível (com jogo, tela cheia ou bloqueio, nada de
// rede), e guarda a resposta em cache\cache-clima.txt; sem internet (ou resposta estranha) tenta de novo em 5 min e
// fica o último valor. O lugar vem do registro (ClimaLatitude e ClimaLongitude, texto como "-23.55"), para não ficar
// no código; sem ele, não há clima. O fuso é o do lugar (timezone=auto).
static void WeatherLoop() {
    std::wstring cache = ProjectPath(L"cache\\cache-clima.txt");
    std::wstring latitude = SettingText(L"ClimaLatitude", L""), longitude = SettingText(L"ClimaLongitude", L"");
    if (latitude.empty() || longitude.empty()) return;

    std::wstring path = L"/v1/forecast?latitude=" + latitude + L"&longitude=" + longitude +
                        L"&current=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min&timezone=auto";
    ULONGLONG next = LoadCache(cache);

    for (;; Sleep(10000)) {
        if (WallpaperHidden() || GetTickCount64() < next) continue;

        std::string json = HttpsRequest(L"api.open-meteo.com", path);
        if (json.find("\"current\":{") == std::string::npos) {
            next = GetTickCount64() + kRetryMs;
            continue;
        }

        next = GetTickCount64() + kRefreshMs;
        FILE* file;
        if (!_wfopen_s(&file, cache.c_str(), L"wb")) {
            fwrite(json.data(), 1, json.size(), file);
            fclose(file);
        }
        Publish(std::move(json));
    }
}

// Começa o clima numa thread à parte (só na primeira chamada).
void StartWeatherService() {
    static bool started;
    if (started) return;

    started = true;
    std::thread(WeatherLoop).detach();
}

// Muda sempre que chega uma resposta nova.
int WeatherVersion() {
    return g_version;
}

// Última resposta boa do Open-Meteo (JSON).
std::string WeatherJson() {
    std::lock_guard lock(g_mutex);
    return g_json;
}

// n-ésimo número depois de `key`, procurando a partir de `from`: "temperature_2m":19.5 ou "temperature_2m_max":[30.8,...].
static double NumberAfter(std::string const& json, const char* key, size_t from, int n = 0) {
    size_t p = from == std::string::npos ? from : json.find(key, from);
    if (p == std::string::npos) return NAN;

    for (p += strlen(key); n-- > 0; p++)
        if ((p = json.find(',', p)) == std::string::npos) return NAN;
    return atof(json.c_str() + p);
}

// Tempo agora (temperatura, código WMO, dia/noite) e máx/mín de hoje: a posição de hoje na lista de dias, porque o
// cache pode ser de ontem.
Weather ParseWeather(std::string const& json, SYSTEMTIME const& today) {
    Weather weather;
    size_t current = json.find("\"current\":{"), daily = json.find("\"daily\":{");
    weather.temperature = NumberAfter(json, "\"temperature_2m\":", current);
    weather.code = int(NumberAfter(json, "\"weather_code\":", current));
    weather.day = NumberAfter(json, "\"is_day\":", current) != 0;

    char date[16];
    sprintf_s(date, "\"%04d-%02d-%02d\"", today.wYear, today.wMonth, today.wDay);
    size_t days = daily == std::string::npos ? daily : json.find("\"time\":[", daily);
    size_t position = days == std::string::npos ? days : json.find(date, days);
    int index = position == std::string::npos ? -1 : int(std::count(json.begin() + days, json.begin() + position, ','));

    weather.maximum = index < 0 ? NAN : NumberAfter(json, "\"temperature_2m_max\":[", daily, index);
    weather.minimum = index < 0 ? NAN : NumberAfter(json, "\"temperature_2m_min\":[", daily, index);
    return weather;
}

// Código WMO do Open-Meteo em texto.
const wchar_t* SkyText(int code) {
    if (code == 0) return L"Céu limpo";
    if (code == 1) return L"Poucas nuvens";
    if (code == 2) return L"Parcialmente nublado";
    if (code == 3) return L"Nublado";
    if (code < 50) return L"Neblina";
    if (code < 60) return L"Garoa";
    if (code == 61) return L"Chuva fraca";
    if (code == 65) return L"Chuva forte";
    if (code < 70) return L"Chuva";
    if (code < 80) return L"Neve";
    if (code < 83) return L"Pancadas de chuva";
    if (code < 90) return L"Neve";
    if (code == 95) return L"Trovoada";
    return L"Trovoada com granizo";
}
