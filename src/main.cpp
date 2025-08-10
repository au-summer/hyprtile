#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

#include "dispatchers.h"

HANDLE PHANDLE = nullptr;

APICALL EXPORT std::string PLUGIN_API_VERSION()
{
    return HYPRLAND_API_VERSION;
}

inline CFunctionHook *g_pStartAnimHook = nullptr;
typedef void (*origStartAnim)(CWorkspace *, bool, bool, bool);
void hk_startAnim(CWorkspace *thisptr, bool in, bool left, bool instant)
{
    auto config = thisptr->m_alpha->getConfig();
    auto &style = config->pValues->internalStyle;

    // For reset purpose
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
    // fade
    case 'f':
        style = "fade";
        break;
    }

    (*(origStartAnim)g_pStartAnimHook->m_original)(thisptr, in, left, instant);

    // Reset the style to original after the animation
    style = original_style;
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
    static const auto START_ANIM = HyprlandAPI::findFunctionsByName(PHANDLE, "startAnim");
    g_pStartAnimHook = HyprlandAPI::createFunctionHook(PHANDLE, START_ANIM[0].address, (void *)&hk_startAnim);
    g_pStartAnimHook->hook();

    // Dispatchers
    dispatchers::addDispatchers();

    return {"hyprtile", "tiled workspace management", "ausummer", "1.0"};
}
