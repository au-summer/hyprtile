A plugin that implements real tiling mechanism for Hyprland.

---

This plugin makes Hyprland's not only windows, but also workspaces tile with each other.

## Idea

The idea is that each workspace is a column, with each workspace being a tile in that column. The columns are arranged horizontally, and the workspaces in each column are arranged vertically. This allows for a more flexible and logical management of windows and workspaces.

## Dispatchers

- `hyprtile:workspace number` - Switch to the last focused workspace on that column.
- `hyprtile:movefocus l/r/u/d` - Movefocus in the specified direction.
- `hyprtile:movewindow l/r/u/d` - Move the focused window in the specified direction. Can move the window to adjacent workspaces.
- `hyprtile:movetoworkspace number` - Move the focused window to the last focused workspace on that column.
- `hyprtile:movetoworkspaceslient number` - Move the focused window to the last focused workspace on that column silently.
- `hyprtile:clearworkspace` - Clear the current column to remove all empty workspaces.
- `hyprtile:insertworkspace` - Insert a new workspace on the current column, and push all workspaces below by one.
- `hyprtile:movecurrentworkspacetomonitor l/r/u/d` - Move the current column to the next monitor in the specified direction. (The name is kept for compatibility with hyprland)
- `hyprtile:movefocustomonitor l/r/u/d` - Move focus to the next monitor in the specified direction.
