#include "wallpaper/next_track.h"
#include "wallpaper/desktop.h"
#include "wallpaper/http.h"
#include "wallpaper/media.h"
#include "wallpaper/timing.h"
#include "common/paths.h"
#include "common/registry.h"

#include <dpapi.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shlwapi.lib")

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Data::Json;

constexpr double kQueueRefresh = 30;

struct AccessToken {
    std::string value;
    double expires = 0;
};

static std::mutex g_mutex;
static NextTrack g_next;

// Lado da capa da próxima música na tela, em pixels (26 DIPs).
int NextCoverSize() {
    return int(26 * GetDpiForSystem() / 96.f);
}

// Arquivo com o refresh token do Spotify, criptografado para o usuário do Windows (DPAPI); quem cria é
// tools\spotify-login.ps1.
static std::wstring TokenPath() {
    return ProjectPath(L"cache\\spotify-token.bin");
}

// Criptografa (ou abre) um texto com a chave do usuário do Windows; "" se falhar.
static std::string Dpapi(std::string const& data, bool protect) {
    DATA_BLOB in{DWORD(data.size()), (BYTE*)data.data()}, out{};
    BOOL ok = protect ? CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)
                      : CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out);
    if (!ok) return "";

    std::string result((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

// Refresh token guardado ("" sem login).
static std::string LoadRefreshToken() {
    std::ifstream file(TokenPath(), std::ios::binary);
    return file ? Dpapi(std::string(std::istreambuf_iterator<char>(file), {}), false) : "";
}

// Guarda o refresh token novo (no fluxo PKCE o Spotify troca o token a cada renovação).
static void SaveRefreshToken(std::string const& token) {
    std::string data = Dpapi(token, true);
    if (!data.empty()) std::ofstream(TokenPath(), std::ios::binary | std::ios::trunc).write(data.data(), data.size());
}

// Texto seguro para um formulário x-www-form-urlencoded.
static std::string FormEncode(std::string const& text) {
    std::string encoded;
    char hex[4];
    for (unsigned char c : text) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') encoded += char(c);
        else sprintf_s(hex, "%%%02X", c), encoded += hex;
    }
    return encoded;
}

// Renova o access token pelo refresh token (PKCE: só o Client ID, sem segredo) e guarda o refresh token novo.
static bool RenewToken(AccessToken& token) {
    std::string refresh = LoadRefreshToken(), clientId = winrt::to_string(SettingText(L"SpotifyClientId", L""));
    if (refresh.empty() || clientId.empty()) return false;

    std::string body = "grant_type=refresh_token&refresh_token=" + FormEncode(refresh) + "&client_id=" + FormEncode(clientId);
    std::string response = HttpsRequest(L"accounts.spotify.com", L"/api/token", L"POST", L"Content-Type: application/x-www-form-urlencoded",
                                        body);
    if (response.empty()) return false;

    auto json = JsonObject::Parse(winrt::to_hstring(response));
    token.value = winrt::to_string(json.GetNamedString(L"access_token"));
    token.expires = Now() + json.GetNamedNumber(L"expires_in", 3600) - 60;
    if (json.HasKey(L"refresh_token")) SaveRefreshToken(winrt::to_string(json.GetNamedString(L"refresh_token")));
    return true;
}

// Artistas separados por vírgula (música) ou o nome do programa (episódio de podcast).
static std::wstring ArtistsOf(JsonObject const& item) {
    if (!item.HasKey(L"artists")) return item.HasKey(L"show") ? std::wstring(item.GetNamedObject(L"show").GetNamedString(L"name")) : L"";

    std::wstring artists;
    for (auto artist : item.GetNamedArray(L"artists")) {
        if (!artists.empty()) artists += L", ";
        artists += artist.GetObject().GetNamedString(L"name");
    }
    return artists;
}

// URL da menor capa com pelo menos 48 px (o Spotify manda 640, 300 e 64).
static std::wstring SmallCoverUrl(JsonObject const& item) {
    auto owner = item.HasKey(L"album") ? item.GetNamedObject(L"album") : item;
    if (!owner.HasKey(L"images")) return L"";

    std::wstring url;
    double best = 1e9;
    for (auto image : owner.GetNamedArray(L"images")) {
        auto object = image.GetObject();
        double width = object.GetNamedNumber(L"width", 0);
        if (width >= 48 && width < best) best = width, url = object.GetNamedString(L"url");
    }
    return url;
}

// Baixa a capa (https://i.scdn.co/image/...) no tamanho da linha da próxima música.
static std::shared_ptr<std::vector<BYTE>> DownloadCover(std::wstring const& url) {
    size_t hostStart = url.find(L"://"), pathStart = hostStart == std::wstring::npos ? hostStart : url.find(L'/', hostStart + 3);
    if (pathStart == std::wstring::npos) return nullptr;

    std::wstring host = url.substr(hostStart + 3, pathStart - hostStart - 3);
    std::string bytes = HttpsRequest(host.c_str(), url.substr(pathStart));
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream((const BYTE*)bytes.data(), UINT(bytes.size())));
    return bytes.empty() ? nullptr : DecodeImage(stream.Get(), NextCoverSize());
}

// Lê a fila (GET /me/player/queue) e devolve a primeira música dela, com a música atual que o Spotify informou.
static NextTrack FetchNext(AccessToken& token) {
    if (Now() >= token.expires && !RenewToken(token)) return {};

    std::wstring authorization = L"Authorization: Bearer " + std::wstring(winrt::to_hstring(token.value));
    std::string response = HttpsRequest(L"api.spotify.com", L"/v1/me/player/queue", L"GET", authorization);
    if (response.empty()) {
        token.expires = 0;
        return {};
    }

    auto json = JsonObject::Parse(winrt::to_hstring(response));
    auto queue = json.GetNamedArray(L"queue");
    if (queue.Size() == 0 || json.GetNamedValue(L"currently_playing").ValueType() != JsonValueType::Object) return {};

    auto item = queue.GetObjectAt(0);
    NextTrack next;
    next.after = json.GetNamedObject(L"currently_playing").GetNamedString(L"name");
    next.title = item.GetNamedString(L"name");
    next.artist = ArtistsOf(item);
    next.cover = DownloadCover(SmallCoverUrl(item));
    return next;
}

// Thread da fila: só com login feito e o fundo visível. Lê de novo 1,5 s depois de cada troca de música (o Spotify
// atualiza a fila um pouco depois) e a cada 30 s tocando, para pegar música adicionada na fila.
static void NextTrackLoop() {
    winrt::init_apartment();
    AccessToken token;
    std::wstring fetchedFor;
    double lastFetch = -kQueueRefresh;

    for (;; Sleep(1000)) {
        NowPlaying song = CurrentMedia();
        bool due = song.title != fetchedFor || (song.playing && Now() - lastFetch > kQueueRefresh);
        if (WallpaperHidden() || song.title.empty() || !due || GetFileAttributesW(TokenPath().c_str()) == INVALID_FILE_ATTRIBUTES) continue;

        if (song.title != fetchedFor) Sleep(1500);
        fetchedFor = song.title;
        lastFetch = Now();

        NextTrack next;
        try { next = FetchNext(token); } catch (...) {}
        std::lock_guard lock(g_mutex);
        g_next = next;
    }
}

// Começa a acompanhar a fila do Spotify numa thread à parte.
void StartNextTrackService() {
    std::thread(NextTrackLoop).detach();
}

// Última próxima música lida (vazia sem login ou sem fila).
NextTrack CurrentNextTrack() {
    std::lock_guard lock(g_mutex);
    return g_next;
}
