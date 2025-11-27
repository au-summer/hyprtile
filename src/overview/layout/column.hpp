#pragma once

#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <map>
#include <vector>

#include "../types.hpp"
#include "layout_base.hpp"

class HTLayoutColumn : public HTLayoutBase {
  private:
    PHLANIMVAR<float> scale;
    PHLANIMVAR<Vector2D> offset;

    // Dynamic grid dimensions discovered from existing workspaces
    int num_columns = 0;  // Actual count of existing columns
    int max_rows = 0;     // Max height among all columns
    std::map<int, int> column_heights;    // column_number -> height
    std::vector<int> existing_columns;    // Sorted list of existing column numbers
    std::map<int, int> column_to_grid_x;  // column_number -> grid x position

    // Zoom state
    float base_scale = 1.f;   // Default overview scale (calculated from grid)
    float zoom_level = 1.f;   // Additional zoom multiplier (1.0 = no extra zoom)

    // Edge panning state
    timespec last_render_time = {0, 0};
    bool edge_pan_initialized = false;

    // Helper methods
    void apply_edge_pan(PHLMONITOR monitor, float dt);
    Vector2D clamp_offset_to_bounds(const Vector2D& off);

  public:
    HTLayoutColumn(VIEWID view_id);
    virtual ~HTLayoutColumn() = default;

    virtual std::string layout_name();

    virtual CBox calculate_ws_box(int x, int y, HTViewStage stage);

    virtual void close_open_lerp(float perc);
    virtual void on_show(CallbackFun on_complete);
    virtual void on_hide(CallbackFun on_complete);
    virtual void on_move(WORKSPACEID old_id, WORKSPACEID new_id, CallbackFun on_complete);
    virtual void on_move_swipe(Vector2D delta);
    virtual WORKSPACEID on_move_swipe_end();

    virtual bool on_mouse_axis(double delta) override;

    virtual WORKSPACEID get_ws_id_in_direction(int x, int y, std::string& direction);

    virtual bool should_render_window(PHLWINDOW window);
    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();

    // Helper to get workspace ID from column/index position
    WORKSPACEID get_ws_id_from_column_index(int column, int index);

    // Drag panning support
    virtual Vector2D get_current_offset() override;
    virtual void apply_drag_pan(const Vector2D& start_offset, const Vector2D& mouse_delta) override;
};
