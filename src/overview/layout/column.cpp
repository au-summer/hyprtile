#include "column.hpp"

#include <algorithm>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/BorderPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#include <map>

#include "../../utils.h"
#include "../config.hpp"
#include "../globals.hpp"
#include "../overview.hpp"
#include "../render.hpp"
#include "../types.hpp"

using Hyprutils::Utils::CScopeGuard;

HTLayoutColumn::HTLayoutColumn(VIEWID new_view_id) : HTLayoutBase(new_view_id)
{
    g_pAnimationManager->createAnimation({0, 0}, offset, Config::animationTree()->getAnimationPropertyConfig("workspaces"),
                                         AVARDAMAGE_NONE);
    g_pAnimationManager->createAnimation(1.f, scale, Config::animationTree()->getAnimationPropertyConfig("workspaces"),
                                         AVARDAMAGE_NONE);

    init_position();
}

std::string HTLayoutColumn::layout_name()
{
    return "column";
}

void HTLayoutColumn::rebuild_columns()
{
    columns.clear();

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    // Collect all workspaces on this monitor, grouped by column
    std::map<int, std::vector<std::pair<int, WORKSPACEID>>> col_map; // column_id -> [(index, ws_id)]

    for (const auto &ws : g_pCompositor->getWorkspaces())
    {
        if (ws == nullptr)
            continue;
        if (ws->m_monitor != monitor)
            continue;
        if (ws->m_isSpecialWorkspace)
            continue;

        int col = name_to_column(ws->m_name);
        int idx = name_to_index(ws->m_name);

        if (col == -1)
            continue; // Skip special/invalid workspaces

        col_map[col].push_back({idx, ws->m_id});
    }

    // Sort workspaces within each column by index and convert to ColumnInfo
    for (auto &[col_id, ws_list] : col_map)
    {
        std::sort(ws_list.begin(), ws_list.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

        ColumnInfo ci;
        ci.column_id = col_id;
        for (const auto &[idx, ws_id] : ws_list)
        {
            ci.workspaces.push_back(ws_id);
        }
        columns.push_back(ci);
    }

    // Sort columns by column_id
    std::sort(columns.begin(), columns.end(), [](const auto &a, const auto &b) { return a.column_id < b.column_id; });
}

int HTLayoutColumn::get_column_count()
{
    return std::max(1, (int)columns.size());
}

int HTLayoutColumn::get_max_rows()
{
    int max_rows = 1;
    for (const auto &col : columns)
    {
        max_rows = std::max(max_rows, (int)col.workspaces.size());
    }
    return max_rows;
}

std::pair<int, int> HTLayoutColumn::get_ws_grid_position(WORKSPACEID ws_id)
{
    for (size_t col_idx = 0; col_idx < columns.size(); col_idx++)
    {
        const auto &col = columns[col_idx];
        for (size_t row_idx = 0; row_idx < col.workspaces.size(); row_idx++)
        {
            if (col.workspaces[row_idx] == ws_id)
            {
                return {col_idx, row_idx};
            }
        }
    }
    return {-1, -1};
}

WORKSPACEID HTLayoutColumn::get_ws_id_in_direction(int col_idx, int row_idx, std::string &direction)
{
    rebuild_columns();

    if (columns.empty())
        return WORKSPACE_INVALID;

    if (direction == "up")
    {
        row_idx--;
        if (row_idx < 0)
            return WORKSPACE_INVALID;
    }
    else if (direction == "down")
    {
        row_idx++;
    }
    else if (direction == "left")
    {
        col_idx--;
        if (col_idx < 0)
            return WORKSPACE_INVALID;
        // Clamp row_idx to target column's size
        if (col_idx < (int)columns.size())
        {
            row_idx = std::min(row_idx, (int)columns[col_idx].workspaces.size() - 1);
            row_idx = std::max(row_idx, 0);
        }
    }
    else if (direction == "right")
    {
        col_idx++;
        if (col_idx >= (int)columns.size())
            return WORKSPACE_INVALID;
        // Clamp row_idx to target column's size
        row_idx = std::min(row_idx, (int)columns[col_idx].workspaces.size() - 1);
        row_idx = std::max(row_idx, 0);
    }
    else
    {
        return WORKSPACE_INVALID;
    }

    if (col_idx < 0 || col_idx >= (int)columns.size())
        return WORKSPACE_INVALID;

    const auto &target_col = columns[col_idx];

    if (row_idx < 0 || row_idx >= (int)target_col.workspaces.size())
    {
        // Going down beyond existing workspaces - could create new one
        // For now, return invalid
        return WORKSPACE_INVALID;
    }

    return target_col.workspaces[row_idx];
}

void HTLayoutColumn::on_move_swipe(Vector2D delta)
{
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    rebuild_columns();
    if (columns.empty())
        return;

    const float MOVE_DISTANCE = HTConfig::value<Hyprlang::FLOAT>("gestures:move_distance");

    int num_cols = get_column_count();
    int max_rows = get_max_rows();

    const CBox min_ws = calculate_ws_box(0, 0, HT_VIEW_CLOSED);
    const CBox max_ws = calculate_ws_box(num_cols - 1, max_rows - 1, HT_VIEW_CLOSED);

    Vector2D new_offset = offset->value() + delta / MOVE_DISTANCE * max_ws.w;
    new_offset = new_offset.clamp(Vector2D{-max_ws.x, -max_ws.y}, Vector2D{-min_ws.x, -min_ws.y});

    offset->resetAllCallbacks();
    offset->setValueAndWarp(new_offset);
}

WORKSPACEID HTLayoutColumn::on_move_swipe_end()
{
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;

    build_overview_layout(HT_VIEW_CLOSED);
    WORKSPACEID closest = WORKSPACE_INVALID;
    double closest_dist = 1e9;
    for (const auto &[ws_id, box] : overview_layout)
    {
        const float dist_sq = offset->value().distanceSq(Vector2D{-box.box.x, -box.box.y});
        if (dist_sq < closest_dist)
        {
            closest_dist = dist_sq;
            closest = ws_id;
        }
    }
    return closest;
}

void HTLayoutColumn::close_open_lerp(float perc)
{
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    rebuild_columns();
    int num_cols = get_column_count();
    int max_rows = get_max_rows();

    double open_scale = calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;
    Vector2D open_pos = {0, 0};

    build_overview_layout(HT_VIEW_CLOSED);
    double close_scale = 1.;
    Vector2D close_pos = -overview_layout[monitor->m_activeWorkspace->m_id].box.pos();

    double new_scale = std::lerp(close_scale, open_scale, perc);
    Vector2D new_pos = Vector2D{std::lerp(close_pos.x, open_pos.x, perc), std::lerp(close_pos.y, open_pos.y, perc)};

    scale->resetAllCallbacks();
    offset->resetAllCallbacks();
    scale->setValueAndWarp(new_scale);
    offset->setValueAndWarp(new_pos);
}

void HTLayoutColumn::on_show(CallbackFun on_complete)
{
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    // Animate to the overview position from the CURRENT camera values: a fresh
    // open starts from the closed pose (init_position warped it there), while a
    // committed swipe-open continues from the lerped position instead of
    // snapping back. Focus emphasis is handled by update_focus_state.
    *scale = calculate_ws_box(0, 0, HT_VIEW_OPENED).w / monitor->m_transformedSize.x;
    *offset = {0, 0};
}

void HTLayoutColumn::on_hide(CallbackFun on_complete)
{
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;

    // Find the current workspace's position
    auto it = overview_layout.find(monitor->m_activeWorkspace->m_id);
    if (it != overview_layout.end())
    {
        *offset = -it->second.box.pos();
    }
    else
    {
        *offset = {0, 0};
    }
}

void HTLayoutColumn::on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete)
{
    CScopeGuard x([this, &on_complete] {
        if (on_complete != nullptr)
            offset->setCallbackOnEnd(on_complete);
    });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr || par_view->active)
        return;

    // Prevent the workspace from animating
    auto old_ws = g_pCompositor->getWorkspaceByID(old_id);
    auto new_ws = g_pCompositor->getWorkspaceByID(new_id);
    if (old_ws)
        old_ws->m_renderOffset->warp();
    if (new_ws)
        new_ws->m_renderOffset->warp();

    build_overview_layout(HT_VIEW_CLOSED);
    *scale = 1.;

    auto it = overview_layout.find(new_id);
    if (it != overview_layout.end())
    {
        *offset = -it->second.box.pos();
    }
}

