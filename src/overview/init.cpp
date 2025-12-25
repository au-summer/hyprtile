#include <linux/input-event-codes.h>

#include <ctime>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprlang.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "config.hpp"
#include "globals.hpp"
#include "overview.hpp"
#include "types.hpp"

#include "init.hpp"

// ========== Overview Dispatchers ==========

static SDispatchResult dispatch_if(std::string arg, bool is_active) {
    if (ht_manager == nullptr)
        return {.passEvent = true, .success = false, .error = "ht_manager is null"};
    PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.passEvent = true, .success = false, .error = "cursor_view is null"};
    if (cursor_view->active != is_active)
        return {.passEvent = true, .success = false, .error = "predicate not met"};

    const auto DISPATCHSTR = arg.substr(0, arg.find_first_of(' '));

    auto DISPATCHARG = std::string();
    if ((int)arg.find_first_of(' ') != -1)
        DISPATCHARG = arg.substr(arg.find_first_of(' ') + 1);

    const auto DISPATCHER = g_pKeybindManager->m_dispatchers.find(DISPATCHSTR);
    if (DISPATCHER == g_pKeybindManager->m_dispatchers.end())
        return {.success = false, .error = "invalid dispatcher"};

    SDispatchResult res = DISPATCHER->second(DISPATCHARG);

    Debug::log(
        LOG,
        "[Hyprtile Overview] passthrough dispatch: {} : {}{}",
        DISPATCHSTR,
        DISPATCHARG,
        res.success ? "" : " -> " + res.error
    );

    return res;
}

static SDispatchResult dispatch_if_not_active(std::string arg) {
    return dispatch_if(arg, false);
}

static SDispatchResult dispatch_if_active(std::string arg) {
    return dispatch_if(arg, true);
}

static SDispatchResult dispatch_toggle_view(std::string arg) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};

    if (arg == "all") {
        if (ht_manager->has_active_view())
            ht_manager->hide_all_views();
        else
            ht_manager->show_all_views();
    } else if (arg == "cursor") {
        if (ht_manager->cursor_view_active())
            ht_manager->hide_all_views();
        else
            ht_manager->show_cursor_view();
    } else {
        return {.success = false, .error = "invalid arg"};
    }
    return {};
}

static SDispatchResult dispatch_move(std::string arg) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};
    cursor_view->move(arg, false);
    return {};
}

static SDispatchResult dispatch_move_window(std::string arg) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};
    cursor_view->move(arg, true);
    return {};
}

static SDispatchResult dispatch_kill_hover(std::string arg) {
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};
    // Only use actually hovered window when overview is active
    // Use focused otherwise
    const PHLWINDOW hovered_window = ht_manager->get_window_from_cursor(!cursor_view->active);
    if (hovered_window == nullptr)
        return {.success = false, .error = "hovered_window is null"};
    g_pCompositor->closeWindow(hovered_window);
    return {};
}

// ========== Overview Hooks ==========

static void hook_render_workspace(
    void* thisptr,
    PHLMONITOR monitor,
    PHLWORKSPACE workspace,
    timespec* now,
    const CBox& geometry
) {
    if (ht_manager == nullptr) {
        ((render_workspace_t)(render_workspace_hook
                                  ->m_original))(thisptr, monitor, workspace, now, geometry);
        return;
    }
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    if ((view != nullptr && view->navigating) || ht_manager->has_active_view()) {
        view->layout->render();
    } else {
        ((render_workspace_t)(render_workspace_hook
                                  ->m_original))(thisptr, monitor, workspace, now, geometry);
    }
}

static bool hook_should_render_window(void* thisptr, PHLWINDOW window, PHLMONITOR monitor) {
    bool ori_result =
        ((should_render_window_t)(should_render_window_hook->m_original))(thisptr, window, monitor);
    if (ht_manager == nullptr || !ht_manager->has_active_view())
        return ori_result;
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    if (view == nullptr)
        return ori_result;
    return view->layout->should_render_window(window);
}

static uint32_t hook_is_solitary_blocked(void* thisptr, bool full) {
    PHTVIEW view = ht_manager->get_view_from_cursor();
    if (view == nullptr) {
        Debug::log(ERR, "[Hyprtile Overview] View is nullptr in hook_is_solitary_blocked");
        (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);
    }

    if (view->active || view->navigating) {
        return CMonitor::SC_UNKNOWN;
    }
    return (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);
}

