#include <linux/input-event-codes.h>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/animation/AnimatedVariable.hpp>

#include "dispatchers.h"
#include "globals.h"
#include "utils.h"

// Overview module includes
#include "overview/config.hpp"
#include "overview/globals.hpp"
#include "overview/types.hpp"
#include "overview/view.hpp"

APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

char anim_type = '\0';

// ===== Hyprtile Animation Hooks =====

inline CFunctionHook *g_pChangeWorkspaceHook = nullptr;
typedef void (*origChangeWorkspace)(CMonitor *, const PHLWORKSPACE &, bool, bool, bool);
void hk_changeWorkspace(CMonitor *thisptr, const PHLWORKSPACE &pWorkspace, bool internal, bool noMouseMove,
                        bool noFocus)
{
    const std::string &current_workspace_name = thisptr->m_activeWorkspace->m_name;
    const std::string &target_workspace_name = pWorkspace->m_name;

    int current_column = name_to_column(current_workspace_name);
    int current_index = name_to_index(current_workspace_name);
    int target_column = name_to_column(target_workspace_name);
    int target_index = name_to_index(target_workspace_name);

    if (current_column == target_column)
    {
        if (current_index < target_index)
        {
            anim_type = 'd';
        }
        else
        {
            anim_type = 'u';
        }
    }
    else
    {
        if (current_column < target_column)
        {
            anim_type = 'r';
        }
        else
        {
            anim_type = 'l';
        }
    }

    (*(origChangeWorkspace)g_pChangeWorkspaceHook->m_original)(thisptr, pWorkspace, internal, noMouseMove, noFocus);

    anim_type = '\0';
}

inline CFunctionHook *g_pStartAnimationHook = nullptr;
typedef void (*origStartAnimation)(CDesktopAnimationManager *, PHLWORKSPACE, CDesktopAnimationManager::eAnimationType,
                                   bool, bool);
void hk_startAnimation(CDesktopAnimationManager *thisptr, PHLWORKSPACE ws,
                       CDesktopAnimationManager::eAnimationType type, bool left, bool instant)
{
    auto config = ws->m_alpha->getConfig();
    auto &style = config->pValues->internalStyle;
    auto original_style = config->pValues->internalStyle;

    switch (anim_type)
    {
    case 'l':
        left = false;
        style = "slide";
        break;
    case 'r':
        left = true;
        style = "slide";
        break;
    case 'u':
        left = false;
        style = "slidevert";
        break;
    case 'd':
        left = true;
        style = "slidevert";
        break;
    }

    (*(origStartAnimation)g_pStartAnimationHook->m_original)(thisptr, ws, type, left, instant);

    style = original_style;
}

inline CFunctionHook *g_pFindAvailableDefaultWSHook = nullptr;
typedef WORKSPACEID (*origFindAvailableDefaultWS)(CMonitor *);
WORKSPACEID hk_findAvailableDefaultWS(CMonitor *thisptr)
{
    // Since there are only few workspaces, brute force
    for (WORKSPACEID i = 1; i < LONG_MAX; ++i)
    {
        bool found = false;
        for (const auto &workspace : g_pCompositor->getWorkspaces())
        {
            int column = name_to_column(workspace->m_name);

            if (i == column)
            {
                found = true;
                break;
            }
        }

        if (!found)
            return i;
    }

    return LONG_MAX;
}

// ===== Overview Mode Hooks =====

static void hook_render_workspace(void *thisptr, PHLMONITOR monitor, PHLWORKSPACE workspace, timespec *now,
                                  const CBox &geometry)
{
    if (ht_manager == nullptr)
    {
        ((render_workspace_t)(render_workspace_hook->m_original))(thisptr, monitor, workspace, now, geometry);
        return;
    }
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    if ((view != nullptr && view->navigating) || ht_manager->has_active_view())
    {
        view->layout->render();
    }
    else
    {
        ((render_workspace_t)(render_workspace_hook->m_original))(thisptr, monitor, workspace, now, geometry);
    }
}

static bool hook_should_render_window(void *thisptr, PHLWINDOW window, PHLMONITOR monitor)
{
    bool ori_result = ((should_render_window_t)(should_render_window_hook->m_original))(thisptr, window, monitor);
    if (ht_manager == nullptr || !ht_manager->has_active_view())
        return ori_result;
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    if (view == nullptr)
        return ori_result;
    return view->layout->should_render_window(window);
}

static uint32_t hook_is_solitary_blocked(void *thisptr, bool full)
{
    PHTVIEW view = ht_manager->get_view_from_cursor();
    if (view == nullptr)
    {
        Debug::log(ERR, "[Hyprtile] View is nullptr in hook_is_solitary_blocked");
        return (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);
    }

    if (view->active || view->navigating)
    {
        return CMonitor::SC_UNKNOWN;
    }
    return (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);
}

