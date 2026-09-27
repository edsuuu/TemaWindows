#include "common/xaml_tree.h"

// Sobe a árvore (a partir do próprio elemento) até achar a classe pedida, por no máximo maxLevels níveis.
FrameworkElement FindAncestor(DependencyObject element, std::wstring_view className, int maxLevels) {
    for (int level = 0; level < maxLevels && element; level++, element = VisualTreeHelper::GetParent(element))
        if (winrt::get_class_name(element) == className) return element.as<FrameworkElement>();

    return nullptr;
}

// Procura, em profundidade, o primeiro descendente com o nome pedido, descendo no máximo maxDepth níveis.
FrameworkElement FindDescendant(DependencyObject const& root, std::wstring_view name, int maxDepth) {
    for (int i = 0, count = VisualTreeHelper::GetChildrenCount(root); i < count; i++) {
        auto child = VisualTreeHelper::GetChild(root, i);
        if (auto element = child.try_as<FrameworkElement>(); element && element.Name() == name) return element;

        if (maxDepth > 1)
            if (auto found = FindDescendant(child, name, maxDepth - 1)) return found;
    }

    return nullptr;
}

// Pai visual do elemento, se for um FrameworkElement.
FrameworkElement ParentOf(DependencyObject const& element) {
    return element ? VisualTreeHelper::GetParent(element).try_as<FrameworkElement>() : nullptr;
}

// Nome da classe do elemento ("" se nulo).
std::wstring ClassOf(DependencyObject const& element) {
    return element ? std::wstring(winrt::get_class_name(element)) : L"";
}

// Fixa um valor: se o shell (tema, estado visual) trocar, volta para o nosso. Sem reentrar e desistindo depois de
// maxFights brigas, porque o Grid avisa "mudou" até com o mesmo valor (uma recursão infinita já derrubou o ShellHost)
// e o shell pode disputar o valor para sempre.
void KeepValue(DependencyObject const& target, DependencyProperty const& property,
               winrt::Windows::Foundation::IInspectable const& value, int maxFights) {
    static thread_local bool busy;
    if (!property || busy) return;

    busy = true;
    try { target.SetValue(property, value); } catch (...) {}
    busy = false;

    target.RegisterPropertyChangedCallback(property, [value, maxFights, fights = 0](DependencyObject const& o, DependencyProperty const& p) mutable {
        if (busy || ++fights > maxFights) return;

        busy = true;
        try { o.SetValue(p, value); } catch (...) {}
        busy = false;
    });
}
