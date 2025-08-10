#include <climits>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/debug/Log.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>

#include "dispatchers.h"
#include "utils.h"

extern HANDLE PHANDLE;

char anim_type = '\0';

namespace dispatchers
{

char parse_move_arg(const std::string &arg)
{
    if (arg == "l" || arg == "left")
        return 'l';
    else if (arg == "r" || arg == "right")
        return 'r';
    else if (arg == "u" || arg == "up")
        return 'u';
    else if (arg == "d" || arg == "down")
        return 'd';
    else
        return '\0';
}

// when I am too lazy to use logging
void notify(const std::string &message)
{
    HyprlandAPI::invokeHyprctlCommand("notify", "1 1000000 0 " + message);
}

SDispatchResult dispatch_workspace(std::string arg)
{
    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column_id = name_to_column(current_workspace_name);

    if (arg == "previous")
    {
        for (auto const &window : g_pCompositor->m_windowFocusHistory)
        {
            const auto &workspace = window->m_workspace;
            const std::string &workspace_name = workspace->m_name;

            // do not consider special workspaces
            if (workspace_name.starts_with("special"))
                continue;

            int workspace_column_id = name_to_column(workspace_name);

            if (workspace_column_id != current_column_id)
            {
                anim_type = workspace_column_id < current_column_id ? 'l' : 'r';
                HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + workspace_name);
                anim_type = '\0';

                return {};
            }
        }

        return {};
    }

    int target_column_id = std::stoi(arg);

    for (auto const &window : g_pCompositor->m_windowFocusHistory)
    {
        const auto &workspace = window->m_workspace;

        const std::string &workspace_name = workspace->m_name;
        int workspace_column_id = name_to_column(workspace_name);

        if (workspace_column_id == target_column_id)
        {
            anim_type = workspace_column_id < current_column_id ? 'l' : 'r';
            HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + workspace_name);
            anim_type = '\0';
            return {};
        }
    }

    // no window found on target workspace, simply switch to it
    anim_type = target_column_id < current_column_id ? 'l' : 'r';
    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + std::to_string(target_column_id));
    anim_type = '\0';
    return {};
}

std::string get_workspace_in_direction(char direction)
{
    // NOTE: Only consider workspaces on the same monitor

    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column_id = name_to_column(current_workspace_name);
    int current_workspace_index = name_to_index(current_workspace_name);

    int target_column_id = 0;
    std::string target_workspace_name;

    switch (direction)
    {
    case 'l':
        for (auto const &window : g_pCompositor->m_windowFocusHistory)
        {
            if (window->m_monitor->m_id != g_pCompositor->m_lastMonitor->m_id)
                continue;

            const auto &workspace = window->m_workspace;
            const std::string &workspace_name = workspace->m_name;
            int workspace_column_id = name_to_column(workspace_name);

            if (workspace_column_id < current_column_id && workspace_column_id > target_column_id)
            {
                target_column_id = workspace_column_id;
                target_workspace_name = workspace_name;
            }
        }

        if (target_column_id == 0)
        {
            return "";
        }

        return target_workspace_name;

        break;
    case 'r':
        target_column_id = INT_MAX;
        for (auto const &window : g_pCompositor->m_windowFocusHistory)
        {
            if (window->m_monitor->m_id != g_pCompositor->m_lastMonitor->m_id)
                continue;

            const auto &workspace = window->m_workspace;
            const std::string &workspace_name = workspace->m_name;
            int workspace_column_id = name_to_column(workspace_name);

            if (workspace_column_id > current_column_id && workspace_column_id < target_column_id)
            {
                target_column_id = workspace_column_id;
                target_workspace_name = workspace_name;
            }
        }

        if (target_column_id == INT_MAX)
        {
            return "";
        }

        return target_workspace_name;
        break;
    case 'u':
        // if it is already the first workspace in the column, do nothing
        if (current_workspace_index == 0)
            return "";

        return get_workspace_name(current_column_id, current_workspace_index - 1);
        break;
    case 'd':
        return get_workspace_name(current_column_id, current_workspace_index + 1);
        break;
    }

    return "";
}

