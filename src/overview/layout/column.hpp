#pragma once

#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <map>

#include "../types.hpp"
#include "layout_base.hpp"

class HTLayoutColumn : public HTLayoutBase {
  private:
    PHLANIMVAR<float> scale;
    PHLANIMVAR<Vector2D> offset;

    // Dynamic grid dimensions discovered from existing workspaces
    int max_columns = 0;
    int max_rows = 0;  // Max height among all columns
    std::map<int, int> column_heights;  // column_number -> height

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

    virtual WORKSPACEID get_ws_id_in_direction(int x, int y, std::string& direction);

    virtual bool should_render_window(PHLWINDOW window);
    virtual float drag_window_scale();
    virtual void init_position();
    virtual void build_overview_layout(HTViewStage stage);
    virtual void render();

    // Helper to get workspace ID from column/index position
    WORKSPACEID get_ws_id_from_column_index(int column, int index);
};
