#include "column.hpp"

#include <ctime>
#include <set>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

#include "../config.hpp"
#include "../globals.hpp"
#include "../view.hpp"
#include "../render.hpp"
#include "../types.hpp"

// Include hyprtile's utils for workspace naming
#include "../../utils.h"

using Hyprutils::Utils::CScopeGuard;

HTLayoutColumn::HTLayoutColumn(VIEWID new_view_id) : HTLayoutBase(new_view_id) {
    g_pAnimationManager->createAnimation(
        {0, 0},
        offset,
        g_pConfigManager->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );
    g_pAnimationManager->createAnimation(
        1.f,
        scale,
        g_pConfigManager->getAnimationPropertyConfig("workspaces"),
        AVARDAMAGE_NONE
    );

    init_position();
}

std::string HTLayoutColumn::layout_name() {
    return "column";
}

WORKSPACEID HTLayoutColumn::get_ws_id_from_column_index(int column, int index) {
    // column is 1-indexed (hyprtile uses 1, 2, 3... for columns)
    // index is 0-indexed (0="", 1="a", 2="b"...)
    std::string ws_name = get_workspace_name(column, index);

    for (const auto& workspace : g_pCompositor->getWorkspaces()) {
        // Remove padding before comparison
        std::string clean_ws_name = remove_padding(workspace->m_name);
        std::string clean_target = remove_padding(ws_name);
        if (clean_ws_name == clean_target)
            return workspace->m_id;
    }
    return WORKSPACE_INVALID;
}

WORKSPACEID HTLayoutColumn::get_ws_id_in_direction(int x, int y, std::string& direction) {
    // x is 0-indexed grid position (not column number)
    // y is row index within the column
    // existing_columns[x] gives the actual column number at grid position x

    if (x < 0 || x >= (int)existing_columns.size())
        return WORKSPACE_INVALID;

    int column = existing_columns[x];

    if (direction == "up") {
        if (y <= 0)
            return WORKSPACE_INVALID;
        y--;
    } else if (direction == "down") {
        // Check if row y+1 exists in this column
        auto it = column_heights.find(column);
        if (it == column_heights.end() || it->second <= y + 1)
            return WORKSPACE_INVALID;
        y++;
    } else if (direction == "left") {
        if (x <= 0)
            return WORKSPACE_INVALID;
        x--;
        column = existing_columns[x];
        // Clamp y to the height of the target column
        auto it = column_heights.find(column);
        if (it != column_heights.end() && y >= it->second)
            y = it->second - 1;
    } else if (direction == "right") {
        if (x + 1 >= (int)existing_columns.size())
            return WORKSPACE_INVALID;
        x++;
        column = existing_columns[x];
        // Clamp y to the height of the target column
        auto it = column_heights.find(column);
        if (it != column_heights.end() && y >= it->second)
            y = it->second - 1;
    } else {
        return WORKSPACE_INVALID;
    }

    return get_ws_id_from_column_index(column, y);
}

void HTLayoutColumn::on_move_swipe(Vector2D delta) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const float MOVE_DISTANCE = HyprtileConfig::value<Hyprlang::FLOAT>("gestures:move_distance");

    // Calculate bounds based on discovered workspaces
    if (num_columns == 0 || max_rows == 0)
        return;

    const CBox min_ws = calculate_ws_box(0, 0, HT_VIEW_CLOSED);
    const CBox max_ws = calculate_ws_box(num_columns - 1, max_rows - 1, HT_VIEW_CLOSED);

    Vector2D new_offset = offset->value() + delta / MOVE_DISTANCE * max_ws.w;
    new_offset = new_offset.clamp(Vector2D {-max_ws.x, -max_ws.y}, Vector2D {-min_ws.x, -min_ws.y});

    offset->resetAllCallbacks();
    offset->setValueAndWarp(new_offset);
}

