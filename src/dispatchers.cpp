#include "dispatchers.h"
#include "overview.h"

#include <climits>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/debug/Log.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/Window.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>

#include "globals.h"
#include "utils.h"

bool focus_mode = false;

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

bool is_window_on_current_monitor(const PHLWINDOWREF &window)
{
    return window->m_monitor->m_id == g_pCompositor->m_lastMonitor->m_id;
}

// check if we should use Hyprland's focus logic
bool should_use_hyprland_for_floating_focus(const PHLWINDOW &old_window, const PHLWINDOW &new_window, char direction)
{
    if (!old_window->m_isFloating || !new_window->m_isFloating)
        return true;

    const auto &old_pos = old_window->m_realPosition;
    const auto &old_size = old_window->m_realSize;
    const auto &new_pos = new_window->m_realPosition;
    const auto &new_size = new_window->m_realSize;

    if ((direction == 'l' && new_pos->goal().x >= old_pos->goal().x) ||
        (direction == 'r' && new_pos->goal().x + new_size->goal().x <= old_pos->goal().x + old_size->goal().x) ||
        (direction == 'u' && new_pos->goal().y >= old_pos->goal().y) ||
        (direction == 'd' && new_pos->goal().y + new_size->goal().y <= old_pos->goal().y + old_size->goal().y))
    {
        return false;
    }

    return true;
}

// compare windows by position based on direction to find best window to focus
bool is_better_window_for_direction(const PHLWINDOWREF &candidate, const PHLWINDOWREF &current_best, char direction)
{
    switch (direction)
    {
    case 'l':
        return candidate->m_realPosition->goal().x + candidate->m_realSize->goal().x >
               current_best->m_realPosition->goal().x + current_best->m_realSize->goal().x;
    case 'r':
        return candidate->m_realPosition->goal().x < current_best->m_realPosition->goal().x;
    case 'u':
        return candidate->m_realPosition->goal().y + candidate->m_realSize->goal().y >
               current_best->m_realPosition->goal().y + current_best->m_realSize->goal().y;
    case 'd':
        return candidate->m_realPosition->goal().y < current_best->m_realPosition->goal().y;
    default:
        return false;
    }
}

// find best window to focus in target workspace based on direction
PHLWINDOWREF find_best_window_in_workspace(const std::string &target_workspace_name, char direction)
{
    bool found = false;
    PHLWINDOWREF target_window;

    for (auto const &window : g_pCompositor->m_windowFocusHistory)
    {
        const std::string &window_workspace_name = window->m_workspace->m_name;

        if (window_workspace_name == target_workspace_name)
        {
            // If the window is fullscreen, we want to focus it
            if (window->isFullscreen())
            {
                return window;
            }

            if (!found)
            {
                target_window = window;
            }
            else if (is_better_window_for_direction(window, target_window, direction))
            {
                target_window = window;
            }

            found = true;
        }
    }

    return found ? target_window : PHLWINDOWREF();
}

// find workspace in column latest visited
std::string find_workspace_by_column(int target_column)
{
    for (auto const &window : g_pCompositor->m_windowFocusHistory)
    {
        const auto &workspace = window->m_workspace;
        const std::string &workspace_name = workspace->m_name;
        int workspace_column = name_to_column(workspace_name);

        // skip special workspaces
        if (workspace_column == -1)
            continue;

        if (workspace_column == target_column)
        {
            return workspace_name;
        }
    }

    return "";
}

// find previous workspace on different column on current monitor
std::string find_previous_workspace(int current_column)
{
    for (auto const &window : g_pCompositor->m_windowFocusHistory)
    {
        // TODO: is this really necessary?
        if (!is_window_on_current_monitor(window))
            continue;

        const auto &workspace = window->m_workspace;
        const std::string &workspace_name = workspace->m_name;

        int workspace_column = name_to_column(workspace_name);

        // special workspace, etc.
        if (workspace_column == -1)
            continue;

        if (workspace_column != current_column)
        {
            return workspace_name;
        }
    }

    return "";
}