bool HTLayoutColumn::should_render_window(PHLWINDOW window)
{
    bool ori_result = HTLayoutBase::should_render_window(window);

    const PHLMONITOR monitor = get_monitor();
    if (window == nullptr || monitor == nullptr)
        return ori_result;

    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target != nullptr && window == target->window())
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

float HTLayoutColumn::drag_window_scale()
{
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return 1.f;

    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const WORKSPACEID ws_id = get_ws_id_from_global(mouse_coords);
    const auto it = overview_layout.find(ws_id);
    if (it == overview_layout.end())
        return scale->value();

    return it->second.box.w / monitor->m_transformedSize.x;
}

void HTLayoutColumn::init_position()
{
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    build_overview_layout(HT_VIEW_CLOSED);

    auto it = overview_layout.find(monitor->m_activeWorkspace->m_id);
    if (it != overview_layout.end())
    {
        offset->setValueAndWarp(-it->second.box.pos());
    }
    else
    {
        offset->setValueAndWarp({0, 0});
    }
    scale->setValueAndWarp(1.f);
}

CBox HTLayoutColumn::calculate_ws_box(int col_idx, int row_idx, HTViewStage stage)
{
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    rebuild_columns();

    const int num_cols = get_column_count();
    const int max_rows = get_max_rows();
    const float GAP_SIZE = HTConfig::value<Hyprlang::FLOAT>("gap_size") * monitor->m_scale;

    // if (GAP_SIZE > std::min(monitor->m_transformedSize.x, monitor->m_transformedSize.y) || GAP_SIZE < 0)
    //     fail_exit("Gap size {} induces invalid render dimensions", GAP_SIZE);

    double render_x = (monitor->m_transformedSize.x - GAP_SIZE * (num_cols + 1)) / num_cols;
    double render_y = (monitor->m_transformedSize.y - GAP_SIZE * (max_rows + 1)) / max_rows;
    const double mon_aspect = monitor->m_transformedSize.x / monitor->m_transformedSize.y;
    Vector2D start_offset{};

    // Make correct aspect ratio
    if (render_y * mon_aspect > render_x)
    {
        start_offset.y = (render_y - render_x / mon_aspect) * max_rows / 2.f;
        render_y = render_x / mon_aspect;
    }
    else if (render_x / mon_aspect > render_y)
    {
        start_offset.x = (render_x - render_y * mon_aspect) * num_cols / 2.f;
        render_x = render_y * mon_aspect;
    }

    float use_scale = scale->value();
    Vector2D use_offset = offset->value();
    if (stage == HT_VIEW_CLOSED)
    {
        use_scale = 1;
        use_offset = Vector2D{0, 0};
    }
    else if (stage == HT_VIEW_OPENED)
    {
        use_scale = render_x / monitor->m_transformedSize.x;
        use_offset = Vector2D{0, 0};
    }

    const Vector2D ws_sz = monitor->m_transformedSize * use_scale;
    return CBox{Vector2D{col_idx, row_idx} * (ws_sz + Vector2D{GAP_SIZE, GAP_SIZE}) + Vector2D{GAP_SIZE, GAP_SIZE} +
                    use_offset + start_offset,
                ws_sz};
}

