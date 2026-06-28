#include <any>
#include <sstream>

#define private public
#define protected public
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#undef protected
#undef private

#include <hyprland/protocols/wlr-layer-shell-unstable-v1.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/helpers/time/Time.hpp>

#include <cmath>

#include "../config.hpp"
#include "../globals.hpp"
#include "../pass/pass_element.hpp"
#include "../types.hpp"
#include "layout_base.hpp"

HTLayoutBase::HTLayoutBase(VIEWID new_view_id) : view_id(new_view_id) {}

void HTLayoutBase::on_move_swipe(Vector2D delta) {
    ;
}

WORKSPACEID HTLayoutBase::on_move_swipe_end() {
    return WORKSPACE_INVALID;
}

WORKSPACEID HTLayoutBase::get_ws_id_in_direction(int x, int y, std::string& direction) {
    if (direction == "up") {
        y--;
    } else if (direction == "down") {
        y++;
    } else if (direction == "right") {
        x++;
    } else if (direction == "left") {
        x--;
    } else {
        return WORKSPACE_INVALID;
    }
    return get_ws_id_from_xy(x, y);
}

bool HTLayoutBase::on_mouse_axis(double delta) {
    return false;
}

bool HTLayoutBase::should_manage_mouse() {
    return true;
}

bool HTLayoutBase::should_render_window(PHLWINDOW window) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || window == nullptr)
        return false;

    return ((should_render_window_t)(should_render_window_hook->m_original))(
        g_pHyprRenderer.get(),
        window,
        monitor
    );
}

float HTLayoutBase::drag_window_scale() {
    return 1.f;
}

void HTLayoutBase::init_position() {
    ;
}

void HTLayoutBase::build_overview_layout(HTViewStage stage) {
    ;
}

void HTLayoutBase::update_focus_state() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || monitor->m_activeWorkspace == nullptr)
        return;

    const WORKSPACEID current_id = monitor->m_activeWorkspace->m_id;
    const PHTVIEW view = ht_manager ? ht_manager->get_view_from_id(view_id) : nullptr;
    const bool overview_active = view != nullptr && view->active;
    const bool is_closing = view != nullptr && view->closing;

    // When overview is completely inactive, drop all focus emphasis
    if (!overview_active) {
        focus_scales.clear();
        return;
    }

    // Closing: deflate everything from its live value and let the close finish
    if (is_closing) {
        for (auto& [ws_id, scale_var] : focus_scales)
            *scale_var = 1.f;
        return;
    }

    // Grow the current tile, shrink all others — each retargeted from its live
    // value, so however fast focus moves, every tile's scale stays continuous.
    const float target_scale = HTConfig::value<Hyprlang::FLOAT>("focus_scale");
    for (auto& [ws_id, scale_var] : focus_scales) {
        if (ws_id != current_id || target_scale <= 0.f)
            *scale_var = 1.f;
    }

    if (target_scale > 0.f) {
        auto& current_scale = focus_scales[current_id];
        if (!current_scale)
            g_pAnimationManager->createAnimation(
                1.f,
                current_scale,
                Config::animationTree()->getAnimationPropertyConfig("workspaces"),
                AVARDAMAGE_NONE
            );
        *current_scale = target_scale;
    }

    // Drop tiles that have fully deflated
    std::erase_if(focus_scales, [&current_id](const auto& e) {
        return e.first != current_id && e.second->value() == 1.f && e.second->goal() == 1.f;
    });
}

float HTLayoutBase::focus_scale_for_id(WORKSPACEID workspace_id, HTViewStage stage) {
    if (stage == HT_VIEW_CLOSED)
        return 1.f;

    const PHTVIEW view = ht_manager ? ht_manager->get_view_from_id(view_id) : nullptr;
    if (view == nullptr || !view->active)
        return 1.f;

    const auto it = focus_scales.find(workspace_id);
    return it != focus_scales.end() ? it->second->value() : 1.f;
}

CBox HTLayoutBase::apply_focus_scale(const CBox& box, WORKSPACEID workspace_id, HTViewStage stage) {
    const float scale = focus_scale_for_id(workspace_id, stage);
    if (scale == 1.f)
        return box;

    const Vector2D center = box.pos() + box.size() / 2.f;
    const Vector2D new_size = box.size() * scale;
    return CBox {center - new_size / 2.f, new_size};
}

