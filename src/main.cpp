#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/animation/AnimatedVariable.hpp>

#include "dispatchers.h"
#include "utils.h"

#include <limits>

HANDLE PHANDLE = nullptr;

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
        for (const auto &workspace : g_pCompositor->getWorkspaces())
        {
            int column = name_to_column(workspace->m_name);

            if (i != column)
                return i;
        }
    }

    return LONG_MAX;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle)
{
    PHANDLE = handle;

    const std::string HASH = __hyprland_api_get_hash();

    // ALWAYS add this to your plugins. It will prevent random crashes coming from
    // mismatched header versions.
    if (HASH != GIT_COMMIT_HASH)
    {
        HyprlandAPI::addNotification(PHANDLE, "[hyprtile] Mismatched headers! Can't proceed.",
                                     CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[hyprtile] Version mismatch");
    }

    // Function Hooks
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

    // Dispatchers
    dispatchers::addDispatchers();

    return {"hyprtile", "tiled workspace management", "ausummer", "1.0"};
}