SDispatchResult dispatch_movefocus(std::string arg)
{
    char direction = parse_move_arg(arg);

    const auto PLASTWINDOW = g_pCompositor->m_lastWindow.lock();

    // If there is a window and a window to move focus to, handle it by hyprland
    if (PLASTWINDOW)
    {
        const auto PWINDOWTOCHANGETO = g_pCompositor->getWindowInDirection(PLASTWINDOW, direction);

        // Found window in direction and on same workspace, switch to it
        if (PWINDOWTOCHANGETO && PWINDOWTOCHANGETO->m_workspace->m_id == PLASTWINDOW->m_workspace->m_id)
        {
            g_pCompositor->focusWindow(PWINDOWTOCHANGETO);
            return {};
        }
    }

    // No window in direction

    std::string target_workspace_name = get_workspace_in_direction(direction);

    if (!target_workspace_name.empty())
    {
        bool found = false;
        PHLWINDOWREF target_window;
        // Find the window in the workspace in direction
        for (auto const &window : g_pCompositor->m_windowFocusHistory)
        {
            const std::string &window_workspace_name = window->m_workspace->m_name;

            if (window_workspace_name == target_workspace_name)
            {
                switch (direction)
                {
                case 'l':
                    if (!found || window->m_position.x > target_window->m_position.x)
                        target_window = window;
                    break;
                case 'r':
                    if (!found || window->m_position.x < target_window->m_position.x)
                        target_window = window;
                    break;
                case 'u':
                    if (!found || window->m_position.y > target_window->m_position.y)
                        target_window = window;
                    break;
                case 'd':
                    if (!found || window->m_position.y < target_window->m_position.y)
                        target_window = window;
                    break;
                }

                found = true;
            }
        }

        anim_type = direction;
        if (found)
        {

            g_pCompositor->focusWindow(target_window.lock());
        }
        else
        {
            HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + target_workspace_name);
        }

        anim_type = '\0';
    }

    // if (!target_workspace_name.empty())
    // {
    //     anim_type = direction;
    //     HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + target_workspace_name);
    //     anim_type = '\0';
    //     return {};
    // }

    return {};
}

SDispatchResult dispatch_movewindow(std::string arg)
{
    // arg can be workspace num or direction
    char direction = parse_move_arg(arg);

    const auto PLASTWINDOW = g_pCompositor->m_lastWindow.lock();
    if (!PLASTWINDOW)
        return {.success = false, .error = "Window to move not found"};

    if (PLASTWINDOW->isFullscreen())
        return {.success = false, .error = "Can't move fullscreen window"};

    if (PLASTWINDOW->m_isFloating)
    {
        // cannot handle it, go to hyprland solution
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movewindow " + arg);
        return {};
    }

    const auto PWINDOWTOCHANGETO = g_pCompositor->getWindowInDirection(PLASTWINDOW, direction);
    // Found window in direction and on same workspace
    if (PWINDOWTOCHANGETO && PWINDOWTOCHANGETO->m_workspace->m_id == PLASTWINDOW->m_workspace->m_id)
    {
        // cannot handle it, go to hyprland solution
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movewindow " + arg);
        return {};
    }

    std::string target_workspace_name = get_workspace_in_direction(direction);
    if (!target_workspace_name.empty())
    {
        anim_type = direction;
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace name:" + target_workspace_name);
        anim_type = '\0';
        return {};
    }

    return {};
}

SDispatchResult dispatch_movetoworkspace(std::string arg)
{
    int target_column_id = name_to_column(arg);

    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column_id = name_to_column(current_workspace_name);

    for (auto const &window : g_pCompositor->m_windowFocusHistory)
    {
        const auto &workspace = window->m_workspace;
        const std::string &workspace_name = workspace->m_name;
        int workspace_column_id = name_to_column(workspace_name);

        if (workspace_column_id == target_column_id)
        {
            anim_type = workspace_column_id < current_column_id ? 'l' : 'r';
            HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace name:" + workspace->m_name);
            anim_type = '\0';
            return {};
        }
    }

    // no window found on target workspace, simply switch to it
    anim_type = target_column_id < current_column_id ? 'l' : 'r';
    HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace name:" + arg);
    anim_type = '\0';

    return {};
}

SDispatchResult dispatch_movetoworkspacesilent(std::string arg)
{
    int target_column_id = name_to_column(arg);

    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column_id = name_to_column(current_workspace_name);

    for (auto const &window : g_pCompositor->m_windowFocusHistory)
    {
        const auto &workspace = window->m_workspace;
        const std::string &workspace_name = workspace->m_name;
        int workspace_column_id = name_to_column(workspace_name);

        if (workspace_column_id == target_column_id)
        {
            anim_type = workspace_column_id < current_column_id ? 'l' : 'r';
            HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspacesilent name:" + workspace->m_name);
            anim_type = '\0';
            return {};
        }
    }

    // no window found on target workspace, simply switch to it
    anim_type = target_column_id < current_column_id ? 'l' : 'r';
    HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspacesilent name:" + arg);
    anim_type = '\0';

    return {};
}