void HTLayoutBase::render() {
    // Render the monitor wallpaper as the overview backdrop. Mirrors Hyprland's
    // own renderWorkspace(no-workspace) path: the built-in background (wallpaper
    // texture / background color) followed by the BACKGROUND and BOTTOM layer
    // surfaces (some wallpaper daemons draw onto the bottom layer).
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    g_pHyprRenderer->renderBackground(monitor);

    const auto now = Time::steadyNow();
    for (const auto& ls : monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]) {
        g_pHyprRenderer->renderLayer(ls.lock(), monitor, now);
    }
    for (const auto& ls : monitor->m_layerSurfaceLayers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]) {
        g_pHyprRenderer->renderLayer(ls.lock(), monitor, now);
    }
}

const std::string CLEAR_PASS_ELEMENT_NAME = "CClearPassElement";

void HTLayoutBase::post_render() {
    bool first = true;
    std::erase_if(g_pHyprRenderer->m_renderPass.m_passElements, [&first](const auto& e) {
        bool res = e.element->passName() == CLEAR_PASS_ELEMENT_NAME && !first;
        first = false;
        return res;
    });
    g_pHyprRenderer->m_renderPass.add(makeUnique<HTPassElement>());
    // g_pHyprOpenGL->setDamage(CRegion {CBox {0, 0, INT32_MAX, INT32_MAX}});
}

PHLMONITOR HTLayoutBase::get_monitor() {
    const auto par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return nullptr;
    return par_view->get_monitor();
}

WORKSPACEID HTLayoutBase::get_ws_id_from_global(Vector2D pos) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;

    if (!monitor->logicalBox().containsPoint(pos))
        return WORKSPACE_INVALID;

    Vector2D relative_pos = (pos - monitor->m_position) * monitor->m_scale;
    for (const auto& [id, layout] : overview_layout)
        if (layout.box.containsPoint(relative_pos))
            return id;

    return WORKSPACE_INVALID;
}

WORKSPACEID HTLayoutBase::get_ws_id_from_xy(int x, int y) {
    for (const auto& [id, layout] : overview_layout)
        if (layout.x == x && layout.y == y)
            return id;

    return WORKSPACE_INVALID;
}

CBox HTLayoutBase::get_global_window_box(PHLWINDOW window, WORKSPACEID workspace_id) {
    if (window == nullptr)
        return {};

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(workspace_id);
    if (workspace == nullptr || workspace->m_monitor != monitor)
        return {};

    const CBox ws_window_box = window->getWindowMainSurfaceBox();

    const Vector2D top_left =
        local_ws_unscaled_to_global(ws_window_box.pos() - monitor->m_position, workspace->m_id);
    const Vector2D bottom_right = local_ws_unscaled_to_global(
        ws_window_box.pos() + ws_window_box.size() - monitor->m_position,
        workspace->m_id
    );

    return {top_left, bottom_right - top_left};
}

CBox HTLayoutBase::get_global_ws_box(WORKSPACEID workspace_id) {
    const CBox scaled_ws_box = overview_layout[workspace_id].box;
    const Vector2D top_left = local_ws_scaled_to_global(scaled_ws_box.pos(), workspace_id);
    const Vector2D bottom_right =
        local_ws_scaled_to_global(scaled_ws_box.pos() + scaled_ws_box.size(), workspace_id);
    return {top_left, bottom_right - top_left};
}

Vector2D HTLayoutBase::global_to_local_ws_unscaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos -= monitor->m_position;
    pos *= monitor->m_scale;
    pos -= workspace_box.pos();
    pos /= monitor->m_scale;
    pos /= workspace_box.w / monitor->m_transformedSize.x;
    return pos;
}

Vector2D HTLayoutBase::global_to_local_ws_scaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos = global_to_local_ws_unscaled(pos, workspace_id);
    pos *= monitor->m_scale;
    return pos;
}

Vector2D HTLayoutBase::local_ws_unscaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos *= workspace_box.w / monitor->m_transformedSize.x;
    pos *= monitor->m_scale;
    pos += workspace_box.pos();
    pos /= monitor->m_scale;
    pos += monitor->m_position;
    return pos;
}

Vector2D HTLayoutBase::local_ws_scaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos /= monitor->m_scale;
    return local_ws_unscaled_to_global(pos, workspace_id);
}
