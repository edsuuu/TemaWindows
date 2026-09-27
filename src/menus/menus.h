#pragma once

#include "common/xaml.h"

#include <functional>
#include <string>
#include <string_view>

constexpr int kMaxFights = 400;
constexpr double kRailWidth = 60;
constexpr double kPanelGap = 8;

struct BrushProperties {
    DependencyProperty background{nullptr};
    DependencyProperty border{nullptr};
    DependencyProperty radius{nullptr};
};

BrushProperties BrushPropertiesOf(DependencyObject const& element);
Media::SolidColorBrush Solid(uint8_t alpha, uint8_t gray = 255);
float GlassRadius();
void ApplyPanelGlass(FrameworkElement const& element);
void ApplyCard(FrameworkElement const& element, float radius, uint8_t fill, float rim = .3f);
void ClearVeil(FrameworkElement const& element, uint8_t line = 0);
void MatchGlassCorners(FrameworkElement const& border);

bool StyleStartMenu(FrameworkElement const& element, std::wstring_view name, std::wstring_view type);
bool StyleSearch(FrameworkElement const& element, std::wstring_view name);
bool StyleQuickSettings(FrameworkElement const& element, std::wstring_view name, std::wstring_view type);
bool StyleNotifications(FrameworkElement const& element, std::wstring_view name);
void ReplaceSearchLens(FrameworkElement const& element, std::wstring_view name);

void ApplyStartLayout(FrameworkElement const& element, std::wstring_view name, bool startProcess);
void FillFolders(FrameworkElement const& appsGrid);

extern winrt::weak_ref<FrameworkElement> g_avatarSlot;
extern winrt::weak_ref<FrameworkElement> g_powerSlot;
void BuildSideRailWhenReady(Controls::Grid const& menu);
Controls::Button SideButton(wchar_t glyph, const wchar_t* label, const wchar_t* tip, bool selected, std::function<void()> click);
void Launch(std::wstring const& target);
void WatchFlyout(FrameworkElement const& presenter);
FrameworkElement CreateMediaTimeline();

void WatchTreeDump(winrt::Windows::Foundation::IInspectable const& root);