// ===== Overview Input Callbacks =====

static void on_mouse_button(void *thisptr, SCallbackInfo &info, std::any args)
{
    if (ht_manager == nullptr)
        return;

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;

    const auto e = std::any_cast<IPointer::SButtonEvent>(args);
    const bool pressed = e.state == WL_POINTER_BUTTON_STATE_PRESSED;

    const unsigned int drag_button = HyprtileConfig::value<Hyprlang::INT>("drag_button");
    const unsigned int select_button = HyprtileConfig::value<Hyprlang::INT>("select_button");

    if (pressed && e.button == drag_button)
    {
        info.cancelled = ht_manager->start_window_drag();
    }
    else if (!pressed && e.button == drag_button)
    {
        info.cancelled = ht_manager->end_window_drag();
    }
    else if (pressed && e.button == select_button)
    {
        info.cancelled = ht_manager->exit_to_workspace();
    }
}

static void on_mouse_move(void *thisptr, SCallbackInfo &info, std::any args)
{
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->on_mouse_move();
}

static void on_mouse_axis(void *thisptr, SCallbackInfo &info, std::any args)
{
    if (ht_manager == nullptr)
        return;
    const auto e =
        std::any_cast<IPointer::SAxisEvent>(std::any_cast<std::unordered_map<std::string, std::any>>(args)["event"]);
    info.cancelled = ht_manager->on_mouse_axis(e.delta);
}

static void on_swipe_begin(void *thisptr, SCallbackInfo &info, std::any args)
{
    if (ht_manager == nullptr)
        return;
    ht_manager->swipe_start();
}

static void on_swipe_update(void *thisptr, SCallbackInfo &info, std::any args)
{
    if (ht_manager == nullptr)
        return;
    auto e = std::any_cast<IPointer::SSwipeUpdateEvent>(args);
    info.cancelled = ht_manager->swipe_update(e);
}

static void on_swipe_end(void *thisptr, SCallbackInfo &info, std::any args)
{
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->swipe_end();
}

static void cancel_event(void *thisptr, SCallbackInfo &info, std::any args)
{
    if (ht_manager == nullptr || !ht_manager->cursor_view_active())
        return;
    info.cancelled = true;
}

// ===== Overview Helper Functions =====

static void register_monitors()
{
    if (ht_manager == nullptr)
        return;
    for (const PHLMONITOR &monitor : g_pCompositor->m_monitors)
    {
        const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
        if (view != nullptr)
        {
            if (!view->active)
                view->layout->init_position();
            continue;
        }
        ht_manager->views.push_back(makeShared<HTView>(monitor->m_id));

        Debug::log(LOG, "[Hyprtile] Registering view for monitor {} with resolution {}x{}", monitor->m_description,
                   monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    }
}

static void on_config_reloaded(void *thisptr, SCallbackInfo &info, std::any args)
{
    if (ht_manager == nullptr)
        return;

    // re-init scale and offset for inactive views
    for (PHTVIEW &view : ht_manager->views)
    {
        if (view == nullptr)
            continue;
        if (HyprtileConfig::value<Hyprlang::INT>("close_overview_on_reload"))
        {
            Debug::log(LOG, "[Hyprtile] Closing overview on config reload");
            view->hide(false);
        }
    }
}

static void init_overview_functions()
{
    bool success = true;

    static auto FNS1 = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWorkspace");
    if (FNS1.empty())
    {
        Debug::log(ERR, "[Hyprtile] No renderWorkspace!");
        return;
    }
    render_workspace_hook = HyprlandAPI::createFunctionHook(PHANDLE, FNS1[0].address, (void *)hook_render_workspace);
    Debug::log(LOG, "[Hyprtile] Attempting hook {}", FNS1[0].signature);
    success = render_workspace_hook->hook();

    static auto FNS2 =
        HyprlandAPI::findFunctionsByName(PHANDLE, "_ZN13CHyprRenderer18shouldRenderWindowEN9Hyprutils6Memory14CS"
                                                  "haredPointerI7CWindowEENS2_I8CMonitorEE");
    if (FNS2.empty())
    {
        Debug::log(ERR, "[Hyprtile] No shouldRenderWindow");
        return;
    }
    should_render_window_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS2[0].address, (void *)hook_should_render_window);
    Debug::log(LOG, "[Hyprtile] Attempting hook {}", FNS2[0].signature);
    success = should_render_window_hook->hook() && success;

    static auto FNS3 = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWindow");
    if (FNS3.empty())
    {
        Debug::log(ERR, "[Hyprtile] No renderWindow");
        return;
    }
    render_window = FNS3[0].address;

    static auto FNS4 = HyprlandAPI::findFunctionsByName(PHANDLE, "isSolitaryBlocked");
    if (FNS4.empty())
    {
        Debug::log(ERR, "[Hyprtile] No isSolitaryBlocked!");
        return;
    }

    is_solitary_blocked_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS4[0].address, (void *)hook_is_solitary_blocked);
    Debug::log(LOG, "[Hyprtile] Attempting hook {}", FNS4[0].signature);
    success = is_solitary_blocked_hook->hook() && success;

    if (!success)
        Debug::log(ERR, "[Hyprtile] Failed initializing some overview hooks");
}