SDispatchResult dispatch_workspace(std::string arg)
{
    if (focus_mode)
    {
        return {.success = false, .error = "Focus mode is enabled"};
    }

    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column = name_to_column(current_workspace_name);

    if (arg == "previous")
    {
        std::string previous_workspace_name = find_previous_workspace(current_column);
        if (!previous_workspace_name.empty())
        {
            // anim_type = workspace_column < current_column ? 'l' : 'r';
            HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + previous_workspace_name);
            // anim_type = '\0';
        }
        return {};
    }
    // TODO: support l/r/u/d
    else if (arg == "l")
    {
    }
    else if (arg == "r")
    {
    }
    else if (arg == "u")
    {
    }
    else if (arg == "d")
    {
    }

    int target_column = std::stoi(arg);

    std::string workspace_name = find_workspace_by_column(target_column);

    // no window found on target workspace, simply switch to it
    if (workspace_name.empty())
    {
        workspace_name = get_workspace_name(target_column, 0);
    }

    // anim_type = target_column < current_column ? 'l' : 'r';
    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + workspace_name);
    // anim_type = '\0';
    return {};
}

std::string find_horizontal_workspace(int current_column, bool search_left)
{
    int target_column = search_left ? 0 : INT_MAX;
    std::string target_workspace_name;

    for (auto const &window : g_pCompositor->m_windowFocusHistory)
    {
        if (!is_window_on_current_monitor(window))
            continue;

        const auto &workspace = window->m_workspace;
        const std::string &workspace_name = workspace->m_name;
        int workspace_column = name_to_column(workspace_name);

        // skip special workspaces
        if (workspace_column == -1)
            continue;

        bool is_valid = search_left ? (workspace_column < current_column && workspace_column > target_column)
                                    : (workspace_column > current_column && workspace_column < target_column);

        if (is_valid)
        {
            target_column = workspace_column;
            target_workspace_name = workspace_name;
        }
    }

    // Check if we found a workspace
    bool found = search_left ? (target_column != 0) : (target_column != INT_MAX);
    return found ? target_workspace_name : "";
}

std::string get_workspace_in_direction(char direction)
{
    // NOTE: Only consider workspaces on the same monitor

    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column = name_to_column(current_workspace_name);
    int current_index = name_to_index(current_workspace_name);

    switch (direction)
    {
    case 'l':
        return find_horizontal_workspace(current_column, true);
    case 'r':
        return find_horizontal_workspace(current_column, false);
    case 'u':
        // if it is already the first workspace in the column, do nothing
        if (current_index == 0)
            return "";
        return get_workspace_name(current_column, current_index - 1);
    case 'd':
        return get_workspace_name(current_column, current_index + 1);
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
            // special condition: both windows are floating
            // TODO: getWindowInDirection behaves weird when focusing up with floating windows
            if (should_use_hyprland_for_floating_focus(PLASTWINDOW, PWINDOWTOCHANGETO, direction))
            {
                // g_pCompositor->focusWindow(PWINDOWTOCHANGETO);

                // Due to mouse cursor warping and other focus side effects, use hyprland's command instead
                HyprlandAPI::invokeHyprctlCommand(
                    "dispatch", "focuswindow address:" + std::format("{:#x}", (uintptr_t)PWINDOWTOCHANGETO.get()));
                return {};
            }
        }
    }

    // No window in direction

    if (focus_mode && (direction == 'l' || direction == 'r'))
    {
        return {.success = false, .error = "Focus mode is enabled"};
    }

    std::string target_workspace_name = get_workspace_in_direction(direction);

    if (!target_workspace_name.empty())
    {
        // Find the window in the workspace in direction
        PHLWINDOWREF target_window = find_best_window_in_workspace(target_workspace_name, direction);

        // anim_type = direction;
        if (target_window)
        {
            // g_pCompositor->focusWindow(target_window.lock());

            // Due to hide_special_on_workspace_change option, use hyprland's command instead
            HyprlandAPI::invokeHyprctlCommand("dispatch", "focuswindow address:" +
                                                              std::format("{:#x}", (uintptr_t)target_window.get()));
        }
        else
        {
            HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + target_workspace_name);
        }

        // anim_type = '\0';
    }

    // if (!target_workspace_name.empty())
    // {
    //     anim_type = direction;
    //     HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" +
    //     target_workspace_name); anim_type = '\0'; return {};
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
        // check if the window is on the edge
        const auto &pos = PLASTWINDOW->m_realPosition->goal();
        // const auto &posX = pos->goal().x;
        // const auto &posY = pos->goal().y;

        const auto &size = PLASTWINDOW->m_realSize->goal();
        // const auto &sizeX = size->goal().x;
        // const auto &sizeY = size->goal().y;

        const auto &mon_size = PLASTWINDOW->m_monitor->m_size;

        // if (!((posX <= 0 && direction == 'l') || (posX + sizeX >= mon_size.x && direction == 'r') ||
        //       (posY <= 0 && direction == 'u') || (posY + sizeY >= mon_size.y && direction == 'd')))
        if (!((pos.x <= 0 && direction == 'l') || (pos.x + size.x >= mon_size.x && direction == 'r') ||
              (pos.y <= 0 && direction == 'u') || (pos.y + size.y >= mon_size.y && direction == 'd')))
        {
            // cannot handle it, go to hyprland solution
            HyprlandAPI::invokeHyprctlCommand("dispatch", "movewindow " + arg);
            return {};
        }
    }

    const auto PWINDOWTOCHANGETO = g_pCompositor->getWindowInDirection(PLASTWINDOW, direction);
    // Found window in direction and on same workspace
    if (PWINDOWTOCHANGETO && PWINDOWTOCHANGETO->m_workspace->m_id == PLASTWINDOW->m_workspace->m_id)
    {
        // cannot handle it, go to hyprland solution
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movewindow " + arg);
        return {};
    }

    if (focus_mode)
    {
        return {.success = false, .error = "Focus mode is enabled"};
    }

    std::string target_workspace_name = get_workspace_in_direction(direction);
    if (!target_workspace_name.empty())
    {
        // anim_type = direction;
        HyprlandAPI::invokeHyprctlCommand("dispatch", "movetoworkspace name:" + target_workspace_name);
        // anim_type = '\0';
        return {};
    }

    return {};
}

