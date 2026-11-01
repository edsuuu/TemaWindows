#include "wallpaper/audio.h"
#include "wallpaper/corner_widgets.h"
#include "wallpaper/http.h"
#include "wallpaper/info_panel.h"
#include "wallpaper/palette.h"
#include "wallpaper/pc_specs.h"
#include "wallpaper/timing.h"
#include "wallpaper/weather.h"
#include "wallpaper/widget_options.h"
#include "common/paths.h"
#include "common/registry.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

constexpr int kInkCount = int(Ink::Count);
constexpr int kSave = 100;
constexpr int kRestore = 101;
constexpr int kCity = 102;
constexpr int kSearch = 103;
constexpr int kResults = 104;
constexpr int kFirstSection = 110;
constexpr int kFirstInk = 200;
constexpr int kFirstLogoInk = 210;
constexpr int kFirstPreset = 220;
constexpr int kGrid = 300;
constexpr int kSide = 400;
constexpr int kFirstPart = 500;
constexpr int kJson = 510;
constexpr int kFirstField = 520;
constexpr int kCelsius = 530;
constexpr int kFahrenheit = 531;
constexpr int kRange = 532;
constexpr int kSize = 540;
constexpr int kMarginStep = 543;
constexpr int kShadow = 546;
constexpr int kBlurRadius = 549;
constexpr int kVeil = 552;
constexpr int kWidth = 555;
constexpr int kFirstMonitor = 560;
constexpr int kFirstTheme = 570;
constexpr int kPickImage = 575;
constexpr int kFlow = 576;
constexpr int kPickVideo = 577;
constexpr int kPause = 578;
constexpr int kFolders = 579;
constexpr int kFirstBarMode = 580;
constexpr int kLogoStandard = 583;
constexpr int kLogoCustom = 584;
constexpr int kPlayer = 585;
constexpr int kFirstWave = 586;
constexpr int kStartup = 590;
constexpr int kHwinfo = 591;
constexpr int kWaveStrength = 592;
constexpr int kWaveWidth = 595;
constexpr int kMaxMonitors = 8;
constexpr UINT kSearchDone = WM_APP + 1;

constexpr float kGap = 20;
constexpr float kRow = 44;
constexpr float kSidebar = 168;
constexpr float kContent = kSidebar + kGap;
constexpr float kLabelWidth = 228;
constexpr float kControls = kContent + kLabelWidth + 8;
constexpr float kControlWidth = 204;
constexpr float kContentWidth = kControls + kControlWidth - kContent;
constexpr float kButtonHeight = 32;
constexpr float kPanelWidth = kControls + kControlWidth + kGap;
constexpr float kPreviewWidth = 480;
constexpr float kHeight = 680;
constexpr COLORREF kBackground = RGB(16, 16, 18);
constexpr COLORREF kField = RGB(32, 32, 36);
constexpr COLORREF kText = RGB(222, 222, 222);
constexpr COLORREF kSoftText = RGB(150, 150, 156);

const wchar_t* const kSections[] = {L"Aparência", L"Widgets", L"JSON", L"Layout", L"Clima", L"Fundo", L"Barra", L"Sistema"};
constexpr int kSectionCount = int(std::size(kSections));
const wchar_t* const kThemes[] = {L"nbhd", L"golden", L"loop", L"imagem", L"video"};
const DWORD kBarModes[] = {1, 0, 2};
const COLORREF kLogoStandardColors[3] = {RGB(255, 255, 255), RGB(128, 128, 128), RGB(0, 0, 0)};

struct Preset {
    const wchar_t* name;
    COLORREF colors[kInkCount];
};

// Paletas prontas (na ordem de kInks): a primeira é a padrão.
const Preset kPresets[] = {
    {L"Grafite", {RGB(0xF2, 0xF2, 0xF2), RGB(0xC7, 0xC7, 0xC7), RGB(0xF2, 0xF2, 0xF2), RGB(0xE6, 0xE6, 0xE6), RGB(0xF2, 0x8C, 0x8C),
                  RGB(0xA8, 0xA8, 0xA8), RGB(0xE6, 0xE6, 0xE6), RGB(0x75, 0x75, 0x75)}},
    {L"Gelo", {RGB(0xEE, 0xF4, 0xF8), RGB(0xB8, 0xC7, 0xD1), RGB(0x9C, 0xCB, 0xE8), RGB(0xCF, 0xE3, 0xF0), RGB(0x7F, 0xB8, 0xE0),
               RGB(0x8F, 0xA6, 0xB5), RGB(0xDC, 0xEA, 0xF3), RGB(0x5E, 0x6E, 0x78)}},
    {L"Âmbar", {RGB(0xF5, 0xEB, 0xDD), RGB(0xCD, 0xBF, 0xAE), RGB(0xE8, 0xB5, 0x72), RGB(0xEA, 0xD9, 0xC2), RGB(0xE8, 0x92, 0x5A),
                RGB(0xB0, 0x9A, 0x80), RGB(0xF0, 0xE2, 0xCE), RGB(0x7A, 0x6A, 0x58)}},
    {L"Menta", {RGB(0xEC, 0xF5, 0xF0), RGB(0xBC, 0xCF, 0xC4), RGB(0x8F, 0xD1, 0xB0), RGB(0xD3, 0xEB, 0xDD), RGB(0xE8, 0xA0, 0x8C),
                RGB(0x93, 0xAD, 0x9F), RGB(0xDD, 0xEF, 0xE5), RGB(0x62, 0x75, 0x69)}},
    {L"Lavanda", {RGB(0xF1, 0xEE, 0xF7), RGB(0xC6, 0xC0, 0xD4), RGB(0xB7, 0xA6, 0xE3), RGB(0xDC, 0xD5, 0xEE), RGB(0xE5, 0x9A, 0xB8),
                  RGB(0xA3, 0x9C, 0xB6), RGB(0xE7, 0xE1, 0xF3), RGB(0x6E, 0x68, 0x80)}},
};

struct Place {
    std::wstring name;
    std::wstring latitude;
    std::wstring longitude;
};

struct Stepper {
    int id;
    int* value;
    int low;
    int high;
    int step;
    const wchar_t* unit;
};

struct Label {
    std::wstring text;
    float top;
    bool heading;
};

struct Preview {
    ComPtr<ID3D11Device> device;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1DeviceContext> dc;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1StrokeStyle> round;
    ComPtr<ID2D1LinearGradientBrush> logo;
    std::unique_ptr<SystemStats> stats;
    NetworkMeter network;
    NowPlayingCard card;
    Rings rings;
    InfoPanel panel;
    Equalizer equalizer;
    CornerLayout layout{};
    COLORREF logoKey[3] = {};
    double lastFrame = 0;
    double lastReading = -10;
};

