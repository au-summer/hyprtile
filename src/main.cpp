#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/animation/AnimatedVariable.hpp>

#include "dispatchers.h"
#include "globals.h"
#include "utils.h"

// Overview module
#include "overview/globals.hpp"
#include "overview/init.hpp"

APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

char anim_type = '\0';

inline CFunctionHook *g_pChangeWorkspaceHook = nullptr;
typedef void (*origChangeWorkspace)(CMonitor *, const PHLWORKSPACE &, bool, bool, bool);
void hk_changeWorkspace(CMonitor *thisptr, const PHLWORKSPACE &pWorkspace, bool internal, bool noMouseMove,
                        bool noFocus)
{
    if (!pWorkspace || !thisptr->m_activeWorkspace)
    {
        (*(origChangeWorkspace)g_pChangeWorkspaceHook->m_original)(thisptr, pWorkspace, internal, noMouseMove, noFocus);
        return;
    }

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

inline CFunctionHook *g_pChangeWorkspaceIDHook = nullptr;
void hk_changeWorkspaceID(CMonitor *thisptr, const WORKSPACEID &id, bool internal, bool noMouseMove, bool noFocus)
{
    hk_changeWorkspace(thisptr, g_pCompositor->getWorkspaceByID(id), internal, noMouseMove, noFocus);
}

inline CFunctionHook *g_pStartAnimationHook = nullptr;
typedef void (*origStartAnimation)(CDesktopAnimationManager *, PHLWORKSPACE, CDesktopAnimationManager::eAnimationType,
                                   bool, bool, std::optional<std::string>);
void hk_startAnimation(CDesktopAnimationManager *thisptr, PHLWORKSPACE ws,
                       CDesktopAnimationManager::eAnimationType type, bool left, bool instant,
                       std::optional<std::string> styleArg)
{
    // Override animation if overview is active to prevent flickering
    if (ht_manager && ht_manager->has_active_view())
    {
        instant = true;
    }

    switch (anim_type)
    {
    case 'l':
        left = false;
        styleArg = "slide";
        break;
    case 'r':
        left = true;
        styleArg = "slide";
        break;
    case 'u':
        left = false;
        styleArg = "slidevert";
        break;
    case 'd':
        left = true;
        styleArg = "slidevert";
        break;
    }

    (*(origStartAnimation)g_pStartAnimationHook->m_original)(thisptr, ws, type, left, instant, styleArg);
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

// fail loudly if the symbol moved
static CFunctionHook *hook_or_throw(const std::string &name, const std::string &label, void *target)
{
    const auto matches = HyprlandAPI::findFunctionsByName(PHANDLE, name);

    if (matches.empty())
    {
        HyprlandAPI::addNotification(PHANDLE, "[hyprtile] Failed to resolve symbol: " + label,
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprtile] symbol resolution failed: " + label);
    }
    CFunctionHook *hook = HyprlandAPI::createFunctionHook(PHANDLE, matches[0].address, target);
    hook->hook();
    return hook;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;

    const std::string COMPOSITOR_HASH = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    // ALWAYS add this to your plugins. It will prevent random crashes coming from
    // mismatched header versions.
    if (COMPOSITOR_HASH != CLIENT_HASH)
    {
        HyprlandAPI::addNotification(PHANDLE,
                                     "[hyprtile] Mismatched headers! Can't proceed.\nCompositor Hash: " +
                                         COMPOSITOR_HASH + "\nClient Hash: " + CLIENT_HASH,
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprtile] Version mismatch\nCompositor Hash: " + COMPOSITOR_HASH +
                                 "\nClient Hash: " + CLIENT_HASH);
    }

    // Function hooks
    g_pChangeWorkspaceIDHook = hook_or_throw("_ZN8CMonitor15changeWorkspaceERKlbbb",
                                             "CMonitor::changeWorkspace(id)", (void *)&hk_changeWorkspaceID);

    g_pChangeWorkspaceHook =
        hook_or_throw("_ZN8CMonitor15changeWorkspaceERKN9Hyprutils6Memory14CSharedPointerI10CWorkspaceEEbbb",
                      "CMonitor::changeWorkspace(workspace)", (void *)&hk_changeWorkspace);

    g_pStartAnimationHook = hook_or_throw(
        "_ZN24CDesktopAnimationManager14startAnimationEN9Hyprutils6Memory14CSharedPointerI10CWorkspaceEENS_"
        "14eAnimationTypeEbbSt8optionalINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEE",
        "CDesktopAnimationManager::startAnimation(workspace)", (void *)&hk_startAnimation);

    g_pFindAvailableDefaultWSHook = hook_or_throw("findAvailableDefaultWS", "CMonitor::findAvailableDefaultWS",
                                                  (void *)&hk_findAvailableDefaultWS);

    // Dispatchers
    dispatchers::addDispatchers();

    // Initialize overview module
    overview::init();

    HyprlandAPI::reloadConfig();

    return {"hyprtile", "tiled workspace management with overview", "ausummer", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT()
{
    // Cleanup overview module
    overview::exit();
}
