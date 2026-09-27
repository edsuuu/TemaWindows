#include "menus/menus.h"

// Painel de notificações e calendário (ShellExperienceHost): os dois painéis viram vidro, os véus internos somem e cada
// notificação vira um cartão.
bool StyleNotifications(FrameworkElement const& element, std::wstring_view name) {
    if (name == L"NotificationCenterGrid" || name == L"CalendarCenterGrid") {
        ApplyPanelGlass(element);
        return true;
    }

    if (name == L"CalendarControlScrollViewer" || name == L"FocusGrid") {
        ClearVeil(element);
        return true;
    }

    if (name == L"ItemOpaquePlating") {
        ApplyCard(element, 14, 0x0C);
        return true;
    }

    return false;
}