static HFONT font;
static HFONT titleFont;
static HBRUSH background;
static HBRUSH field;
static float scale = 1;
static COLORREF custom[16];
static D2D1_COLOR_F pending[kInkCount];
static WidgetOptions o;
static bool dirty = false;
static bool imageFlow = false;
static bool folders = true;
static bool startup = true;
static bool logoCustom = false;
static bool player = true;
static bool monitorShown[kMaxMonitors];
static COLORREF logo[3];
static int section = 0;
static int theme = 0;
static int barMode = 1;
static int wave = 0;
static int waveStrength = 70;
static int waveWidth = 200;
static int blurRadius = 9;
static int veilPercent = 12;
static DWORD veil = 31;  // BlurVeu como estava (0-255), para não mudar com o arredondamento do %
static std::wstring imageFile;
static std::wstring videoFile;
static std::vector<std::pair<std::wstring, std::wstring>> monitors;
static std::vector<Label> labels;
static WPARAM searches = 0;
static Place city;
static std::vector<Place> places;
static Preview preview;

const Stepper kSteppers[] = {
    {kSize, &o.size, 50, 200, 10, L"%"},   {kWidth, &o.width, 380, 800, 20, L" px"},     {kMarginStep, &o.margin, 0, 200, 4, L" px"},
    {kShadow, &o.shadow, 0, 100, 5, L"%"}, {kBlurRadius, &blurRadius, 0, 40, 1, L" px"}, {kVeil, &veilPercent, 0, 100, 5, L"%"},
    {kWaveStrength, &waveStrength, 30, 100, 10, L"%"}, {kWaveWidth, &waveWidth, 100, 400, 20, L" px"},
};

// DIPs -> pixels na escala do sistema.
static int Px(float dips) {
    return int(lroundf(dips * scale));
}

// Cor da paleta como COLORREF do GDI, e o contrário.
static COLORREF ToColorRef(D2D1_COLOR_F color) {
    return RGB(lroundf(color.r * 255), lroundf(color.g * 255), lroundf(color.b * 255));
}

static D2D1_COLOR_F ToColor(COLORREF color) {
    return {GetRValue(color) / 255.f, GetGValue(color) / 255.f, GetBValue(color) / 255.f, 1};
}

// "#RRGGBB" de uma cor, e o contrário (texto fora do formato: `fallback`).
static std::wstring Hex(COLORREF color) {
    wchar_t text[8];
    swprintf_s(text, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return text;
}

static COLORREF FromHex(std::wstring const& text, COLORREF fallback) {
    wchar_t* end = nullptr;
    unsigned long rgb = text.size() == 7 && text[0] == L'#' ? wcstoul(text.c_str() + 1, &end, 16) : 0;
    return end && !*end ? RGB(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255) : fallback;
}

// UTF-16 <-> UTF-8 (a API de cidades fala UTF-8).
static std::string ToUtf8(std::wstring const& text) {
    std::string utf8(WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr), 0);
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), utf8.data(), int(utf8.size()), nullptr, nullptr);
    return utf8;
}

static std::wstring FromUtf8(std::string const& text) {
    std::wstring wide(MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0), 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), wide.data(), int(wide.size()));
    return wide;
}

// Como cada valor estava no registro antes da primeira mudança desde o último Salvar (nullopt = não existia), para
// desfazer ao fechar sem salvar.
static std::map<std::wstring, std::optional<std::pair<DWORD, std::vector<BYTE>>>> unsaved;

static void Remember(const wchar_t* name) {
    if (unsaved.count(name)) return;

    DWORD type = 0, size = 0;
    auto& value = unsaved[name];
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", name, RRF_RT_ANY, &type, nullptr, &size) != ERROR_SUCCESS) return;
    std::vector<BYTE> data(size);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", name, RRF_RT_ANY, &type, data.data(), &size) == ERROR_SUCCESS)
        value = std::pair{type, std::move(data)};
}

// Grava um texto ou um número em HKCU\Software\TemaBarra, ou apaga o valor (lembrando o de antes).
static void WriteText(const wchar_t* name, std::wstring const& value) {
    Remember(name);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", name, REG_SZ, value.c_str(), DWORD((value.size() + 1) * sizeof(wchar_t)));
}

static void WriteNumber(const wchar_t* name, DWORD value) {
    Remember(name);
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", name, REG_DWORD, &value, sizeof value);
}

static void Forget(const wchar_t* name) {
    Remember(name);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", name);
}

// Fechou sem salvar: o registro volta a como estava no último Salvar (e o FundoVivo e a barra junto, em até um
// segundo; o clima da cidade antiga, em até 10 s).
// ponytail: fechar pelo Gerenciador de Tarefas com mudança pendente deixa ela valendo.
static void Revert() {
    for (auto const& [name, value] : unsaved) {
        if (value) RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", name.c_str(), value->first, value->second.data(), DWORD(value->second.size()));
        else RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\TemaBarra", name.c_str());
    }
    if (unsaved.count(L"ClimaLatitude")) DeleteFileW(ProjectPath(L"cache\\cache-clima.txt").c_str());
    unsaved.clear();
}

// O papel de parede ao lado deste app: Wallget.exe instalado, FundoVivo.exe no desenvolvimento.
static std::wstring WallpaperExe() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring folder(path, wcsrchr(path, L'\\') + 1);
    return GetFileAttributesW((folder + L"Wallget.exe").c_str()) != INVALID_FILE_ATTRIBUTES ? folder + L"Wallget.exe" : folder + L"FundoVivo.exe";
}

// Inicia com o Windows? (HKCU Run "Wallget")
static bool StartsWithWindows() {
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Wallget", RRF_RT_REG_SZ, nullptr, nullptr,
                        &size) == ERROR_SUCCESS;
}

// Põe no registro tudo o que está na tela, para o FundoVivo e a barra mostrarem na hora (em até um segundo; o clima
// novo, em até 10 s): cores diferentes do padrão como "#RRGGBB" (as iguais saem do registro), widgets, JSON, layout,
// clima, fundo e barra; a cidade só se mudou (apagando o cache do clima da antiga). Fica valendo só com o Salvar.
static void Apply() {
    for (int i = 0; i < kInkCount; i++) {
        COLORREF color = ToColorRef(pending[i]);
        if (color == ToColorRef(kInks[i].standard)) Forget(kInks[i].name);
        else WriteText(kInks[i].name, Hex(color));
    }

    for (size_t i = 0; i < std::size(kPartSwitches); i++) WriteNumber(kPartSwitches[i].name, o.parts[i]);
    for (int i = 0; i < FieldCount; i++) WriteNumber(kFieldSwitches[i].name, o.fields[i]);
    WriteNumber(L"Json", o.json);
    WriteNumber(L"ClimaUnidade", o.fahrenheit);
    WriteNumber(L"ClimaMaxMin", o.range);
    WriteNumber(L"FundoPausa", o.pause);
    WriteNumber(L"Posicao", o.position);
    WriteNumber(L"PosicaoClima", o.weatherSide);
    WriteNumber(L"Escala", o.size);
    WriteNumber(L"Largura", o.width);
    WriteNumber(L"Margem", o.margin);
    WriteNumber(L"Sombra", o.shadow);
    std::wstring hidden;
    for (size_t i = 0; i < monitors.size() && i < kMaxMonitors; i++)
        if (!monitorShown[i]) hidden += L"|" + monitors[i].first + L"|";
    if (hidden.empty()) Forget(L"MonitoresOcultos");
    else WriteText(L"MonitoresOcultos", hidden);

    if (city.latitude != SettingText(L"ClimaLatitude", L"") || city.longitude != SettingText(L"ClimaLongitude", L"")) {
        WriteText(L"ClimaLatitude", city.latitude);
        WriteText(L"ClimaLongitude", city.longitude);
        WriteText(L"ClimaCidade", city.name);
        DeleteFileW(ProjectPath(L"cache\\cache-clima.txt").c_str());
    }

    WriteText(L"FundoTema", kThemes[theme]);
    WriteText(L"FundoImagem", imageFile);
    WriteText(L"FundoVideo", videoFile);
    WriteNumber(L"FundoOndular", imageFlow);
    WriteNumber(L"Pastas", folders);

    WriteNumber(L"BlurModo", barMode);
    WriteNumber(L"BlurRaio", blurRadius);
    if (lroundf(veil / 2.55f) != veilPercent) veil = DWORD(lroundf(veilPercent * 2.55f));
    WriteNumber(L"BlurVeu", veil);
    const wchar_t* logoNames[3] = {L"LogoCor1", L"LogoCor2", L"LogoCor3"};
    for (int i = 0; i < 3; i++) {
        if (logoCustom) WriteText(logoNames[i], Hex(logo[i]));
        else Forget(logoNames[i]);
    }
    WriteNumber(L"Player", player);
    WriteNumber(L"PlayerOnda", wave);
    WriteNumber(L"PlayerOndaForca", waveStrength);
    WriteNumber(L"PlayerOndaLargura", waveWidth);
}