WORKSPACEID HTLayoutColumn::on_move_swipe_end() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;

    build_overview_layout(HT_VIEW_CLOSED);
    WORKSPACEID closest = WORKSPACE_INVALID;
    double closest_dist = 1e9;
    for (const auto& [ws_id, box] : overview_layout) {
        const float dist_sq = offset->value().distanceSq(Vector2D {-box.box.x, -box.box.y});
        if (dist_sq < closest_dist) {
            closest_dist = dist_sq;
            closest = ws_id;
        }
    }
    return closest;
}

bool HTLayoutColumn::on_mouse_axis(double delta) {
    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr || !par_view->active || par_view->closing)
        return false;

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return false;

    // Get config values
    const float ZOOM_FACTOR = HyprtileConfig::value<Hyprlang::FLOAT>("zoom_factor");
    const float ZOOM_MIN = HyprtileConfig::value<Hyprlang::FLOAT>("zoom_min");
    const float ZOOM_MAX = HyprtileConfig::value<Hyprlang::FLOAT>("zoom_max");

    // Calculate new zoom level (scroll up = zoom in, scroll down = zoom out)
    float zoom_multiplier = (delta > 0) ? (1.f / ZOOM_FACTOR) : ZOOM_FACTOR;
    float new_zoom = zoom_level * zoom_multiplier;

    // Clamp zoom level (zoom_min/max are relative to base_scale)
    float min_zoom = ZOOM_MIN;
    float max_zoom = ZOOM_MAX / base_scale;
    new_zoom = std::clamp(new_zoom, min_zoom, max_zoom);

    if (std::abs(new_zoom - zoom_level) < 0.001f)
        return true;  // At limit, but still consume event

    // Get cursor position relative to monitor (in scaled coordinates)
    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const Vector2D cursor_pos = (mouse_coords - monitor->m_position) * monitor->m_scale;

    // Calculate new offset to keep cursor position fixed
    // Formula: o2 = P * (1 - s2/s1) + o1 * (s2/s1)
    // Use current animated values for smooth chained animations
    float old_actual_scale = scale->value();
    float new_actual_scale = base_scale * new_zoom;
    float scale_ratio = new_actual_scale / old_actual_scale;

    Vector2D new_offset = cursor_pos * (1.f - scale_ratio) + offset->value() * scale_ratio;

    // Clamp offset to valid bounds
    new_offset = clamp_offset_to_bounds(new_offset);

    // Apply changes with smooth animation
    zoom_level = new_zoom;
    *offset = new_offset;      // Animate to new offset
    *scale = new_actual_scale; // Animate to new scale

    // Trigger redraw
    g_pHyprRenderer->damageMonitor(monitor);
    g_pCompositor->scheduleFrameForMonitor(monitor);

    return true;  // Consume the event
}

void HTLayoutColumn::close_open_lerp(float perc) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    double open_scale =
        calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;
    Vector2D open_pos = {0, 0};

    build_overview_layout(HT_VIEW_CLOSED);
    double close_scale = 1.;
    Vector2D close_pos = -overview_layout[monitor->m_activeWorkspace->m_id].box.pos();

    double new_scale = std::lerp(close_scale, open_scale, perc);
    Vector2D new_pos = Vector2D {
        std::lerp(close_pos.x, open_pos.x, perc),
        std::lerp(close_pos.y, open_pos.y, perc)
    };

    scale->resetAllCallbacks();
    offset->resetAllCallbacks();
    scale->setValueAndWarp(new_scale);
    offset->setValueAndWarp(new_pos);
}

void HTLayoutColumn::on_show(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    // Rebuild layout to discover any new workspaces
    build_overview_layout(HT_VIEW_CLOSED);

    // Set initial position to current active workspace before animating
    if (overview_layout.count(monitor->m_activeWorkspace->m_id)) {
        offset->setValueAndWarp(-overview_layout[monitor->m_activeWorkspace->m_id].box.pos());
    }
    scale->setValueAndWarp(1.f);

    // Calculate and store base scale, reset zoom level
    base_scale = calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;
    zoom_level = 1.f;

    // Now animate TO the overview view
    *scale = base_scale;
    *offset = {0, 0};
}

