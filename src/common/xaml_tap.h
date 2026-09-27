#pragma once

#include "common/xaml.h"

#include <xamlom.h>
#include <initializer_list>

extern const CLSID kTapClsid;

void OnTapStarted();
void OnElementAdded(ParentChildRelation const& relation, VisualElement const& element);

winrt::Windows::Foundation::IInspectable ElementFromHandle(InstanceHandle handle);
FrameworkElement FrameworkElementFromHandle(InstanceHandle handle);
HRESULT InjectTap(DWORD pid);
void InjectIntoProcesses(const wchar_t* mutexName, std::initializer_list<const wchar_t*> exeNames, const wchar_t* enabledSetting,
                         DWORD intervalMs);
