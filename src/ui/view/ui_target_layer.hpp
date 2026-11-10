#ifndef __UI_VIEW_UI_TARGET_LAYER_HPP__
#define __UI_VIEW_UI_TARGET_LAYER_HPP__

/** The target that the compositor composes an entry on, from the bottom of the display to the top. */
enum class UiTargetLayer {
    BackgroundLayer,
    SelectionLayer,
    MainSurface,
    StatusLayer,
    AnimationLayer,
    ToastLayer,
};

constexpr int kUiTargetLayerCount = 6;

#endif