void HTLayoutColumn::build_overview_layout(HTViewStage stage)
{
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    update_focus_state();

    rebuild_columns();
    overview_layout.clear();

    const PHLMONITOR last_monitor = Desktop::focusState()->monitor();
    Desktop::focusState()->rawMonitorFocus(monitor);

    for (size_t col_idx = 0; col_idx < columns.size(); col_idx++)
    {
        const auto &col = columns[col_idx];
        for (size_t row_idx = 0; row_idx < col.workspaces.size(); row_idx++)
        {
            WORKSPACEID ws_id = col.workspaces[row_idx];
            const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(ws_id);

            // Ensure workspace is on this monitor
            if (workspace != nullptr && workspace->monitorID() != view_id)
            {
                g_pCompositor->moveWorkspaceToMonitor(workspace, monitor);
            }

            const CBox ws_box = calculate_ws_box(col_idx, row_idx, stage);
            CBox scaled_box = apply_focus_scale(ws_box, ws_id, stage);
            scaled_box.round();
            overview_layout[ws_id] = HTWorkspace{(int)col_idx, (int)row_idx, scaled_box};
        }
    }

    if (last_monitor != nullptr)
        Desktop::focusState()->rawMonitorFocus(last_monitor);
}

// Runs in the render.pre hook, BEFORE the live monitor pass. Hyprland's
// recalculateMonitor only lays out the active workspace, so non-active workspaces
// keep stale window positions — in a scrolling layout their strip sits outside the
// monitor box and every window fails visibleOnMonitor, leaving a wallpaper-only tile.
// Recalc each shown workspace here (active swapped in transiently) so its windows are
// positioned; doing it before the live pass keeps the mutation out of render().
void HTLayoutColumn::prepare_workspaces()
{
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    rebuild_columns();

    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;
    if (start_workspace == nullptr)
        return;

    for (const auto &col : columns)
    {
        for (WORKSPACEID ws_id : col.workspaces)
        {
            const PHLWORKSPACE ws = g_pCompositor->getWorkspaceByID(ws_id);
            if (ws == nullptr || ws == start_workspace)
                continue;
            monitor->m_activeWorkspace = ws;
            g_layoutManager->recalculateMonitor(monitor,
                                                Layout::CLayoutManager::RECALCULATE_MONITOR_REASON_WORKSPACE_CHANGE);
        }
    }

    monitor->m_activeWorkspace = start_workspace;
}