// Tela inteira redesenhada (botões, nomes e o aviso de não salvo).
static void Redraw(HWND window) {
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

// Mantém o que está valendo (e o início com o Windows, HKCU Run "Wallget"): fechar depois não desfaz mais.
static void Save(HWND window) {
    Apply();
    WriteNumber(L"IniciarComWindows", startup);
    if (startup) {
        std::wstring command = L"\"" + WallpaperExe() + L"\"";
        RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Wallget", REG_SZ, command.c_str(),
                        DWORD((command.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Wallget");
    }

    unsaved.clear();
    dirty = false;
    EnableWindow(GetDlgItem(window, kSave), FALSE);
    Redraw(window);
}

// Alguma coisa mudou na tela: já vale no fundo e na barra, libera o Salvar e redesenha.
static void Changed(HWND window) {
    Apply();
    dirty = true;
    EnableWindow(GetDlgItem(window, kSave), TRUE);
    Redraw(window);
}

// Só o nome do arquivo de um caminho.
static std::wstring FileName(std::wstring const& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// HWiNFO mandando os sensores (memória compartilhada aberta)? Com a temperatura da CPU, se a prévia já leu.
static std::wstring HwinfoStatus() {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Global\\HWiNFO_SENS_SM2");
    if (!mapping) return L"HWiNFO parado (sem temperatura da CPU)";
    CloseHandle(mapping);

    double temperature = preview.stats ? preview.stats->cpuTemperature : -1;
    wchar_t text[96];
    if (temperature >= 0) swprintf_s(text, L"HWiNFO conectado · CPU %.0f °C", temperature);
    else wcscpy_s(text, L"HWiNFO conectado");
    return text;
}

// Põe na tela só os controles da seção escolhida, linha por linha (alguns somem conforme outra opção: as cores e os
// campos do JSON com ele desligado, a imagem e o vídeo fora do tema deles, o raio do blur fora do modo blur, as cores
// do logo sem o personalizado), e guarda os nomes de cada linha para o PaintLabels.
static void Arrange(HWND window) {
    labels.clear();
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        int id = GetDlgCtrlID(child);
        bool fixed = (id >= kFirstSection && id < kFirstSection + kSectionCount) || id == kSave || id == kRestore || id == 0;
        if (!fixed) ShowWindow(child, SW_HIDE);
    }

    float y = kGap;
    auto place = [&](int id, float x, float top, float width, float height = kButtonHeight) {
        SetWindowPos(GetDlgItem(window, id), nullptr, Px(x), Px(top), Px(width), Px(height), SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    };
    auto label = [&](std::wstring text, bool heading = false) { labels.push_back({std::move(text), y, heading}); };
    auto row = [&](std::wstring text, int id) {
        label(std::move(text));
        place(id, kControls, y, kControlWidth);
        y += kRow;
    };
    auto choices = [&](std::wstring text, int first, int count) {
        label(std::move(text));
        float width = (kControlWidth - (count - 1) * 6) / count;
        for (int i = 0; i < count; i++) place(first + i, kControls + i * (width + 6), y, width);
        y += kRow;
    };
    auto heading = [&](std::wstring text) {
        label(std::move(text), true);
        y += 28;
    };
    auto wide = [&](int first, int count) {
        float width = (kContentWidth - (count - 1) * 6) / count;
        for (int i = 0; i < count; i++) place(first + i, kContent + i * (width + 6), y, width);
        y += kRow;
    };
    auto stepper = [&](std::wstring text, int first) {
        label(std::move(text));
        place(first, kControls, y, 40);
        place(first + 1, kControls + 46, y, kControlWidth - 92);
        place(first + 2, kControls + kControlWidth - 40, y, 40);
        y += kRow;
    };
    auto ink = [&](int id, std::wstring text) { row(std::move(text), id); };

    switch (section) {
    case 0:
        heading(L"Temas de cores");
        wide(kFirstPreset, int(std::size(kPresets)));
        for (int i = 0; i < 5; i++) ink(kFirstInk + i, kInks[i].label);
        break;
    case 1:
        for (size_t i = 0; i < std::size(kPartSwitches); i++) row(kPartSwitches[i].label, kFirstPart + int(i));
        break;
    case 2:
        row(L"Mostrar o bloco JSON", kJson);
        if (!o.json) break;
        for (int i = 5; i < kInkCount; i++) ink(kFirstInk + i, kInks[i].label);
        heading(L"Campos do JSON");
        wide(kFirstField, 4);
        wide(kFirstField + 4, FieldCount - 4);
        break;
    case 3:
        label(L"Posição na tela");
        for (int i = 0; i < 9; i++) place(kGrid + i, kControls + i % 3 * 70, y + i / 3 * 28, 64, 24);
        y += 3 * 28 + 16;
        choices(L"Clima em relação ao JSON", kSide, 3);
        stepper(L"Tamanho do bloco", kSize);
        stepper(L"Largura do bloco", kWidth);
        stepper(L"Distância da borda", kMarginStep);
        stepper(L"Sombra atrás do bloco", kShadow);
        if (monitors.size() > 1) {
            heading(L"Aparece nos monitores");
            for (size_t i = 0; i < monitors.size() && i < kMaxMonitors; i++)
                row(monitors[i].second, kFirstMonitor + int(i));
        }
        break;
    case 4:
        heading(L"Cidade do clima");
        place(kCity, kContent, y + 4, kLabelWidth, kButtonHeight - 8);
        place(kSearch, kControls, y, kControlWidth);
        y += kRow;
        place(kResults, kContent, y, kContentWidth, 200);
        y += kRow;
        choices(L"Unidade", kCelsius, 2);
        row(L"Máxima e mínima do dia", kRange);
        break;
    case 5:
        heading(L"Papel de parede");
        wide(kFirstTheme, int(std::size(kThemes)));
        if (theme == 3) {
            row(L"Imagem: " + (imageFile.empty() ? std::wstring(L"nenhuma") : FileName(imageFile)), kPickImage);
            row(L"Ondular a imagem", kFlow);
        }
        if (theme == 4) row(L"Vídeo: " + (videoFile.empty() ? std::wstring(L"nenhum") : FileName(videoFile)), kPickVideo);
        row(L"Pausar com jogo ou tela cheia", kPause);
        row(L"Ícone com degradê nas pastas", kFolders);
        break;
    case 6:
        heading(L"Fundo da barra");
        wide(kFirstBarMode, 3);
        if (barMode == 1) stepper(L"Força do blur", kBlurRadius);
        stepper(L"Escurecer", kVeil);
        heading(L"Logo do Iniciar");
        wide(kLogoStandard, 2);
        if (logoCustom) {
            const wchar_t* names[3] = {L"Cor de cima (esquerda)", L"Cor do meio", L"Cor de baixo (direita)"};
            for (int i = 0; i < 3; i++) ink(kFirstLogoInk + i, names[i]);
        }
        heading(L"Player do Spotify");
        row(L"Na barra de tarefas", kPlayer);
        if (player) {
            wide(kFirstWave, 2);
            stepper(L"Força da onda", kWaveStrength);
            stepper(L"Largura da onda", kWaveWidth);
        }
        break;
    case 7:
        row(L"Iniciar com o Windows", kStartup);
        row(HwinfoStatus(), kHwinfo);
        heading(L"Wallget 1.2.0 · muda na hora, Salvar mantém");
        break;
    }
}

// Baixa o clima de um lugar numa thread à parte; o painel da prévia pega quando chegar.
static void FetchPreviewWeather(Place const& place) {
    if (place.latitude.empty()) return;
    std::thread([place] { FetchWeather(place.latitude, place.longitude); }).detach();
}

// Texto de "chave": valor (string ou número) num objeto JSON simples, sem aspas escapadas.
static std::wstring JsonValue(std::string const& object, const char* key) {
    std::string pattern = std::string("\"") + key + "\":";
    size_t p = object.find(pattern);
    if (p == std::string::npos) return L"";

    p += pattern.size();
    size_t end = object[p] == '"' ? object.find('"', ++p) : object.find_first_of(",}", p);
    return FromUtf8(object.substr(p, end - p));
}

// Lugares da resposta do geocoding do Open-Meteo: nome, estado e país, com latitude e longitude.
static std::vector<Place> ParsePlaces(std::string const& json) {
    std::vector<Place> found;
    for (size_t p = json.find("{\"id\":"); p != std::string::npos;) {
        size_t next = json.find("{\"id\":", p + 1);
        std::string object = json.substr(p, next - p);
        Place place{JsonValue(object, "name"), JsonValue(object, "latitude"), JsonValue(object, "longitude")};
        for (const char* key : {"admin1", "country"})
            if (auto part = JsonValue(object, key); !part.empty() && part != place.name) place.name += L", " + part;
        if (!place.latitude.empty()) found.push_back(place);
        p = next;
    }
    return found;
}

// Procura a cidade digitada (até 5 lugares com esse nome) numa thread à parte; a resposta volta em kSearchDone.
static void Search(HWND window) {
    wchar_t text[128];
    GetDlgItemTextW(window, kCity, text, 128);
    std::wstring query;
    for (unsigned char c : ToUtf8(text)) {
        wchar_t escaped[4];
        swprintf_s(escaped, L"%%%02X", c);
        query += isalnum(c) ? std::wstring(1, wchar_t(c)) : escaped;
    }
    if (query.empty()) return;

    SetDlgItemTextW(window, kSearch, L"Buscando...");
    std::thread([window, query, id = ++searches] {
        std::string json = HttpsRequest(L"geocoding-api.open-meteo.com", L"/v1/search?count=5&language=pt&format=json&name=" + query);
        PostMessageW(window, kSearchDone, id, LPARAM(new std::vector<Place>(ParsePlaces(json))));
    }).detach();
}

// Lista de lugares na caixa de seleção (o primeiro já escolhido); sem nenhum, só o aviso.
static void ShowPlaces(HWND window, std::vector<Place> const& found) {
    HWND list = GetDlgItem(window, kResults);
    places = found;
    SendMessageW(list, CB_RESETCONTENT, 0, 0);
    for (auto const& place : places) SendMessageW(list, CB_ADDSTRING, 0, LPARAM(place.name.c_str()));
    if (places.empty()) SendMessageW(list, CB_ADDSTRING, 0, LPARAM(L"Nenhuma cidade com esse nome"));
    SendMessageW(list, CB_SETCURSEL, 0, 0);
}

// Seletor de cor do Windows, começando na cor atual; true se escolheu outra.
static bool PickColor(HWND window, COLORREF& color) {
    CHOOSECOLORW dialog{sizeof dialog, window};
    dialog.rgbResult = color;
    dialog.lpCustColors = custom;
    dialog.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&dialog) || dialog.rgbResult == color) return false;
    color = dialog.rgbResult;
    return true;
}

const wchar_t kImageFilter[] = L"Imagens\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.webp;*.tif;*.tiff\0";
const wchar_t kVideoFilter[] = L"Vídeos\0*.mp4;*.mov;*.m4v;*.wmv;*.mkv;*.avi;*.webm\0";

// Seletor de arquivo (imagem ou vídeo); true se escolheu um.
static bool PickFile(HWND window, std::wstring& path, const wchar_t* filter) {
    wchar_t chosen[MAX_PATH * 2] = {};
    wcsncpy_s(chosen, path.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog{sizeof dialog, window};
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = chosen;
    dialog.nMaxFile = DWORD(std::size(chosen));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return false;
    path = chosen;
    return true;
}

// Interruptor de cada botão liga/desliga, ou nullptr.
static bool* ToggleOf(int id) {
    if (id >= kFirstPart && id < kFirstPart + int(std::size(kPartSwitches))) return &o.parts[id - kFirstPart];
    if (id >= kFirstField && id < kFirstField + FieldCount) return &o.fields[id - kFirstField];
    if (id >= kFirstMonitor && id < kFirstMonitor + kMaxMonitors) return &monitorShown[id - kFirstMonitor];
    switch (id) {
    case kJson: return &o.json;
    case kRange: return &o.range;
    case kFlow: return &imageFlow;
    case kPause: return &o.pause;
    case kFolders: return &folders;
    case kStartup: return &startup;
    case kPlayer: return &player;
    }
    return nullptr;
}

// Botão de escolha (uma entre várias) está escolhido?
static bool Chosen(int id) {
    if (id >= kFirstSection && id < kFirstSection + kSectionCount) return id - kFirstSection == section;
    if (id >= kGrid && id < kGrid + 9) return id - kGrid == o.position;
    if (id >= kSide && id < kSide + 3) return id - kSide == o.weatherSide;
    if (id >= kFirstTheme && id < kFirstTheme + int(std::size(kThemes))) return id - kFirstTheme == theme;
    if (id >= kFirstBarMode && id < kFirstBarMode + 3) return kBarModes[id - kFirstBarMode] == DWORD(barMode);
    if (id == kCelsius || id == kFahrenheit) return (id == kFahrenheit) == o.fahrenheit;
    if (id == kLogoStandard || id == kLogoCustom) return (id == kLogoCustom) == logoCustom;
    if (id == kFirstWave || id == kFirstWave + 1) return id - kFirstWave == wave;
    return false;
}

// Passo do botão (-, valor ou +) de um seletor de número: o seletor e a posição dentro dele (0, 1, 2).
static Stepper const* StepperOf(int id, int& part) {
    for (auto const& stepper : kSteppers)
        if (id >= stepper.id && id < stepper.id + 3) {
            part = id - stepper.id;
            return &stepper;
        }
    return nullptr;
}

// Botões desenhados à mão (fundo escuro): os de cor são a própria cor com o "#RRGGBB" em preto ou branco, conforme o
// que lê melhor; interruptores ligados e escolhas feitas ficam claros; o Salvar é claro e apagado sem mudança; as
// seções da barra lateral são texto, com a escolhida num fundo levemente mais claro; o valor dos seletores de número
// é só texto. Borda clara com o foco do teclado; apertado, fica mais escuro.
static void DrawButton(DRAWITEMSTRUCT const& item) {
    int id = item.CtlID, part = 0;
    bool ink = id >= kFirstInk && id < kFirstInk + kInkCount, logoInk = id >= kFirstLogoInk && id < kFirstLogoInk + 3;
    bool sectionButton = id >= kFirstSection && id < kFirstSection + kSectionCount, grid = id >= kGrid && id < kGrid + 9;
    Stepper const* stepper = StepperOf(id, part);
    bool* toggle = ToggleOf(id);
    bool disabled = item.itemState & ODS_DISABLED;
    bool on = id == kSave || (toggle && *toggle) || (!sectionButton && Chosen(id));

    COLORREF fill = ink ? ToColorRef(pending[id - kFirstInk]) : logoInk ? logo[id - kFirstLogoInk] : on && !disabled ? RGB(232, 232, 232) : RGB(44, 44, 48);
    if (sectionButton) fill = Chosen(id) ? RGB(34, 34, 38) : kBackground;
    if (stepper && part == 1) fill = kBackground;
    if (item.itemState & ODS_SELECTED) fill = RGB(GetRValue(fill) * 3 / 4, GetGValue(fill) * 3 / 4, GetBValue(fill) * 3 / 4);

    wchar_t text[96];
    if (ink || logoInk) wcscpy_s(text, Hex(fill).c_str());
    else if (id >= kFirstMonitor && id < kFirstMonitor + kMaxMonitors) wcscpy_s(text, *toggle ? L"Aparece" : L"Escondido");
    else if (toggle && id < kFirstField) wcscpy_s(text, *toggle ? L"Ligado" : L"Desligado");
    else if (toggle && id >= kFirstField + FieldCount) wcscpy_s(text, *toggle ? L"Ligado" : L"Desligado");
    else if (stepper && part == 1) swprintf_s(text, L"%d%s", *stepper->value, stepper->unit);
    else if (grid) text[0] = 0;
    else GetWindowTextW(item.hwndItem, text, 96);
    bool light = GetRValue(fill) * 0.299 + GetGValue(fill) * 0.587 + GetBValue(fill) * 0.114 > 140;

    HBRUSH brush = CreateSolidBrush(fill);
    bool flat = sectionButton || (stepper && part == 1);
    HPEN pen = CreatePen(PS_SOLID, Px(1), item.itemState & ODS_FOCUS ? RGB(230, 230, 230) : flat ? fill : RGB(70, 70, 76));
    RECT box = item.rcItem;
    FillRect(item.hDC, &box, background);
    HGDIOBJ oldBrush = SelectObject(item.hDC, brush), oldPen = SelectObject(item.hDC, pen), oldFont = SelectObject(item.hDC, font);
    RoundRect(item.hDC, box.left, box.top, box.right, box.bottom, Px(grid ? 6.f : 10.f), Px(grid ? 6.f : 10.f));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled && !stepper ? RGB(110, 110, 116) : sectionButton && !Chosen(id) ? kSoftText : light ? RGB(0, 0, 0) : kText);
    if (sectionButton) box.left += Px(14);
    DrawTextW(item.hDC, text, -1, &box, (sectionButton ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(item.hDC, oldBrush);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldFont);
    DeleteObject(brush);
    DeleteObject(pen);
}

// Nome do app em cima da barra lateral, a linha que separa a barra do resto e o nome de cada linha da seção (os
// títulos de grupo em cinza).
static void PaintLabels(HWND window) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    SetBkMode(dc, TRANSPARENT);

    HGDIOBJ oldFont = SelectObject(dc, titleFont);
    SetTextColor(dc, kText);
    RECT title{Px(kGap), Px(kGap), Px(kSidebar - 8), Px(kGap + 30)};
    DrawTextW(dc, L"Wallget", -1, &title, DT_VCENTER | DT_SINGLELINE);
    RECT line{Px(kSidebar), 0, Px(kSidebar) + 1, Px(kHeight)};
    FillRect(dc, &line, field);

    SelectObject(dc, font);
    for (auto const& item : labels) {
        RECT box{Px(kContent), Px(item.top), Px(item.heading ? kControls + kControlWidth : kControls - 8), Px(item.top + (item.heading ? 24 : kButtonHeight))};
        SetTextColor(dc, item.heading ? kSoftText : kText);
        DrawTextW(dc, item.text.c_str(), -1, &box, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (dirty) {
        RECT note{Px(kContent + 160), Px(kHeight - kGap - kButtonHeight), Px(kControls + kControlWidth - 140), Px(kHeight - kGap)};
        SetTextColor(dc, kSoftText);
        DrawTextW(dc, L"Não salvo", -1, &note, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(dc, oldFont);
    EndPaint(window, &paint);
}

// Direct3D + Direct2D na janela da prévia (como no FundoVivo) e os mesmos widgets dele: cartão da música, anéis e o
// painel do JSON/clima, no bloco da largura escolhida (o layout é refeito a cada quadro), e embaixo a barra de tarefas
// com o logo do Iniciar.
static bool CreatePreview(HWND window, int width, int height) {
    auto& p = preview;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                 &p.device, nullptr, nullptr)))
        return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> dxgiFactory;
    p.device.As(&dxgiDevice);
    dxgiDevice->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    DXGI_SWAP_CHAIN_DESC1 desc{UINT(width), UINT(height), DXGI_FORMAT_B8G8R8A8_UNORM, FALSE, {1, 0}, DXGI_USAGE_RENDER_TARGET_OUTPUT, 2,
                               DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_DISCARD};
    if (FAILED(dxgiFactory->CreateSwapChainForHwnd(p.device.Get(), window, &desc, nullptr, nullptr, &p.swapChain))) return false;

    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<IDXGISurface> surface;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, p.factory.GetAddressOf());
    p.factory->CreateDevice(dxgiDevice.Get(), &d2dDevice);
    d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &p.dc);
    p.dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    p.swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    D2D1_BITMAP_PROPERTIES1 properties{{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE}, 96, 96,
                                       D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW};
    p.dc->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &p.target);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)p.dwrite.GetAddressOf());
    p.dc->CreateSolidColorBrush({1, 1, 1, 1}, &p.brush);
    p.factory->CreateStrokeStyle({D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND, 10, D2D1_DASH_STYLE_SOLID, 0},
                                 nullptr, 0, &p.round);

    DXGI_ADAPTER_DESC adapterDesc{};
    adapter->GetDesc(&adapterDesc);
    p.stats = std::make_unique<SystemStats>(adapterDesc.AdapterLuid);
    p.card.Create(p.dwrite.Get(), scale);
    p.rings.Create(p.factory.Get(), p.dwrite.Get(), scale);
    p.lastFrame = Now();
    return true;
}