static void register_overview_callbacks()
{
    static auto P1 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseButton", on_mouse_button);
    static auto P2 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseMove", on_mouse_move);
    static auto P3 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "mouseAxis", on_mouse_axis);

    // TODO: support touch
    static auto P4 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchDown", cancel_event);
    static auto P5 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchUp", cancel_event);
    static auto P6 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "touchMove", cancel_event);

    static auto P7 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeBegin", on_swipe_begin);
    static auto P8 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeUpdate", on_swipe_update);
    static auto P9 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "swipeEnd", on_swipe_end);

    static auto P10 = HyprlandAPI::registerCallbackDynamic(PHANDLE, "configReloaded", on_config_reloaded);

    static auto P11 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "monitorAdded", [&](void *thisptr, SCallbackInfo &info, std::any data) { register_monitors(); });
}

static void init_overview_config()
{
    // General overview settings
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:bg_color", Hyprlang::INT{0x000000FF});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:gap_size", Hyprlang::FLOAT{8.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:border_size", Hyprlang::FLOAT{4.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:exit_on_hovered", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:warp_on_move_window", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:close_overview_on_reload", Hyprlang::INT{1});

    // Mouse buttons
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:drag_button", Hyprlang::INT{BTN_LEFT});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:select_button", Hyprlang::INT{BTN_RIGHT});

    // Gesture settings
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:gestures:enabled", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:gestures:move_fingers", Hyprlang::INT{3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:gestures:move_distance", Hyprlang::FLOAT{300.0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:gestures:open_fingers", Hyprlang::INT{4});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:gestures:open_distance", Hyprlang::FLOAT{300.0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:overview:gestures:open_positive", Hyprlang::INT{1});
}

// ===== Plugin Entry Points =====

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;

    // const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    // const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    //
    // // ALWAYS add this to your plugins. It will prevent random crashes coming from
    // // mismatched header versions.
    // if (COMPOSITOR_HASH != CLIENT_HASH)
    // {
    //     HyprlandAPI::addNotification(PHANDLE,
    //                                  "[hyprtile] Mismatched headers! Can't proceed.\nCompositor Hash: " +
    //                                      COMPOSITOR_HASH + "\nClient Hash: " + CLIENT_HASH,
    //                                  CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
    //     throw std::runtime_error("[hyprtile] Version mismatch\nCompositor Hash: " + COMPOSITOR_HASH +
    //                              "\nClient Hash: " + CLIENT_HASH);
    // }

    // Initialize overview manager
    if (ht_manager == nullptr)
        ht_manager = std::make_unique<HTManager>();
    else
        ht_manager->reset();

    // Initialize overview config
    init_overview_config();

    // Hyprtile Function Hooks
    static const auto CHANGE_WORKSPACE = HyprlandAPI::findFunctionsByName(PHANDLE, "changeWorkspace");
    g_pChangeWorkspaceHook =
        HyprlandAPI::createFunctionHook(PHANDLE, CHANGE_WORKSPACE[1].address, (void *)&hk_changeWorkspace);
    g_pChangeWorkspaceHook->hook();

    static const auto START_ANIMATION = HyprlandAPI::findFunctionsByName(PHANDLE, "startAnimation");
    g_pStartAnimationHook =
        HyprlandAPI::createFunctionHook(PHANDLE, START_ANIMATION[0].address, (void *)&hk_startAnimation);
    g_pStartAnimationHook->hook();

    static const auto FIND_AVAILABLE_DEFAULT_WS = HyprlandAPI::findFunctionsByName(PHANDLE, "findAvailableDefaultWS");
    g_pFindAvailableDefaultWSHook = HyprlandAPI::createFunctionHook(PHANDLE, FIND_AVAILABLE_DEFAULT_WS[0].address,
                                                                    (void *)&hk_findAvailableDefaultWS);
    g_pFindAvailableDefaultWSHook->hook();

    // Overview Function Hooks
    init_overview_functions();

    // Register callbacks for overview input
    register_overview_callbacks();

    // Dispatchers
    dispatchers::addDispatchers();

    HyprlandAPI::reloadConfig();

    // Register monitors for overview
    register_monitors();

    Debug::log(LOG, "[Hyprtile] Plugin initialized with overview support");

    return {"hyprtile", "tiled workspace management with overview", "ausummer", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    Debug::log(LOG, "[Hyprtile] Plugin exiting");

    if (ht_manager)
        ht_manager->reset();
}
