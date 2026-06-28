#include <linux/input-event-codes.h>

#include <cmath>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/devices/IKeyboard.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/macros.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/layout/LayoutManager.hpp>
#include <hyprland/src/managers/PointerManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/plugins/PluginSystem.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprlang.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

#include "config.hpp"
#include "globals.hpp"
#include "overview.hpp"
#include "types.hpp"

#include "init.hpp"

// Store event listeners to prevent them from being destroyed
static std::vector<std::any> g_eventListeners;

static void register_monitors();

// ========== Overview Dispatchers ==========

static SDispatchResult dispatch_if(std::string arg, bool is_active)
{
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

    Log::logger->log(
        LOG,
        "[Hyprtile Overview] passthrough dispatch: {} : {}{}",
        DISPATCHSTR,
        DISPATCHARG,
        res.success ? "" : " -> " + res.error
    );

    return res;
}

static SDispatchResult dispatch_if_not_active(std::string arg)
{
    return dispatch_if(arg, false);
}

static SDispatchResult dispatch_if_active(std::string arg)
{
    return dispatch_if(arg, true);
}

static SDispatchResult dispatch_toggle_view(std::string arg)
{
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};

    // Monitors that weren't initialized when the plugin loaded (e.g. the built-in panel at
    // startup) are skipped by register_monitors() and never re-fire monitor.added, leaving them
    // without a view. Register lazily here so the overview always has a view to show.
    register_monitors();

    if (arg == "all")
    {
        if (ht_manager->has_active_view())
            ht_manager->hide_all_views();
        else
            ht_manager->show_all_views();
    }
    else if (arg == "cursor")
    {
        if (ht_manager->cursor_view_active())
            ht_manager->hide_all_views();
        else
            ht_manager->show_cursor_view();
    }
    else
    {
        return {.success = false, .error = "invalid arg"};
    }
    return {};
}

static SDispatchResult dispatch_move(std::string arg)
{
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};
    cursor_view->move(arg, false);
    return {};
}

static SDispatchResult dispatch_move_window(std::string arg)
{
    if (ht_manager == nullptr)
        return {.success = false, .error = "ht_manager is null"};
    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return {.success = false, .error = "cursor_view is null"};
    cursor_view->move(arg, true);
    return {};
}

static SDispatchResult dispatch_kill_hover(std::string arg)
{
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
    Config::Actions::closeWindow(hovered_window);
    return {};
}

// ========== Overview Hooks ==========

static void hook_render_workspace(void *thisptr, PHLMONITOR monitor, PHLWORKSPACE workspace, const Time::steady_tp &now,
                                  const CBox &geometry)
{
    if (ht_manager == nullptr)
    {
        ((render_workspace_t)(render_workspace_hook->m_original))(thisptr, monitor, workspace, now, geometry);
        return;
    }
    // view can legitimately be null (monitor registered after plugin load); fall
    // through to the original instead of dereferencing when another view is active
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    if (view != nullptr && (view->navigating || ht_manager->has_active_view()))
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
    // No manager/view for the cursor monitor (e.g. during teardown): defer to Hyprland.
    // Guarding ht_manager too — dereferencing it here would crash on monitor unplug.
    PHTVIEW view = ht_manager == nullptr ? nullptr : ht_manager->get_view_from_cursor();
    if (view == nullptr)
        return (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);

    if (view->active || view->navigating)
    {
        return CMonitor::SC_UNKNOWN;
    }
    return (*(origIsSolitaryBlocked)is_solitary_blocked_hook->m_original)(thisptr, full);
}

// ========== Overview Mouse/Touch Callbacks ==========