// Barra de tarefas de exemplo embaixo da prévia: faixa escura (mais escura com o Escurecer) e o logo do Iniciar no meio,
// quatro quadrados com o degradê de cima à esquerda até embaixo à direita (o padrão ou o personalizado).
static void DrawTaskbar(float width, float height) {
    auto& p = preview;
    const float s = scale, bar = 48 * s, top = height - bar, side = 24 * s, square = 11 * s, left = roundf((width - side) / 2), y = roundf(top + (bar - side) / 2);
    COLORREF const* colors = logoCustom ? logo : kLogoStandardColors;
    if (!p.logo || !std::equal(colors, colors + 3, p.logoKey)) {
        std::copy(colors, colors + 3, p.logoKey);
        D2D1_GRADIENT_STOP stops[3];
        for (int i = 0; i < 3; i++) stops[i] = {i / 2.f, ToColor(colors[i])};
        ComPtr<ID2D1GradientStopCollection> collection;
        p.dc->CreateGradientStopCollection(stops, 3, &collection);
        p.dc->CreateLinearGradientBrush({}, collection.Get(), &p.logo);
    }

    float shade = 0.14f * (1 - veilPercent / 100.f * 0.8f);
    p.brush->SetColor({shade, shade, shade + 0.01f, 1});
    p.dc->FillRectangle({0, top, width, height}, p.brush.Get());
    p.brush->SetColor({1, 1, 1, 0.06f});
    p.dc->FillRectangle({0, top, width, top + s}, p.brush.Get());
    p.logo->SetStartPoint({left, y});
    p.logo->SetEndPoint({left + side, y + side});
    for (float dy : {0.f, side - square})
        for (float dx : {0.f, side - square}) p.dc->FillRoundedRectangle({{left + dx, y + dy, left + dx + square, y + dy + square}, s, s}, p.logo.Get());
}

