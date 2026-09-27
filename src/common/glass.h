#pragma once

#include "common/xaml.h"

#include <winrt/Windows.Graphics.Effects.h>

using winrt::Windows::Graphics::Effects::IGraphicsEffect;
using winrt::Windows::Graphics::Effects::IGraphicsEffectSource;

using GlassBrushFactory = CompositionBrush (*)(Compositor const&);

constexpr Color kGlassColor{255, 14, 14, 16};

IGraphicsEffectSource BackdropSource();
IGraphicsEffect GaussianBlur(float sigma, IGraphicsEffectSource const& source);
IGraphicsEffect GlassEffect(float sigma, float tint);
CompositionBrush EffectOnBackdrop(Compositor const& compositor, IGraphicsEffect const& effect, CompositionBrush const& backdrop);
CompositionBrush TintedGlass(Compositor const& compositor);
Media::XamlCompositionBrushBase GlassXamlBrush(Compositor const& compositor, GlassBrushFactory factory);
void Sheen(FrameworkElement const& element, float radius, float rim, uint8_t glow);