static void on_mouse_button(IPointer::SButtonEvent e, Event::SCallbackInfo& info)
{
    if (ht_manager == nullptr)
        return;

    const PHTVIEW cursor_view = ht_manager->get_view_from_cursor();
    if (cursor_view == nullptr)
        return;

    const bool pressed = e.state == WL_POINTER_BUTTON_STATE_PRESSED;

    const unsigned int drag_button = HTConfig::value<Hyprlang::INT>("drag_button");
    const unsigned int select_button = HTConfig::value<Hyprlang::INT>("select_button");

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

static void on_mouse_move(Vector2D c, Event::SCallbackInfo& info) 
{
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->on_mouse_move();
}

static void on_mouse_axis(IPointer::SAxisEvent e, Event::SCallbackInfo& info)
{
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->on_mouse_axis(e.delta);
}

static void on_swipe_begin(IPointer::SSwipeBeginEvent e, Event::SCallbackInfo& info)
{
    if (ht_manager == nullptr)
        return;
    ht_manager->swipe_start();
}

static void on_swipe_update(IPointer::SSwipeUpdateEvent e, Event::SCallbackInfo& info)
{
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->swipe_update(e);
}

static void on_swipe_end(IPointer::SSwipeEndEvent e, Event::SCallbackInfo& info)
{
    if (ht_manager == nullptr)
        return;
    info.cancelled = ht_manager->swipe_end();
}

static void cancel_event(Event::SCallbackInfo& info)
{
    if (ht_manager == nullptr || !ht_manager->cursor_view_active())
        return;
    info.cancelled = true;
}

static void on_key_press(IKeyboard::SKeyEvent event, Event::SCallbackInfo &info)
{
    if (ht_manager == nullptr)
        return;

    // Check if any view is active
    if (!ht_manager->has_active_view())
        return;

    // Only handle key press, not release
    if (event.state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    // Check for Escape key and Enter key
    if (event.keycode == KEY_ESC || event.keycode == KEY_ENTER)
    {
        ht_manager->hide_all_views();
        info.cancelled = true;
    }
}

// ========== Monitor Registration ==========

static void register_monitors()
{
    if (ht_manager == nullptr)
        return;
    for (const PHLMONITOR &monitor : g_pCompositor->m_monitors)
    {
        // Skip monitors that haven't finished initializing
        if (monitor->m_transformedSize.x < 1 || monitor->m_transformedSize.y < 1)
            continue;

        const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
        if (view != nullptr)
        {
            if (!view->active)
                view->layout->init_position();
            continue;
        }
        ht_manager->views.push_back(makeShared<HTView>(monitor->m_id));

        Log::logger->log(
            LOG,
            "[Hyprtile Overview] Registering view for monitor {} with resolution {}x{}",
            monitor->m_description,
            monitor->m_transformedSize.x,
            monitor->m_transformedSize.y
        );
    }
}

static void on_monitor_removed(PHLMONITOR monitor)
{
    if (ht_manager == nullptr || monitor == nullptr)
        return;
    ht_manager->remove_view_for_monitor_id(monitor->m_id);
}

static void on_config_reloaded()
{
    if (ht_manager == nullptr)
        return;

    // re-init scale and offset for inactive views, change layout if changed
    for (PHTVIEW &view : ht_manager->views)
    {
        if (view == nullptr)
            continue;
        const Hyprlang::STRING new_layout = HTConfig::value<Hyprlang::STRING>("layout");
        if (HTConfig::value<Hyprlang::INT>("close_overview_on_reload") || view->layout->layout_name() != new_layout)
        {
            Log::logger->log(LOG, "[Hyprtile Overview] Closing overview on config reload");
            view->hide(false);
            view->change_layout(new_layout);
        }
    }
}

// Fires once per monitor BEFORE Hyprland opens that monitor's main render pass.
// While the overview is shown, lay out each workspace's windows (recalculateMonitor
// only touches the active one, so non-active workspaces would keep stale positions and
// render blank in a scrolling layout); doing it here keeps the mutation off the live pass.
static void on_render_pre(PHLMONITOR monitor)
{
    if (ht_manager == nullptr || monitor == nullptr)
        return;
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    if (view == nullptr)
        return;
    // Mirror hook_render_workspace's guard for when the overview is visible.
    if (view->navigating || ht_manager->has_active_view())
        view->layout->prepare_workspaces();
}

// ========== Anti-culling render hooks ==========
//
// The direct scaled render draws each workspace into its tile via a renderModif, which
// Hyprland applies to texture/border QUADS but NOT to the per-surface scissor (derived
// from the untransformed window box). The hooks below keep the scissor in sync with the
// active renderModif during our scaled renders, so contents/borders aren't culled at
// tile edges, and force the fresh blur path. Ported from hyprtasking main.

typedef void (*render_texture_t)(void *thisptr, SP<Render::ITexture> tex, const CBox &box,
                                 Render::GL::CHyprOpenGLImpl::STextureRenderData data);
typedef void (*render_border_t)(void *thisptr, const CBox &box, const Config::CGradientValueData &grad,
                                Render::GL::CHyprOpenGLImpl::SBorderRenderData data);
typedef void (*render_border2_t)(void *thisptr, const CBox &box, const Config::CGradientValueData &grad1,
                                 const Config::CGradientValueData &grad2, float lerp,
                                 Render::GL::CHyprOpenGLImpl::SBorderRenderData data);
typedef bool (*blur_optimizations_t)(void *thisptr, PHLLS pLayer, PHLWINDOW pWindow);

// True while a tile / dragged-window is being rendered through an active renderModif.
static bool render_modif_scaled()
{
    auto &render_modif = g_pHyprRenderer->m_renderData.renderModif;
    return render_modif.enabled && !render_modif.modifs.empty();
}

// True only while hyprtile is itself driving a scaled render (overview open, or a view
// animating open/closed on this monitor). Native renderModif paths are left untouched.
static bool ht_scaled_render()
{
    if (ht_manager == nullptr || !render_modif_scaled())
        return false;
    if (ht_manager->has_active_view())
        return true;
    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (monitor == nullptr)
        return false;
    const PHTVIEW view = ht_manager->get_view_from_monitor(monitor);
    return view != nullptr && view->navigating;
}

// Drop the per-surface clip regions and scissor to the whole monitor so scaled
// surfaces are never clipped at their tile edges. Blur path pre-bakes the transform.
static void hook_render_texture(void *thisptr, SP<Render::ITexture> tex, const CBox &box,
                                Render::GL::CHyprOpenGLImpl::STextureRenderData data)
{
    auto &render_data = g_pHyprRenderer->m_renderData;
    auto &render_modif = render_data.renderModif;

    if (!ht_scaled_render() || render_data.pMonitor == nullptr)
    {
        ((render_texture_t)(render_texture_hook->m_original))(thisptr, tex, box, data);
        return;
    }

    CRegion full_damage;
    full_damage = CBox{0, 0, render_data.pMonitor->m_transformedSize.x, render_data.pMonitor->m_transformedSize.y};
    data.damage = &full_damage;
    data.clipRegion = {};
    const CBox saved_clip_box = render_data.clipBox;
    render_data.clipBox = CBox{};

    if (data.blur)
    {
        // The blur UVs stay untransformed, so pre-bake the transform into the box and
        // disable the renderModif so the quad and its UVs agree on the tile.
        CBox tbox = box;
        render_modif.applyToBox(tbox);
        const auto saved_modif = render_modif;
        render_modif.enabled = false;
        ((render_texture_t)(render_texture_hook->m_original))(thisptr, tex, tbox, data);
        render_modif = saved_modif;
    }
    else
    {
        ((render_texture_t)(render_texture_hook->m_original))(thisptr, tex, box, data);
    }

    render_data.clipBox = saved_clip_box;
}

// renderBorder scissors with the untransformed box, which swallows the transformed ring.
// Pre-bake the transform into the box + border size, then disable the renderModif.
template <typename Fn>
static void render_border_scaled(CBox &box, Render::GL::CHyprOpenGLImpl::SBorderRenderData &data, Fn &&call_original)
{
    auto &render_modif = g_pHyprRenderer->m_renderData.renderModif;
    if (!ht_scaled_render())
    {
        call_original();
        return;
    }
    render_modif.applyToBox(box);
    data.borderSize = std::round(data.borderSize * render_modif.combinedScale());
    const auto saved_modif = render_modif;
    render_modif.enabled = false;
    call_original();
    render_modif = saved_modif;
}

static void hook_render_border(void *thisptr, const CBox &box, const Config::CGradientValueData &grad,
                               Render::GL::CHyprOpenGLImpl::SBorderRenderData data)
{
    CBox tbox = box;
    render_border_scaled(tbox, data, [&] {
        ((render_border_t)(render_border_hook->m_original))(thisptr, tbox, grad, data);
    });
}

static void hook_render_border2(void *thisptr, const CBox &box, const Config::CGradientValueData &grad1,
                                const Config::CGradientValueData &grad2, float lerp,
                                Render::GL::CHyprOpenGLImpl::SBorderRenderData data)
{
    CBox tbox = box;
    render_border_scaled(tbox, data, [&] {
        ((render_border2_t)(render_border2_hook->m_original))(thisptr, tbox, grad1, grad2, lerp, data);
    });
}

// Force the fresh blur path during scaled renders so each window's blur is taken from
// the current framebuffer (the overview as drawn so far), clipped to its tile.
static bool hook_blur_optimizations(void *thisptr, PHLLS pLayer, PHLWINDOW pWindow)
{
    if (ht_scaled_render())
        return false;
    return ((blur_optimizations_t)(blur_optimizations_hook->m_original))(thisptr, pLayer, pWindow);
}

// ========== Initialization Functions ==========

static void init_functions()
{
    bool success = true;

    static auto FNS1 = HyprlandAPI::findFunctionsByName(PHANDLE, "renderWorkspace");
    if (FNS1.empty())
        fail_exit("No renderWorkspace!");
    render_workspace_hook = HyprlandAPI::createFunctionHook(PHANDLE, FNS1[0].address, (void *)hook_render_workspace);
    Log::logger->log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS1[0].signature);
    success = render_workspace_hook->hook();

    // Specific renderTexture overload taking STextureRenderData (several functions share
    // the "renderTexture" name). Keeps the per-surface scissor in sync with the renderModif.
    static auto FNS_RT = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render2GL15CHyprOpenGLImpl13renderTextureEN9Hyprutils6Memory14CSharedPointerINS_8ITextureEEERKNS2_4Math4CBoxENS1_18STextureRenderDataE"
    );
    if (FNS_RT.empty())
        fail_exit("No renderTexture");
    render_texture_hook = HyprlandAPI::createFunctionHook(PHANDLE, FNS_RT[0].address, (void *)hook_render_texture);
    Log::logger->log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS_RT[0].signature);
    success = render_texture_hook->hook() && success;

    static auto FNS_RB = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render2GL15CHyprOpenGLImpl12renderBorderERKN9Hyprutils4Math4CBoxERKN6Config18CGradientValueDataENS1_17SBorderRenderDataE"
    );
    if (FNS_RB.empty())
        fail_exit("No renderBorder");
    render_border_hook = HyprlandAPI::createFunctionHook(PHANDLE, FNS_RB[0].address, (void *)hook_render_border);
    Log::logger->log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS_RB[0].signature);
    success = render_border_hook->hook() && success;

    static auto FNS_RB2 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render2GL15CHyprOpenGLImpl12renderBorderERKN9Hyprutils4Math4CBoxERKN6Config18CGradientValueDataESA_fNS1_17SBorderRenderDataE"
    );
    if (FNS_RB2.empty())
        fail_exit("No renderBorder (lerp)");
    render_border2_hook = HyprlandAPI::createFunctionHook(PHANDLE, FNS_RB2[0].address, (void *)hook_render_border2);
    Log::logger->log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS_RB2[0].signature);
    success = render_border2_hook->hook() && success;

    static auto FNS_BO = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render13IHyprRenderer29shouldUseNewBlurOptimizationsEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View13CLayerSurfaceEEENS3_INS5_7CWindowEEE"
    );
    if (FNS_BO.empty())
        fail_exit("No shouldUseNewBlurOptimizations");
    blur_optimizations_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS_BO[0].address, (void *)hook_blur_optimizations);
    Log::logger->log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS_BO[0].signature);
    success = blur_optimizations_hook->hook() && success;

    static auto FNS2 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render13IHyprRenderer18shouldRenderWindowEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View7CWindowEEENS3_I8CMonitorEE"
    );
    if (FNS2.empty())
        fail_exit("No shouldRenderWindow");
    should_render_window_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS2[0].address, (void *)hook_should_render_window);
    Log::logger->log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS2[0].signature);
    success = should_render_window_hook->hook() && success;

    static auto FNS3 = HyprlandAPI::findFunctionsByName(
        PHANDLE,
        "_ZN6Render13IHyprRenderer12renderWindowEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View7CWindowEEENS3_I8CMonitorEERKNSt6chrono10time_pointINSA_3_V212steady_clockENSA_8durationIlSt5ratioILl1ELl1000000000EEEEEEbNS_15eRenderPassModeEbb"
    );
    if (FNS3.empty())
        fail_exit("No renderWindow");
    render_window = FNS3[0].address;

    static auto FNS4 = HyprlandAPI::findFunctionsByName(PHANDLE, "isSolitaryBlocked");
    if (FNS4.empty())
        fail_exit("No isSolitaryBlocked");

    is_solitary_blocked_hook =
        HyprlandAPI::createFunctionHook(PHANDLE, FNS4[0].address, (void *)hook_is_solitary_blocked);
    Log::logger->log(LOG, "[Hyprtile Overview] Attempting hook {}", FNS4[0].signature);
    success = is_solitary_blocked_hook->hook() && success;

    if (!success)
        fail_exit("Failed initializing hooks");
}