// Um quadro da prévia com o que está na tela: uma música de exemplo tocando (com a próxima da fila),
// o equalizador dançando sozinho, os anéis com os números de verdade e o JSON/clima do jeito que o FundoVivo desenha,
// no tamanho escolhido (o bloco largo encolhe até caber); embaixo, a barra com o logo.
static void DrawPreview() {
    auto& p = preview;
    if (!p.dc) return;

    double now = Now();
    float dt = std::min(float(now - p.lastFrame), 0.1f);
    p.lastFrame = now;
    SetPalette(pending);
    p.panel.options = o;
    p.card.showEqualizer = o.Equalizer();
    for (int i = 0; i < 4; i++) p.rings.show[i] = o.Ring(i);

    NowPlaying song{L"Música", L"Artista", true};
    song.position = 83;
    song.duration = 214;
    song.readAt = now;
    NextTrack next{L"Música", L"Próxima da fila", L"Outro artista"};
    p.equalizer.Simulate(now, dt);
    float cardLeft = float(Px((kPreviewWidth - o.width) / 2));
    p.layout = {cardLeft + o.width * scale, 30 * scale, cardLeft, cardLeft + CoverSize() + 16 * scale, 36 * scale, scale};
    p.card.Update(p.dc.Get(), p.dwrite.Get(), o.Music() ? song : NowPlaying{}, o.Next() ? next : NextTrack{}, p.layout, dt);
    if (now - p.lastReading > 1.5) {
        p.lastReading = now;
        p.stats->Read();
        double networkPercent = p.network.Read();
        p.rings.Update(*p.stats, p.network, networkPercent);
    }

    auto const& l = p.layout;
    const float s = l.scale, a = p.card.alpha * p.card.alpha * (3 - 2 * p.card.alpha),
                k = o.size / 100.f * std::min(1.f, (kPreviewWidth - 40) / o.width);
    float rowTop = l.top + a * (118 * s + p.card.NextRowHeight(s)), ringsY = roundf(rowTop + l.ringRadius + 2 * s);
    float panelY = p.rings.Count() ? ringsY + l.ringRadius + 22 * s : rowTop;
    D2D1::Matrix3x2F zoom = D2D1::Matrix3x2F::Scale(k, k, {(l.cardLeft + l.right) / 2, l.top});
    D2D1_SIZE_F size = p.target->GetSize();

    p.dc->SetTarget(p.target.Get());
    p.dc->BeginDraw();
    p.dc->Clear({0, 0, 0, 1});
    p.dc->SetTransform(zoom);
    if (a > 0) p.card.Draw(p.dc.Get(), p.brush.Get(), l, p.equalizer, a, -1, -1, false, false);
    p.rings.Draw(p.dc.Get(), p.brush.Get(), p.round.Get(), zoom, l, ringsY);
    p.panel.Draw(p.dc.Get(), l.cardLeft, panelY, l.right - l.cardLeft, false);
    p.dc->SetTransform(D2D1::Matrix3x2F::Identity());
    DrawTaskbar(size.width, size.height);
    p.dc->EndDraw();
    p.dc->SetTarget(nullptr);
    p.swapChain->Present(1, 0);
}

