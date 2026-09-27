#pragma once

#include "common/xaml.h"

#include <string>
#include <string_view>

FrameworkElement FindAncestor(DependencyObject element, std::wstring_view className, int maxLevels);
FrameworkElement FindDescendant(DependencyObject const& root, std::wstring_view name, int maxDepth = 40);
FrameworkElement ParentOf(DependencyObject const& element);
std::wstring ClassOf(DependencyObject const& element);
void KeepValue(DependencyObject const& target, DependencyProperty const& property,
               winrt::Windows::Foundation::IInspectable const& value, int maxFights);