// ========== Overview Mouse/Touch Callbacks ==========

static void on_mouse_button(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;

    const auto e = std::any_cast<IPointer::SButtonEvent>(args);
    const bool pressed = e.state == WL_POINTER_BUTTON_STATE_PRESSED;

    const unsigned int drag_button = HTConfig::value<Hyprlang::INT>("drag_button");
    const unsigned int select_button = HTConfig::value<Hyprlang::INT>("select_button");

    if (pressed && e.button == drag_button) {
        info.cancelled = ht_manager->start_window_drag();
    } else if (!pressed && e.button == drag_button) {
        info.cancelled = ht_manager->end_window_drag();
    } else if (pressed && e.button == select_button) {
        info.cancelled = ht_manager->exit_to_workspace();
    }
}

static void on_mouse_move(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->on_mouse_move();
}

static void on_mouse_axis(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    const auto e = std::any_cast<IPointer::SAxisEvent>(
        std::any_cast<std::unordered_map<std::string, std::any>>(args)["event"]
    );
    info.cancelled = ht_manager->on_mouse_axis(e.delta);
}

static void on_swipe_begin(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    ht_manager->swipe_start();
}

static void on_swipe_update(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    auto e = std::any_cast<IPointer::SSwipeUpdateEvent>(args);
    info.cancelled = ht_manager->swipe_update(e);
}

static void on_swipe_end(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->swipe_end();
}

static void cancel_event(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr || !ht_manager->cursor_view_active())
        return;
    info.cancelled = true;
}

static void on_key_press(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;

    // Check if any view is active
    if (!ht_manager->has_active_view())
        return;

    const auto event_map = std::any_cast<std::unordered_map<std::string, std::any>>(args);
    const auto event = std::any_cast<IKeyboard::SKeyEvent>(event_map.at("event"));

    // Only handle key press, not release
    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    // Check for Escape key
    if (event.keycode == KEY_ESC) {
        ht_manager->hide_all_views();
        info.cancelled = true;
    }
}

// ========== Monitor Registration ==========

static void register_monitors() {
    if (ht_manager == nullptr)
        return;
    for (const PHLMONITOR& monitor : g_pCompositor->m_monitors) {
        const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
        if (view != nullptr) {
            if (!view->active)
                view->layout->init_position();
            continue;
        }
        ht_manager->views.push_back(makeShared<HTView>(monitor->m_id));

        Debug::log(
            LOG,
            "[Hyprtile Overview] Registering view for monitor {} with resolution {}x{}",
            monitor->m_description,
            monitor->m_transformedSize.x,
            monitor->m_transformedSize.y
        );
    }
}

static void on_config_reloaded(void* thisptr, SCallbackInfo& info, std::any args) {
    if (ht_manager == nullptr)
        return;

    // re-init scale and offset for inactive views, change layout if changed
    for (PHTVIEW& view : ht_manager->views) {
        if (view == nullptr)
            continue;
        const Hyprlang::STRING new_layout = HTConfig::value<Hyprlang::STRING>("layout");
        if (HTConfig::value<Hyprlang::INT>("close_overview_on_reload")
            || view->layout->layout_name() != new_layout) {
            Debug::log(LOG, "[Hyprtile Overview] Closing overview on config reload");
            view->hide(false);
            view->change_layout(new_layout);
        }
    }
}

// ========== Initialization Functions ==========

static void init_functions() {
    bool success = true;

    static auto FNS1 = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWorkspace");
    if (FNS1.empty())
        fail_exit("No renderWorkspace!");
    render_workspace_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS1[0].address, (void*)hook_render_workspace);
    Debug::log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS1[0].signature);
    success = render_workspace_hook->hook();

    static auto FNS2 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN13CHyprRenderer18shouldRenderWindowEN9Hyprutils6Memory14CS"
        "haredPointerI7CWindowEENS2_I8CMonitorEE"
    );
    if (FNS2.empty())
        fail_exit("No shouldRenderWindow");
    should_render_window_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS2[0].address, (void*)hook_should_render_window);
    Debug::log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS2[0].signature);
    success = should_render_window_hook->hook() && success;

    static auto FNS3 = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWindow");
    if (FNS3.empty())
        fail_exit("No renderWindow");
    render_window = FNS3[0].address;

    static auto FNS4 = HyprlandAPI::findFunctionsByName(PHANDLE, "isSolitaryBlocked");
    if (FNS4.empty())
        fail_exit("No isSolitaryBlocked!");

    is_solitary_blocked_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS4[0].address, (void*)hook_is_solitary_blocked);
    Debug::log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS4[0].signature);
    success = is_solitary_blocked_hook->hook() && success;

    if (!success)
        fail_exit("Failed initializing hooks");
}

