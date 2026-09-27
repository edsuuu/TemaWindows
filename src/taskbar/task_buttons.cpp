#include "taskbar/taskbar.h"
#include "common/log.h"
#include "common/xaml_tree.h"

// Indicador cinza em cada botão de app da lista (o botão Iniciar não é um TaskListButton e fica de fora). O fundo de
// hover/ativo e a placa de várias janelas ficam os nativos.
static void StyleTaskButtons(FrameworkElement const& list) {
    for (int i = 0, count = VisualTreeHelper::GetChildrenCount(list); i < count; i++) {
        auto button = VisualTreeHelper::GetChild(list, i).try_as<FrameworkElement>();
        if (!button || winrt::get_class_name(button) != L"Taskbar.TaskListButton") continue;
        if (!FindDescendant(button, L"Icon", 2)) continue;

        if (auto indicator = FindDescendant(button, L"RunningIndicator", 2)) MirrorRunningIndicator(indicator);
    }
}

// Liga o indicador cinza dos botões de apps e o reaplica a cada layout: o ItemsRepeater recicla botões e recria
// templates.
void HookTaskButtons(FrameworkElement const& list) {
    list.LayoutUpdated([list](auto&&, auto&&) {
        try { StyleTaskButtons(list); } catch (...) {}
    });
    StyleTaskButtons(list);
    Log(L"indicadores cinza da barra ligados");
}