SDispatchResult dispatch_clearworkspaces(std::string arg)
{
    // check all the workspaces on this column
    // if there is empty ones, shrink others
    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column_id = name_to_column(current_workspace_name);

    // pair of (name, id)
    // include id because hyprland needs id to rename workspace
    std::set<std::pair<std::string, int>> workspaces_in_column;

    for (const auto &workspace : g_pCompositor->m_workspaces)
    {
        // do not consider special workspaces
        if (workspace->m_name.starts_with("special"))
            continue;

        int workspace_column_id = name_to_column(workspace->m_name);
        if (workspace_column_id == current_column_id)
        {
            workspaces_in_column.insert({workspace->m_name, workspace->m_id});
        }
    }

    // std::sort(workspaces_in_column.begin(), workspaces_in_column.end());

    int counter = 0;
    for (auto origin_workspace : workspaces_in_column)
    {
        std::string target_workspace_name = get_workspace_name(current_column_id, counter);

        if (origin_workspace.first == target_workspace_name)
        {
            // this workspace is already in the right place, skip it
            counter++;
            continue;
        }
        HyprlandAPI::invokeHyprctlCommand("dispatch", "renameworkspace " + std::to_string(origin_workspace.second) +
                                                          " " + target_workspace_name);

        counter++;
    }

    return {};
}

SDispatchResult dispatch_insertworkspace(std::string arg)
{
    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column = name_to_column(current_workspace_name);
    int current_index = name_to_index(current_workspace_name);

    std::set<std::pair<std::string, int>> workspaces_in_column;

    for (const auto &workspace : g_pCompositor->m_workspaces)
    {
        // do not consider special workspaces
        if (workspace->m_name.starts_with("special"))
            continue;

        int workspace_column = name_to_column(workspace->m_name);
        int workspace_index = name_to_index(workspace->m_name);

        if (workspace_column == current_column && workspace_index >= current_index)
        {
            workspaces_in_column.insert({workspace->m_name, workspace->m_id});
        }
    }

    for (auto it = workspaces_in_column.rbegin(); it != workspaces_in_column.rend(); ++it)
    {
        const auto &workspace_name = it->first;
        const auto &workspace_id = it->second;
        int workspace_index = name_to_index(workspace_name);

        HyprlandAPI::invokeHyprctlCommand("dispatch", "renameworkspace " + std::to_string(workspace_id) + " " +
                                                          get_workspace_name(current_column, workspace_index + 1));
    }

    // switch to the new workspace
    std::string new_workspace_name = get_workspace_name(current_column, current_index);
    anim_type = 'f';
    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + new_workspace_name);
    anim_type = '\0';

    return {};
}

SDispatchResult dispatch_movecurrentworkspacetomonitor(std::string arg)
{
    char direction = parse_move_arg(arg);

    const auto &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    const auto current_column = name_to_column(current_workspace_name);

    // Here we move other workspaces in the column first, then the current one
    // This is because Hyprland uses current workspace's l/r/u/d to determine the target monitor
    for (const auto &workspace : g_pCompositor->m_workspaces)
    {
        // do not consider special workspaces
        if (workspace->m_name.starts_with("special"))
            continue;

        const auto workspace_column = name_to_column(workspace->m_name);

        // if (workspace_column == current_column)
        if (workspace->m_name != current_workspace_name && workspace_column == current_column)
        {
            HyprlandAPI::invokeHyprctlCommand("dispatch",
                                              "moveworkspacetomonitor name:" + workspace->m_name + " " + arg);
        }
    }

    // Then move current workspace
    HyprlandAPI::invokeHyprctlCommand("dispatch", "movecurrentworkspacetomonitor " + arg);

    return {};
}

SDispatchResult dispatch_movefocustomonitor(std::string arg)
{
    char direction = parse_move_arg(arg);

    const auto target_monitor = g_pCompositor->getMonitorInDirection(direction);

    if (target_monitor)
    {
        HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + target_monitor->m_activeWorkspace->m_name);
    }

    return {};
}

void addDispatchers()
{
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:workspace", dispatch_workspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movefocus", dispatch_movefocus);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movewindow", dispatch_movewindow);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movetoworkspace", dispatch_movetoworkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movetoworkspacesilent", dispatch_movetoworkspacesilent);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:clearworkspaces", dispatch_clearworkspaces);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:insertworkspace", dispatch_insertworkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movecurrentworkspacetomonitor",
                                 dispatch_movecurrentworkspacetomonitor);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movefocustomonitor", dispatch_movefocustomonitor);
}

} // namespace dispatchers
