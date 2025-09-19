# Hyprtile

This plugin makes Hyprland not only tile windows, but also tile workspaces with each other.

## Idea

The idea is that each workspace is a column, with each workspace being a tile in that column. The columns are arranged horizontally, and the workspaces in each column are arranged vertically. This allows for a more flexible and logical management of windows and workspaces.

# Demo

(TODO)

## Installation

1. Clone the repository
1. Build the plugin using `make`
1. Run `hyprctl plugin load /path/to/hyprtile.so`
1. Set `"sort-by": "name"` in `"hyprland/workspaces"` waybar setting

> Note: If when loading the plugin you get an error `[hyprtile] Version mismatch`, install the Hyprland headers that fits your Hyprland version. You can install them by cloning the official Hyprland repository, checkout to your corresponding version's commit, and run `sudo make installheaders`.

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

## Roadmap

- [x] Rewriting into plugin
  - [ ] Code Refactoring
  - [x] General animation control
  - [x] Dispatcher for moving the current workspace around the column
  - [ ] Dispatcher for moving the current workspace to other columns
  - [ ] Moving windows to adjacent workspaces should move to edge
  - [ ] Gesture support
  - [ ] Maintain a data structure of window-workspace mapping by the plugin itself, instead of relying on Hyprland's internal data structures
- [x] Name-based instead of id-based management
- [x] Natural waybar sorting support
- [ ] Overview mechanism using Plugin/AGS/Quickshell/...