// Runs inside Hyprland's live monitor pass (via the renderWorkspace hook). Draws each
// workspace's contents directly, scaled into its tile via render_workspace_at_box; the
// renderTexture/renderBorder hooks keep the per-surface scissor in sync with that scale
// so contents/borders aren't culled near tile edges.
void HTLayoutColumn::render()
{
    HTLayoutBase::render();
    CScopeGuard x([this] { post_render(); });

    const PHTVIEW par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return;
    const PHLMONITOR monitor = par_view->get_monitor();
    if (monitor == nullptr)
        return;

    static auto PACTIVECOL = CConfigValue<Config::IComplexConfigValue>("general:col.active_border");
    static auto PINACTIVECOL = CConfigValue<Config::IComplexConfigValue>("general:col.inactive_border");

    auto *const ACTIVECOL = (Config::CGradientValueData *)(PACTIVECOL.ptr());
    auto *const INACTIVECOL = (Config::CGradientValueData *)(PINACTIVECOL.ptr());

    const float BORDERSIZE = HTConfig::value<Hyprlang::FLOAT>("border_size");
    const auto time = Time::steadyNow();

    CBox monitor_box = {{0, 0}, monitor->m_transformedSize};

    CRectPassElement::SRectData data;
    data.color = CHyprColor{HTConfig::value<Hyprlang::INT>("bg_color")};
    data.box = monitor_box;
    g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(data));

    build_overview_layout(HT_VIEW_ANIMATING);

    // Hyprland only fully renders the active workspace; render_workspace_at_box swaps
    // each tile's workspace in as it renders, so capture the real active one to restore
    // at the end and to pick out the active-border color.
    const PHLWORKSPACE start_workspace = monitor->m_activeWorkspace;
    if (start_workspace == nullptr)
        return;
    g_pDesktopAnimationManager->startAnimation(start_workspace, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false,
                                               true);
    start_workspace->m_visible = false;

    CBox global_mon_box = {monitor->m_position, monitor->m_transformedSize};
    const auto tile_visible = [&](const CBox &box) {
        if (box.width < 0.01 || box.height < 0.01)
            return false;
        CBox global_box = {box.pos() + monitor->m_position, box.size()};
        return !global_box.expand(BORDERSIZE).intersection(global_mon_box).empty();
    };

    // Borders first, so window contents render on top of them (a window can extend past
    // its tile, e.g. one being dragged to a new workspace), unclipped by the borders.
    for (const auto &[ws_id, ws_layout] : overview_layout)
    {
        if (!tile_visible(ws_layout.box))
            continue;
        CBorderPassElement::SBorderData bdata;
        bdata.box = ws_layout.box;
        bdata.grad1 = start_workspace->m_id == ws_id ? *ACTIVECOL : *INACTIVECOL;
        bdata.borderSize = BORDERSIZE;
        g_pHyprRenderer->m_renderPass.add(makeUnique<CBorderPassElement>(bdata));
    }

    // Contents: plain tiles first, then focus-scaled tiles (ascending), then the active
    // workspace last. Focus-scaled tiles are enlarged past the gaps and overlap their
    // neighbors, so the most-emphasized / active tile must draw last to stay on top.
    std::vector<std::pair<float, WORKSPACEID>> scaled_tiles;
    for (const auto &[ws_id, ws_layout] : overview_layout)
    {
        if (!tile_visible(ws_layout.box) || ws_id == start_workspace->m_id)
            continue;
        if (focus_scale_for_id(ws_id, HT_VIEW_ANIMATING) != 1.f)
        {
            scaled_tiles.push_back({focus_scale_for_id(ws_id, HT_VIEW_ANIMATING), ws_id});
            continue;
        }
        render_workspace_at_box(monitor, g_pCompositor->getWorkspaceByID(ws_id), time, ws_layout.box);
    }
    std::sort(scaled_tiles.begin(), scaled_tiles.end());
    for (const auto &[fscale, ws_id] : scaled_tiles)
        render_workspace_at_box(monitor, g_pCompositor->getWorkspaceByID(ws_id), time, overview_layout[ws_id].box);

    if (const auto it = overview_layout.find(start_workspace->m_id);
        it != overview_layout.end() && tile_visible(it->second.box))
        render_workspace_at_box(monitor, start_workspace, time, it->second.box);

    monitor->m_activeWorkspace = start_workspace;
    g_pDesktopAnimationManager->startAnimation(start_workspace, CDesktopAnimationManager::ANIMATION_TYPE_IN, false,
                                               true);
    start_workspace->m_visible = true;

    g_pHyprRenderer->damageMonitor(monitor);

    // Dragged window rendered on top, following the cursor.
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;
    const SP<Layout::ITarget> target = g_layoutManager->dragController()->target();
    if (target == nullptr)
        return;
    const PHLWINDOW dragged_window = target->window();
    if (dragged_window == nullptr)
        return;
    const Vector2D mouse_coords = g_pInputManager->getMouseCoordsInternal();
    const CBox window_box = dragged_window->getWindowMainSurfaceBox()
                                .translate(-mouse_coords)
                                .scale(cursor_view->layout->drag_window_scale())
                                .translate(mouse_coords);
    if (!window_box.intersection(monitor->logicalBox()).empty())
        render_window_at_box(dragged_window, monitor, time, window_box);
}