static void register_callbacks() {
    static auto P1 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseButton", on_mouse_button);
    static auto P2 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseMove", on_mouse_move);
    static auto P3 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseAxis", on_mouse_axis);

    // Keyboard: close overview on Escape
    static auto P4 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "keyPress", on_key_press);

    // TODO: support touch
    static auto P5 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchDown", cancel_event);
    static auto P6 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchUp", cancel_event);
    static auto P7 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchMove", cancel_event);

    static auto P8 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeBegin", on_swipe_begin);
    static auto P9 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeUpdate", on_swipe_update);
    static auto P10 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeEnd", on_swipe_end);

    static auto P11 =
        HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", on_config_reloaded);

    static auto P12 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE,
        "monitorAdded",
        [&](void* thisptr, SCallbackInfo& info, std::any data) { register_monitors(); }
    );
}

static void add_dispatchers() {
    // Main expo toggle dispatcher
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:expo", dispatch_toggle_view);
    
    // Conditional dispatchers
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:expo:if_not_active", dispatch_if_not_active);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:expo:if_active", dispatch_if_active);
    
    // Navigation dispatchers
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:expo:move", dispatch_move);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:expo:movewindow", dispatch_move_window);
    
    // Utility dispatchers
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:expo:killhovered", dispatch_kill_hover);
}

static void init_config() {
    // Layout selection - default to column for hyprtile
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:layout", Hyprlang::STRING {"column"});

    // general
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:bg_color", Hyprlang::INT {0xFF111111});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gap_size", Hyprlang::FLOAT {10.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:border_size", Hyprlang::FLOAT {4.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:exit_on_hovered", Hyprlang::INT {0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:warp_on_move_window", Hyprlang::INT {1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:close_overview_on_reload", Hyprlang::INT {1});

    // Mouse buttons
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:drag_button", Hyprlang::INT {BTN_LEFT});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:select_button", Hyprlang::INT {BTN_RIGHT});

    // Gesture settings
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:enabled", Hyprlang::INT {1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:move_fingers", Hyprlang::INT {3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:move_distance", Hyprlang::FLOAT {300.0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:open_fingers", Hyprlang::INT {4});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:open_distance", Hyprlang::FLOAT {300.0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:open_positive", Hyprlang::INT {1});

    // Grid layout specific (kept for compatibility)
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:grid:rows", Hyprlang::INT {3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:grid:cols", Hyprlang::INT {3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:grid:loop", Hyprlang::INT {0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:grid:gaps_use_aspect_ratio", Hyprlang::INT {0});

    // Linear layout specific (kept for compatibility)
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:linear:blur", Hyprlang::INT {1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:linear:height", Hyprlang::FLOAT {300.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:linear:scroll_speed", Hyprlang::FLOAT {1.f});
}

// ========== Public API ==========

namespace overview {

void init() {
    Debug::log(LOG, "[Hyprtile Overview] Initializing overview module...");

    // Initialize manager
    if (ht_manager == nullptr)
        ht_manager = std::make_unique<HTManager>();
    else
        ht_manager->reset();

    // Initialize config, dispatchers, callbacks, hooks, monitors
    init_config();
    add_dispatchers();
    register_callbacks();
    init_functions();
    register_monitors();

    Debug::log(LOG, "[Hyprtile Overview] Overview module initialized");
}

void exit() {
    Debug::log(LOG, "[Hyprtile Overview] Cleaning up overview module...");

    if (ht_manager)
        ht_manager->reset();

    Debug::log(LOG, "[Hyprtile Overview] Overview module cleaned up");
}

} // namespace overview