// shared implementation for movetoworkspace and movetoworkspacesilent
SDispatchResult move_to_workspace_impl(std::string arg, bool silent)
{
    if (focus_mode)
    {
        return {.success = false, .error = "Focus mode is enabled"};
    }

    int target_column = name_to_column(arg);

    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column = name_to_column(current_workspace_name);

    std::string workspace_name_to_use = find_workspace_by_column(target_column);

    // no window found on target workspace, use default workspace name
    if (workspace_name_to_use.empty())
    {
        workspace_name_to_use = get_workspace_name(target_column, 0);
    }

    // anim_type = workspace_column < current_column ? 'l' : 'r';
    std::string command = silent ? "movetoworkspacesilent name:" : "movetoworkspace name:";
    HyprlandAPI::invokeHyprctlCommand("dispatch", command + workspace_name_to_use);
    // anim_type = '\0';

    return {};
}

SDispatchResult dispatch_movetoworkspace(std::string arg)
{
    return move_to_workspace_impl(arg, false);
}

SDispatchResult dispatch_movetoworkspacesilent(std::string arg)
{
    return move_to_workspace_impl(arg, true);
}

SDispatchResult dispatch_cleancurrentcolumn(std::string arg)
{
    // check all the workspaces on this column
    // if there is empty ones, shrink others
    const std::string &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column = name_to_column(current_workspace_name);

    // pair of (name, id)
    // include id because hyprland needs id to rename workspace
    std::set<std::pair<std::string, int>> workspaces_in_column;

    for (const auto &workspace : g_pCompositor->getWorkspaces())
    {
        int workspace_column = name_to_column(workspace->m_name);

        // skip special workspaces
        if (workspace_column == -1)
            continue;

        if (workspace_column == current_column)
        {
            workspaces_in_column.insert({workspace->m_name, workspace->m_id});
        }
    }

    // std::sort(workspaces_in_column.begin(), workspaces_in_column.end());

    int counter = 0;
    for (auto origin_workspace : workspaces_in_column)
    {
        std::string target_workspace_name = get_workspace_name(current_column, counter);

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

    for (const auto &workspace : g_pCompositor->getWorkspaces())
    {
        int workspace_column = name_to_column(workspace->m_name);
        int workspace_index = name_to_index(workspace->m_name);

        // skip special workspaces
        if (workspace_column == -1)
            continue;

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
    // anim_type = 'f';
    HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + new_workspace_name);
    // anim_type = '\0';

    return {};
}

SDispatchResult dispatch_moveworkspace(std::string arg)
{
    char direction = parse_move_arg(arg);
    // relative index
    int dy = 0;
    if (direction == 'u')
    {
        dy = -1;
    }
    else if (direction == 'd')
    {
        dy = 1;
    }
    else
    {
        // TODO: support moving workspace to left/right column
        return {.success = false, .error = "Invalid direction for moveworkspace"};
    }

    auto current_workspace_id = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_id;

    // not a reference because it can be modified later
    const std::string current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    int current_column = name_to_column(current_workspace_name);
    int current_index = name_to_index(current_workspace_name);

    for (const auto &workspace : g_pCompositor->getWorkspaces())
    {
        int workspace_column = name_to_column(workspace->m_name);
        int workspace_index = name_to_index(workspace->m_name);

        // skip special workspaces
        if (workspace_column == -1)
            continue;

        if (workspace_column == current_column && workspace_index == current_index + dy)
        {
            std::string target_workspace_name = workspace->m_name;

            // for animation puspose, first switch to that workspace
            // anim_type = direction;
            HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + target_workspace_name);

            // swap the name
            HyprlandAPI::invokeHyprctlCommand("dispatch", "renameworkspace " + std::to_string(current_workspace_id) +
                                                              " " + target_workspace_name);

            HyprlandAPI::invokeHyprctlCommand("dispatch", "renameworkspace " + std::to_string(workspace->m_id) + " " +
                                                              current_workspace_name);

            // switch back to the current workspace
            // which is now renamed to target_workspace_name!
            HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + target_workspace_name);
            // anim_type = '\0';

            return {};
        }
    }

    return {};
}