// Pergunta se salva antes de fechar com mudança pendente (Não = desfaz tudo); false = o usuário desistiu de fechar.
static bool ConfirmClose(HWND window) {
    if (!dirty) return true;

    int answer = MessageBoxW(window, L"Salvar as mudanças? Sem salvar, o fundo e a barra voltam ao que estavam.", L"Wallget",
                             MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDYES) Save(window);
    if (answer == IDNO) Revert();
    return answer != IDCANCEL;
}

// Volta ao padrão tudo o que é visual (cores, widgets, JSON, layout, clima e barra), sem salvar; tema do fundo,
// arquivos, cidade e início com o Windows ficam.
static void RestoreDefaults() {
    for (int i = 0; i < kInkCount; i++) pending[i] = kInks[i].standard;
    WidgetOptions standard;
    standard.hiddenMonitors.clear();
    o = standard;
    std::fill(std::begin(monitorShown), std::end(monitorShown), true);
    barMode = 1;
    blurRadius = 9;
    veilPercent = 12;
    logoCustom = false;
    std::copy(std::begin(kLogoStandardColors), std::end(kLogoStandardColors), logo);
    player = true;
    wave = 0;
    waveStrength = 70;
    waveWidth = 200;
}

// Cliques: seções trocam a tela; cores abrem o seletor; temas de cores trocam as 8; interruptores alternam; escolhas
// escolhem; - e + andam o número; imagem e vídeo abrem o seletor de arquivo; Buscar (ou Enter na caixa da cidade)
// procura e a lista escolhe a cidade; Reconfigurar roda o hwinfo-setup.ps1 como administrador; Restaurar volta ao
// padrão e Salvar mantém. Toda mudança já vale no fundo e na barra (Changed).
static void Command(HWND window, int id, int code) {
    int part = 0;
    bool* toggle = ToggleOf(id);
    Stepper const* stepper = StepperOf(id, part);
    bool relayout = false, changed = true;

    if (id >= kFirstSection && id < kFirstSection + kSectionCount) {
        section = id - kFirstSection;
        relayout = true;
        changed = false;
    } else if (id >= kFirstInk && id < kFirstInk + kInkCount) {
        COLORREF color = ToColorRef(pending[id - kFirstInk]);
        if (!PickColor(window, color)) return;
        pending[id - kFirstInk] = ToColor(color);
    } else if (id >= kFirstLogoInk && id < kFirstLogoInk + 3) {
        if (!PickColor(window, logo[id - kFirstLogoInk])) return;
    } else if (id >= kFirstPreset && id < kFirstPreset + int(std::size(kPresets))) {
        for (int i = 0; i < kInkCount; i++) pending[i] = ToColor(kPresets[id - kFirstPreset].colors[i]);
    } else if (toggle) {
        *toggle = !*toggle;
        relayout = id == kJson || id == kPlayer;
    } else if (stepper) {
        if (part == 1) return;
        *stepper->value = std::clamp(*stepper->value + (part ? stepper->step : -stepper->step), stepper->low, stepper->high);
    } else if (id >= kGrid && id < kGrid + 9) {
        o.position = id - kGrid;
    } else if (id >= kSide && id < kSide + 3) {
        o.weatherSide = id - kSide;
    } else if (id == kCelsius || id == kFahrenheit) {
        o.fahrenheit = id == kFahrenheit;
    } else if (id >= kFirstTheme && id < kFirstTheme + int(std::size(kThemes))) {
        int chosen = id - kFirstTheme;  // imagem e vídeo sem arquivo pedem um antes (senão o fundo fica preto)
        if (chosen == 3 && imageFile.empty() && !PickFile(window, imageFile, kImageFilter)) return;
        if (chosen == 4 && videoFile.empty() && !PickFile(window, videoFile, kVideoFilter)) return;
        theme = chosen;
        relayout = true;
    } else if (id == kPickImage) {
        if (!PickFile(window, imageFile, kImageFilter)) return;
        relayout = true;
    } else if (id == kPickVideo) {
        if (!PickFile(window, videoFile, kVideoFilter)) return;
        relayout = true;
    } else if (id >= kFirstBarMode && id < kFirstBarMode + 3) {
        barMode = int(kBarModes[id - kFirstBarMode]);
        relayout = true;
    } else if (id == kLogoStandard || id == kLogoCustom) {
        logoCustom = id == kLogoCustom;
        relayout = true;
    } else if (id == kFirstWave || id == kFirstWave + 1) {
        wave = id - kFirstWave;
    } else if (id == kHwinfo) {
        std::wstring parameters = L"-NoProfile -ExecutionPolicy Bypass -File \"" + ProjectPath(L"tools\\hwinfo-setup.ps1") + L"\"";
        ShellExecuteW(window, L"runas", L"powershell.exe", parameters.c_str(), nullptr, SW_HIDE);
        return;
    } else if (id == kSearch || (id == IDOK && GetFocus() == GetDlgItem(window, kCity))) {
        Search(window);
        return;
    } else if (id == kResults && code == CBN_SELCHANGE) {
        size_t chosen = SendMessageW(GetDlgItem(window, kResults), CB_GETCURSEL, 0, 0);
        if (chosen >= places.size() || (places[chosen].latitude == city.latitude && places[chosen].longitude == city.longitude)) return;
        city = places[chosen];
        FetchPreviewWeather(city);
    } else if (id == kRestore) {
        RestoreDefaults();
        relayout = true;
    } else if (id == kSave) {
        Save(window);
        return;
    } else {
        return;
    }

    if (relayout) Arrange(window);
    if (changed) Changed(window);
    else Redraw(window);
}