void HTLayoutColumn::on_hide(CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    // Reset zoom level when hiding
    zoom_level = 1.f;

    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;
    *offset = -overview_layout[monitor->m_activeWorkspace->m_id].box.pos();
}

void HTLayoutColumn::on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete) {
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr || par_view->active)
        return;

    // prevent the thing from animating
    auto old_ws = g_pCompositor->getWorkspaceByID(old_id);
    auto new_ws = g_pCompositor->getWorkspaceByID(new_id);
    if (old_ws) old_ws->m_renderOffset->warp();
    if (new_ws) new_ws->m_renderOffset->warp();

    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;
    *offset = -overview_layout[new_id].box.pos();
}

bool HTLayoutColumn::should_render_window(PHLWINDOW window) {
    bool ori_result = HTLayoutBase::should_render_window(window);

    const PHLMONITOR monitor = get_monitor();
    if (window == nullptr || monitor == nullptr)
        return ori_result;

    if (window == g_pInputManager->m_currentlyDraggedWindow.lock())
        return false;

    PHLWORKSPACE workspace = window->m_workspace;
    if (workspace == nullptr)
        return false;

    CBox window_box = get_global_window_box(window, window->workspaceID());
    if (window_box.empty())
        return false;
    if (window_box.intersection(monitor->logicalBox()).empty())
        return false;

    return ori_result;
}

float HTLayoutColumn::drag_window_scale() {
    return scale->value();
}

void HTLayoutColumn::init_position() {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    build_overview_layout(HT_VIEW_CLOSED);
    if (overview_layout.count(monitor->m_activeWorkspace->m_id))
        offset->setValueAndWarp(-overview_layout[monitor->m_activeWorkspace->m_id].box.pos());
    scale->setValueAndWarp(1.f);
}

CBox HTLayoutColumn::calculate_ws_box(int x, int y, HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    // Use discovered dimensions (num_columns is actual count of existing columns)
    const int COLS = std::max(1, num_columns);
    const int ROWS = std::max(1, max_rows);

    const float GAP_SIZE = HyprtileConfig::value<Hyprlang::FLOAT>("gap_size") * monitor->m_scale;
    const Vector2D gaps = {GAP_SIZE, GAP_SIZE};

    if (GAP_SIZE > std::min(monitor->m_transformedSize.x, monitor->m_transformedSize.y)
        || GAP_SIZE < 0)
        fail_exit("Gap size {} induces invalid render dimensions", GAP_SIZE);

    double render_x = (monitor->m_transformedSize.x - gaps.x * (COLS + 1)) / COLS;
    double render_y = (monitor->m_transformedSize.y - gaps.y * (ROWS + 1)) / ROWS;
    const double mon_aspect = monitor->m_transformedSize.x / monitor->m_transformedSize.y;
    Vector2D start_offset {};

    // make correct aspect ratio
    if (render_y * mon_aspect > render_x) {
        start_offset.y = (render_y - render_x / mon_aspect) * ROWS / 2.f;
        render_y = render_x / mon_aspect;
    } else if (render_x / mon_aspect > render_y) {
        start_offset.x = (render_x - render_y * mon_aspect) * COLS / 2.f;
        render_x = render_y * mon_aspect;
    }

    float use_scale = scale->value();
    Vector2D use_offset = offset->value();
    if (stage == HT_VIEW_CLOSED) {
        use_scale = 1;
        use_offset = Vector2D {0, 0};
    } else if (stage == HT_VIEW_OPENED) {
        use_scale = render_x / monitor->m_transformedSize.x;
        use_offset = Vector2D {0, 0};
    }

    const Vector2D ws_sz = monitor->m_transformedSize * use_scale;
    return CBox {Vector2D {x, y} * (ws_sz + gaps) + gaps + use_offset + start_offset, ws_sz};
}