SDispatchResult dispatch_movecurrentcolumntomonitor(std::string arg)
{
    char direction = parse_move_arg(arg);

    const auto &current_workspace_name = g_pCompositor->m_lastMonitor->m_activeWorkspace->m_name;
    const auto current_column = name_to_column(current_workspace_name);

    // Here we move other workspaces in the column first, then the current one
    // This is because Hyprland uses current workspace's l/r/u/d to determine the
    // target monitor
    for (const auto &workspace : g_pCompositor->getWorkspaces())
    {
        // do not consider special workspaces
        if (workspace->m_name.starts_with("special"))
            continue;

        const auto workspace_column = name_to_column(workspace->m_name);

        // skip special workspaces
        if (workspace_column == -1)
            continue;

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
    if (focus_mode)
    {
        return {.success = false, .error = "Focus mode is enabled"};
    }

    char direction = parse_move_arg(arg);

    const auto target_monitor = g_pCompositor->getMonitorInDirection(direction);

    if (target_monitor)
    {
        HyprlandAPI::invokeHyprctlCommand("dispatch", "workspace name:" + target_monitor->m_activeWorkspace->m_name);
    }

    return {};
}

SDispatchResult dispatch_togglefocusmode(std::string arg)
{
    focus_mode = !focus_mode;
    return {};
}

void addDispatchers()
{
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:workspace", dispatch_workspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movefocus", dispatch_movefocus);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movewindow", dispatch_movewindow);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movetoworkspace", dispatch_movetoworkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movetoworkspacesilent", dispatch_movetoworkspacesilent);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:cleancurrentcolumn", dispatch_cleancurrentcolumn);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:insertworkspace", dispatch_insertworkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:moveworkspace", dispatch_moveworkspace);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movecurrentcolumntomonitor", dispatch_movecurrentcolumntomonitor);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:movefocustomonitor", dispatch_movefocustomonitor);
    HyprlandAPI::addDispatcherV2(PHANDLE, "hyprtile:togglefocusmode", dispatch_togglefocusmode);
}

} // namespace dispatchers