// Janela principal: seções à esquerda, opções no meio, prévia à direita (redesenhada a ~30 fps por um timer).
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_DRAWITEM:
        DrawButton(*(DRAWITEMSTRUCT*)lParam);
        return TRUE;
    case WM_PAINT:
        PaintLabels(window);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor(HDC(wParam), kText);
        SetBkColor(HDC(wParam), kField);
        return LRESULT(field);
    case WM_COMMAND:
        Command(window, LOWORD(wParam), HIWORD(wParam));
        return 0;
    case kSearchDone: {
        std::unique_ptr<std::vector<Place>> found((std::vector<Place>*)lParam);
        if (wParam != searches) return 0;  // resposta de uma busca anterior, que chegou depois
        SetDlgItemTextW(window, kSearch, L"Buscar");
        ShowPlaces(window, *found);
        Command(window, kResults, CBN_SELCHANGE);
        return 0;
    }
    case WM_TIMER:
        if (!IsIconic(window)) DrawPreview();
        return 0;
    case WM_CLOSE:
        if (ConfirmClose(window)) DestroyWindow(window);
        return 0;
    case WM_ENDSESSION:  // sair ou desligar com mudança pendente: desfaz, como fechar sem salvar
        if (wParam && dirty) Revert();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// Um controle filho com a fonte da janela (o lugar dele vem do Arrange).
static HWND Child(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int id) {
    HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent, HMENU(INT_PTR(id)),
                                 GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, WPARAM(font), FALSE);
    return child;
}

// Opções da tela começam no que está salvo (cidade sem nome salvo aparece como "Lugar atual").
static void LoadSettings() {
    for (int i = 0; i < kInkCount; i++) {
        pending[i] = ReadInk(Ink(i));
        custom[i] = ToColorRef(kInks[i].standard);
    }
    o = ReadWidgetOptions();
    monitors = ReadMonitorNames();
    for (size_t i = 0; i < kMaxMonitors; i++) monitorShown[i] = i >= monitors.size() || !o.Hidden(monitors[i].first);
    city = {SettingText(L"ClimaCidade", L"Lugar atual"), SettingText(L"ClimaLatitude", L""), SettingText(L"ClimaLongitude", L"")};
    if (city.latitude.empty()) city.name = L"Sem clima (escolha uma cidade)";

    std::wstring themeName = SettingText(L"FundoTema", L"nbhd");
    for (int i = 0; i < int(std::size(kThemes)); i++)
        if (!_wcsicmp(themeName.c_str(), kThemes[i])) theme = i;
    imageFile = SettingText(L"FundoImagem", L"");
    videoFile = SettingText(L"FundoVideo", L"");
    imageFlow = Setting(L"FundoOndular", 0) != 0;
    folders = Enabled(L"Pastas");

    barMode = int(std::min<DWORD>(Setting(L"BlurModo", 1), 2));
    blurRadius = int(std::min<DWORD>(Setting(L"BlurRaio", 9), 40));
    veil = std::min<DWORD>(Setting(L"BlurVeu", 31), 255);
    veilPercent = int(lroundf(veil / 2.55f));
    const wchar_t* logoNames[3] = {L"LogoCor1", L"LogoCor2", L"LogoCor3"};
    logoCustom = false;
    for (int i = 0; i < 3; i++) {
        std::wstring text = SettingText(logoNames[i], L"");
        logo[i] = FromHex(text, kLogoStandardColors[i]);
        logoCustom |= !text.empty();
    }
    player = Enabled(L"Player");
    wave = Setting(L"PlayerOnda", 0) == 1;
    waveStrength = int(std::clamp<DWORD>(Setting(L"PlayerOndaForca", 70), 30, 100));
    waveWidth = int(std::clamp<DWORD>(Setting(L"PlayerOndaLargura", 200), 100, 400));
    startup = StartsWithWindows();
}

