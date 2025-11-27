#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>

#include "view.hpp"

class HTManager {
  public:
    HTManager();

    std::vector<PHTVIEW> views;

    PHTVIEW get_view_from_monitor(PHLMONITOR pMonitor);
    PHTVIEW get_view_from_cursor();
    PHTVIEW get_view_from_id(VIEWID view_id);

    PHLWINDOW get_window_from_cursor(bool return_focused = true);

    void reset();

    void show_all_views();
    void hide_all_views();
    void show_cursor_view();

    bool start_window_drag();
    bool end_window_drag();
    bool exit_to_workspace();
    bool on_mouse_move();
    bool on_mouse_axis(double delta);

    // Drag state for window drag vs pan drag
    enum drag_state_t {
        HT_DRAG_NONE,
        HT_DRAG_WINDOW,  // Dragging a window
        HT_DRAG_PAN,     // Panning the overview (drag on empty space)
    };

    drag_state_t drag_state = HT_DRAG_NONE;
    Vector2D drag_start_pos;      // Mouse position when drag started
    Vector2D drag_start_offset;   // Offset value when drag started
    VIEWID drag_view_id = 0;      // View being dragged

    // Swipe state
    enum swipe_state_t {
        HT_SWIPE_OPEN,
        HT_SWIPE_MOVE,
        HT_SWIPE_NONE,
    };

    swipe_state_t swipe_state;
    float swipe_amt;
    void swipe_start();
    bool swipe_update(IPointer::SSwipeUpdateEvent e);
    bool swipe_end();

    bool has_active_view();
    bool cursor_view_active();
};