void HTLayoutColumn::build_overview_layout(HTViewStage stage) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    overview_layout.clear();
    column_heights.clear();
    existing_columns.clear();
    column_to_grid_x.clear();
    num_columns = 0;
    max_rows = 0;

    // First pass: discover all existing columns and their heights
    std::set<int> column_set;
    for (const auto& workspace : g_pCompositor->getWorkspaces()) {
        if (workspace->m_monitor->m_id != view_id)
            continue;

        int column = name_to_column(workspace->m_name);
        int index = name_to_index(workspace->m_name);

        // Skip special workspaces (name_to_column returns -1)
        if (column == -1)
            continue;

        column_set.insert(column);
        column_heights[column] = std::max(column_heights[column], index + 1);
        max_rows = std::max(max_rows, index + 1);
    }

    // Build sorted list of existing columns and create mapping
    existing_columns.assign(column_set.begin(), column_set.end());
    num_columns = existing_columns.size();
    for (int i = 0; i < num_columns; i++) {
        column_to_grid_x[existing_columns[i]] = i;
    }

    // Ensure we have at least a 1x1 grid
    if (num_columns == 0) num_columns = 1;
    if (max_rows == 0) max_rows = 1;

    // Now populate overview_layout for each discovered workspace
    for (const auto& workspace : g_pCompositor->getWorkspaces()) {
        if (workspace->m_monitor->m_id != view_id)
            continue;

        int column = name_to_column(workspace->m_name);
        int index = name_to_index(workspace->m_name);

        if (column == -1)
            continue;

        // x is grid position (mapped from column number), y is row index
        int x = column_to_grid_x[column];
        int y = index;

        const CBox ws_box = calculate_ws_box(x, y, stage);
        overview_layout[workspace->m_id] = HTWorkspace {x, y, ws_box};
    }
}

Vector2D HTLayoutColumn::clamp_offset_to_bounds(const Vector2D& off) {
    if (num_columns == 0 || max_rows == 0)
        return off;

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return off;

    // Calculate the bounds based on workspace grid at current scale
    const CBox min_ws = calculate_ws_box(0, 0, HT_VIEW_ANIMATING);
    const CBox max_ws = calculate_ws_box(num_columns - 1, max_rows - 1, HT_VIEW_ANIMATING);

    // Find the bounding box of all workspaces
    float min_x = min_ws.x;
    float max_x = max_ws.x + max_ws.w;
    float min_y = min_ws.y;
    float max_y = max_ws.y + max_ws.h;

    // Offset bounds: keep at least some portion of content visible
    const Vector2D monitor_size = monitor->m_transformedSize;
    float margin = 50.f;  // Keep at least this much content visible

    Vector2D clamped = off;
    // Offset is negative (panning left means positive offset change)
    // We want: -max_x + margin <= offset.x <= -min_x + monitor_size.x - margin
    double min_off_x = -(max_x - margin);
    double max_off_x = -min_x + monitor_size.x - margin;
    double min_off_y = -(max_y - margin);
    double max_off_y = -min_y + monitor_size.y - margin;
    clamped.x = std::clamp(clamped.x, min_off_x, max_off_x);
    clamped.y = std::clamp(clamped.y, min_off_y, max_off_y);

    return clamped;
}