// FundoConfig.exe (WallgetConfig.exe no instalador): configura os widgets do papel de parede, o fundo e a barra de
// tarefas, com uma prévia ao lado; as mudanças aparecem na hora e só ficam com o Salvar (fechar sem salvar desfaz).
// Uma janela só: aberto de novo (pela bandeja ou pelo Iniciar), só traz a aberta para a frente. Liga o papel de parede
// junto, se ele saiu pela bandeja (já rodando, a cópia nova sai na hora).
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    CreateMutexW(nullptr, TRUE, L"Local\\FundoConfig");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND open = FindWindowW(L"FundoConfig", nullptr)) {
            ShowWindow(open, IsIconic(open) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(open);
        }
        return 0;
    }
    ShellExecuteW(nullptr, nullptr, WallpaperExe().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    scale = GetDpiForSystem() / 96.f;
    font = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Variable Text");
    titleFont = CreateFontW(-Px(22), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Variable Display");
    background = CreateSolidBrush(kBackground);
    field = CreateSolidBrush(kField);
    LoadSettings();

    WNDCLASSW windowClass{0, WindowProc, 0, 0, instance, LoadIconW(instance, MAKEINTRESOURCEW(1)),
                          LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW), background, nullptr, L"FundoConfig"};
    WNDCLASSW previewClass{0, DefWindowProcW, 0, 0, instance, nullptr, nullptr, (HBRUSH)GetStockObject(BLACK_BRUSH), nullptr, L"FundoPrevia"};
    RegisterClassW(&windowClass);
    RegisterClassW(&previewClass);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT size{0, 0, Px(kPanelWidth + kPreviewWidth), Px(kHeight)};
    AdjustWindowRectEx(&size, style, FALSE, 0);
    HWND window = CreateWindowExW(0, L"FundoConfig", L"Wallget", style, CW_USEDEFAULT, CW_USEDEFAULT, size.right - size.left,
                                  size.bottom - size.top, nullptr, nullptr, instance, nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);

    auto button = [&](const wchar_t* text, int id) { return Child(window, L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW, id); };
    for (int i = 0; i < kSectionCount; i++) {
        button(kSections[i], kFirstSection + i);
        SetWindowPos(GetDlgItem(window, kFirstSection + i), nullptr, Px(12), Px(76 + i * 40.f), Px(kSidebar - 24), Px(34), SWP_NOZORDER);
    }
    for (auto const& preset : kPresets) button(preset.name, kFirstPreset + int(&preset - kPresets));
    for (int i = 0; i < kInkCount; i++) button(kInks[i].label, kFirstInk + i);
    for (int i = 0; i < 3; i++) button(L"", kFirstLogoInk + i);
    for (size_t i = 0; i < std::size(kPartSwitches); i++) button(kPartSwitches[i].label, kFirstPart + int(i));
    button(L"JSON", kJson);
    for (int i = 0; i < FieldCount; i++) button(kFieldSwitches[i].label, kFirstField + i);
    const wchar_t* cells[9] = {L"Em cima, à esquerda", L"Em cima, no meio", L"Em cima, à direita", L"No meio, à esquerda", L"No centro",
                               L"No meio, à direita", L"Embaixo, à esquerda", L"Embaixo, no meio", L"Embaixo, à direita"};
    for (int i = 0; i < 9; i++) button(cells[i], kGrid + i);
    const wchar_t* sides[3] = {L"Esquerda", L"Em cima", L"Direita"};
    for (int i = 0; i < 3; i++) button(sides[i], kSide + i);
    for (auto const& stepper : kSteppers) {
        button(L"−", stepper.id);
        EnableWindow(button(L"", stepper.id + 1), FALSE);
        button(L"+", stepper.id + 2);
    }
    for (int i = 0; i < kMaxMonitors; i++) button(L"", kFirstMonitor + i);
    HWND cityBox = Child(window, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, kCity);
    SendMessageW(cityBox, EM_SETCUEBANNER, TRUE, LPARAM(L"Cidade do clima (ex.: Campinas)"));
    button(L"Buscar", kSearch);
    HWND list = Child(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kResults);
    button(L"°C", kCelsius);
    button(L"°F", kFahrenheit);
    button(L"", kRange);
    const wchar_t* themes[5] = {L"Nbhd", L"Golden", L"Loop", L"Imagem", L"Vídeo"};
    for (int i = 0; i < 5; i++) button(themes[i], kFirstTheme + i);
    button(L"Escolher...", kPickImage);
    button(L"", kFlow);
    button(L"Escolher...", kPickVideo);
    button(L"", kPause);
    button(L"", kFolders);
    const wchar_t* modes[3] = {L"Blur", L"Acrílico", L"Transparente"};
    for (int i = 0; i < 3; i++) button(modes[i], kFirstBarMode + i);
    button(L"Branco, cinza e preto", kLogoStandard);
    button(L"Personalizado", kLogoCustom);
    button(L"", kPlayer);
    button(L"Onda do centro", kFirstWave);
    button(L"Onda de baixo para cima", kFirstWave + 1);
    button(L"", kStartup);
    button(L"Reconfigurar", kHwinfo);
    button(L"Restaurar padrão", kRestore);
    EnableWindow(button(L"Salvar", kSave), FALSE);
    SetWindowPos(GetDlgItem(window, kRestore), nullptr, Px(kContent), Px(kHeight - kGap - kButtonHeight), Px(150), Px(kButtonHeight), SWP_NOZORDER);
    SetWindowPos(GetDlgItem(window, kSave), nullptr, Px(kControls + kControlWidth - 130), Px(kHeight - kGap - kButtonHeight), Px(130),
                 Px(kButtonHeight), SWP_NOZORDER);
    for (HWND control : {cityBox, list}) SetWindowTheme(control, L"DarkMode_CFD", nullptr);
    ShowPlaces(window, {city});
    if (city.latitude.empty()) places.clear();

    HWND previewWindow = Child(window, L"FundoPrevia", L"", 0, 0);
    SetWindowPos(previewWindow, nullptr, Px(kPanelWidth), 0, Px(kPreviewWidth), Px(kHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    if (CreatePreview(previewWindow, Px(kPreviewWidth), Px(kHeight))) SetTimer(window, 1, 33, nullptr);
    FetchPreviewWeather(city);
    Arrange(window);

    ShowWindow(window, show);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