static void register_callbacks()
{
    auto& bus = Event::bus()->m_events;

    g_eventListeners = {
        bus.input.mouse.button.listen(on_mouse_button),
        bus.input.mouse.move.listen(on_mouse_move),
        bus.input.mouse.axis.listen(on_mouse_axis),

    	// TODO: support touch
        bus.input.touch.down.listen([](ITouch::SDownEvent e, Event::SCallbackInfo &i) { cancel_event(i); }),
        bus.input.touch.up.listen([](ITouch::SUpEvent e, Event::SCallbackInfo &i) { cancel_event(i); }),
        bus.input.touch.motion.listen([](ITouch::SMotionEvent e, Event::SCallbackInfo &i) { cancel_event(i); }),

        bus.gesture.swipe.begin.listen(on_swipe_begin),
        bus.gesture.swipe.update.listen(on_swipe_update),
        bus.gesture.swipe.end.listen(on_swipe_end),

        bus.config.reloaded.listen(on_config_reloaded),
        bus.monitor.added.listen([](PHLMONITOR m) { register_monitors(); }),
        bus.monitor.removed.listen(on_monitor_removed),
        bus.render.pre.listen(on_render_pre),

        bus.input.keyboard.key.listen(on_key_press),
    };
}

static void add_dispatchers()
{
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

static void init_config()
{
    // Layout selection - default to column for hyprtile
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:layout", Hyprlang::STRING{"column"});

    // general
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:bg_color",
                                Hyprlang::INT{0x00000000}); // Transparent by default
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gap_size", Hyprlang::FLOAT{10.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:border_size", Hyprlang::FLOAT{4.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:focus_scale", Hyprlang::FLOAT{1.1f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:exit_on_hovered", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:warp_on_move_window", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:close_overview_on_reload", Hyprlang::INT{1});

    // Mouse buttons
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:drag_button", Hyprlang::INT{BTN_LEFT});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:select_button", Hyprlang::INT{BTN_RIGHT});

    // Gesture settings
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:enabled", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:move_fingers", Hyprlang::INT{3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:move_distance", Hyprlang::FLOAT{300.0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:open_fingers", Hyprlang::INT{4});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:open_distance", Hyprlang::FLOAT{300.0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:gestures:open_positive", Hyprlang::INT{1});

    // Grid layout specific (kept for compatibility)
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:grid:rows", Hyprlang::INT{3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:grid:cols", Hyprlang::INT{3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:grid:loop", Hyprlang::INT{0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:grid:gaps_use_aspect_ratio", Hyprlang::INT{0});

    // Linear layout specific (kept for compatibility)
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:linear:blur", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:linear:height", Hyprlang::FLOAT{300.f});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:hyprtile:expo:linear:scroll_speed", Hyprlang::FLOAT{1.f});
}

// ========== Public API ==========

namespace overview
{

void init()
{
    Log::logger->log(LOG, "[Hyprtile Overview] Initializing overview module...");

    if (ht_manager == nullptr)
        ht_manager = std::make_unique<HTManager>();
    else
        ht_manager->reset();

    init_config();
    add_dispatchers();
    register_callbacks();
    init_functions();
    register_monitors();

    Log::logger->log(LOG, "[Hyprtile Overview] Overview module initialized");
}

void exit()
{
    Log::logger->log(LOG, "[Hyprtile Overview] Cleaning up overview module...");

    if (ht_manager)
    {
        // prevent crashes on unload (upstream 74a1319)
        ht_manager->hide_all_views();
        ht_manager->reset();
    }

    Log::logger->log(LOG, "[Hyprtile Overview] Overview module cleaned up");
}

} // namespace overview