void HTLayoutColumn::apply_edge_pan(PHLMONITOR monitor, float dt) {
    const float THRESHOLD = HyprtileConfig::value<Hyprlang::FLOAT>("edge_pan_threshold");
    const float SPEED = HyprtileConfig::value<Hyprlang::FLOAT>("edge_pan_speed");

    if (THRESHOLD <= 0 || SPEED <= 0)
        return;

    // Get cursor position relative to monitor
    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    if (!monitor->logicalBox().containsPoint(mouse_coords))
        return;  // Cursor not on this monitor

    const Vector2D local_cursor = (mouse_coords - monitor->m_position) * monitor->m_scale;
    const Vector2D monitor_size = monitor->m_transformedSize;

    // Calculate pan direction and intensity based on edge proximity
    Vector2D pan_delta = {0, 0};

    // Left edge - pan right (positive offset change) to reveal left content
    if (local_cursor.x < THRESHOLD) {
        float intensity = 1.f - (local_cursor.x / THRESHOLD);
        pan_delta.x = SPEED * intensity * dt;
    }
    // Right edge - pan left (negative offset change) to reveal right content
    else if (local_cursor.x > monitor_size.x - THRESHOLD) {
        float intensity = 1.f - ((monitor_size.x - local_cursor.x) / THRESHOLD);
        pan_delta.x = -SPEED * intensity * dt;
    }

    // Top edge - pan down (positive offset change) to reveal top content
    if (local_cursor.y < THRESHOLD) {
        float intensity = 1.f - (local_cursor.y / THRESHOLD);
        pan_delta.y = SPEED * intensity * dt;
    }
    // Bottom edge - pan up (negative offset change) to reveal bottom content
    else if (local_cursor.y > monitor_size.y - THRESHOLD) {
        float intensity = 1.f - ((monitor_size.y - local_cursor.y) / THRESHOLD);
        pan_delta.y = -SPEED * intensity * dt;
    }

    if (pan_delta.x == 0 && pan_delta.y == 0)
        return;

    // Apply pan and clamp to bounds
    Vector2D new_offset = offset->value() + pan_delta;
    new_offset = clamp_offset_to_bounds(new_offset);

    // Only apply if offset actually changed
    if ((new_offset - offset->value()).size() > 0.01f) {
        offset->setValueAndWarp(new_offset);

        // Schedule next frame to continue panning
        g_pCompositor->scheduleFrameForMonitor(monitor);
    }
}

void HTLayoutColumn::render() {
    HTLayoutBase::render();
    CScopeGuard x([this] { post_render(); });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return;
    const PHLMONITOR monitor = par_view->get_monitor();
    if (monitor == nullptr)
        return;

    // Calculate delta time for edge panning
    timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    float dt = 0.016f;  // Default to ~60fps
    if (edge_pan_initialized) {
        dt = (current_time.tv_sec - last_render_time.tv_sec)
           + (current_time.tv_nsec - last_render_time.tv_nsec) / 1e9f;
        dt = std::clamp(dt, 0.001f, 0.1f);  // Clamp to reasonable range
    }
    last_render_time = current_time;
    edge_pan_initialized = true;

    // Apply edge panning if overview is active and not closing
    if (par_view->active && !par_view->closing) {
        apply_edge_pan(monitor, dt);
    }

    static auto PACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.active_border");
    static auto PINACTIVECOL = CConfigValue<Hyprlang::CUSTOMTYPE>("general:col.inactive_border");

    auto* const ACTIVECOL = (CGradientValueData*)(PACTIVECOL.ptr())->getData();
    auto* const INACTIVECOL = (CGradientValueData*)(PINACTIVECOL.ptr())->getData();

    const float BORDERSIZE = HyprtileConfig::value<Hyprlang::FLOAT>("border_size");

    timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);

    g_pHyprRenderer->damageMonitor(monitor);
    g_pHyprOpenGL->m_renderData.pCurrentMonData->blurFBShouldRender = true;
    CBox monitor_box = {{0, 0}, monitor->m_transformedSize};

    CRectPassElement::SRectData data;
    data.color = CHyprColor {static_cast<uint64_t>(HyprtileConfig::value<Hyprlang::INT>("bg_color"))}.stripA();
    data.box = monitor_box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));

    // Do a dance with active workspaces: Hyprland will only properly render the
    // current active one so make the workspace active before rendering it, etc
    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;

    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_OUT,
        false,
        true
    );
    start_workspace->m_visible = false;

    build_overview_layout(HT_VIEW_ANIMATING);

    CBox global_mon_box = {monitor->m_position, monitor->m_transformedSize};
    for (const auto& [ws_id, ws_layout] : overview_layout) {
        // Skip if the box is empty
        if (ws_layout.box.width < 0.01 || ws_layout.box.height < 0.01)
            continue;

        // Could be nullptr, in which we render only layers
        const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

        // renderModif translation used by renderWorkspace is weird so need
        // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
        CBox render_box = {{ws_layout.box.pos() / scale->value()}, ws_layout.box.size()};
        if (monitor->m_transform % 2 == 1)
            std::swap(render_box.w, render_box.h);

        // render active one last
        if (workspace == start_workspace && start_workspace != nullptr)
            continue;

        CBox global_box = {ws_layout.box.pos() + monitor->m_position, ws_layout.box.size()};
        if (global_box.expand(BORDERSIZE).intersection(global_mon_box).empty())
            continue;

        const CGradientValueData border_col =
            monitor->m_activeWorkspace->m_id == ws_id ? *ACTIVECOL : *INACTIVECOL;
        CBox border_box = ws_layout.box;

        CBorderPassElement::SBorderData bdata;
        bdata.box = border_box;
        bdata.grad1 = border_col;
        bdata.borderSize = BORDERSIZE;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(bdata));

        if (workspace != nullptr) {
            monitor->m_activeWorkspace = workspace;
            g_pDesktopAnimationManager->startAnimation(
                workspace,
                CDesktopAnimationManager::ANIMATION_TYPE_IN,
                false,
                true
            );
            workspace->m_visible = true;

            ((render_workspace_t)(render_workspace_hook->m_original))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                &time,
                render_box
            );

            g_pDesktopAnimationManager->startAnimation(
                workspace,
                CDesktopAnimationManager::ANIMATION_TYPE_OUT,
                false,
                true
            );
            workspace->m_visible = false;
        } else {
            // If pWorkspace is null, then just render the layers
            ((render_workspace_t)(render_workspace_hook->m_original))(
                g_pHyprRenderer.get(),
                monitor,
                workspace,
                &time,
                render_box
            );
        }
    }

    monitor->m_activeWorkspace = start_workspace;
    g_pDesktopAnimationManager->startAnimation(
        start_workspace,
        CDesktopAnimationManager::ANIMATION_TYPE_IN,
        false,
        true
    );
    start_workspace->m_visible = true;

    // Render active workspace last so the dragging window is always on top when let go of
    if (start_workspace != nullptr && overview_layout.count(start_workspace->m_id)) {
        CBox ws_box = overview_layout[start_workspace->m_id].box;
        // make sure box is not empty
        if (ws_box.width > 0.01 && ws_box.height > 0.01) {
            // renderModif translation used by renderWorkspace is weird so need
            // to scale the translation up as well. Geometry is also calculated from pixel size and not transformed size??
            CBox render_box = {{ws_box.pos() / scale->value()}, ws_box.size()};
            if (monitor->m_transform % 2 == 1)
                std::swap(render_box.w, render_box.h);

            const CGradientValueData border_col =
                monitor->m_activeWorkspace->m_id == start_workspace->m_id ? *ACTIVECOL
                                                                          : *INACTIVECOL;
            CBox border_box = ws_box;

            CBorderPassElement::SBorderData bdata;
            bdata.box = border_box;
            bdata.grad1 = border_col;
            bdata.borderSize = BORDERSIZE;
            g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(bdata));

            ((render_workspace_t)(render_workspace_hook->m_original))(
                g_pHyprRenderer.get(),
                monitor,
                start_workspace,
                &time,
                render_box
            );
        }
    }

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;
    const PHLWINDOW dragged_window = g_pInputManager->m_currentlyDraggedWindow.lock();
    if (dragged_window == nullptr)
        return;
    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const CBox window_box = dragged_window->getWindowMainSurfaceBox()
                                .translate(-mouse_coords)
                                .scale(cursor_view->layout->drag_window_scale())
                                .translate(mouse_coords);
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, &time, window_box);
}
