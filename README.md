# Hyprtile

A Hyprland plugin that extends tiling from windows to workspaces. Each workspace has sub-workspaces, so one workspace corresponds to exactly one task.

## Concept

Hyprtile organizes workspaces in a 2D grid. Each workspace (1, 2, 3...) can have sub-workspaces (1a, 1b, 1c...) below it.

- **Left/Right**: Switch between workspaces
- **Up/Down**: Switch between sub-workspaces

```
Workspace 1    Workspace 2    Workspace 3
┌─────┐        ┌─────┐        ┌─────┐
│  1  │        │  2  │        │  3  │
├─────┤        ├─────┤        ├─────┤
│ 1a  │        │ 2a  │        │ 3a  │
├─────┤        ├─────┤        └─────┘
│ 1b  │        │ 2b  │
└─────┘        └─────┘
```

## Demo

(TODO)

## Installation

### Hyprpm 

```bash
hyprpm add https://github.com/au-summer/hyprtile
hyprpm enable hyprtile
```

To update:
```bash
hyprpm update
```

### Manual

1. **Clone the repository**
   ```bash
   git clone https://github.com/au-summer/hyprtile
   cd hyprtile
   ```

2. **Update Hyprland headers** (required when Hyprland updates)
   ```bash
   hyprpm update
   ```

3. **Build the plugin**
   ```bash
   make
   ```

4. **Load the plugin**
   ```bash
   hyprctl plugin load /path/to/hyprtile/hyprtile.so
   ```

5. **Optional: Auto-load on startup**

   Add to your `~/.config/hypr/hyprland.conf`:
   ```
   exec-once = hyprctl plugin load /path/to/hyprtile/hyprtile.so
   ```

### Status Bar Integration

For proper workspace sorting in status bars, configure your bar to sort workspaces by name. Hyprtile uses zero-width characters to ensure correct alphabetical ordering (e.g., `1`, `1a`, `1b` sort correctly before `10`).

**Waybar:**
```json
"hyprland/workspaces": {
    "sort-by": "name"
}
```

## Dispatchers

Hyprtile provides custom dispatchers for workspace and window management.

### Navigation

| Dispatcher | Description |
|------------|-------------|
| `hyprtile:workspace <n\|previous>` | Switch to the last focused sub-workspace in workspace `n`, or to the previous workspace |
| `hyprtile:movefocus <l/r/u/d>` | Move focus in direction. Crosses to adjacent workspace if at edge |
| `hyprtile:movefocustomonitor <l/r/u/d>` | Move focus to adjacent monitor |
| `hyprtile:togglefocusmode` | Toggle focus mode (disables horizontal navigation to other workspaces) |

### Window Movement

| Dispatcher | Description |
|------------|-------------|
| `hyprtile:movewindow <l/r/u/d>` | Move window in direction, crossing to adjacent workspace if at edge |
| `hyprtile:movetoworkspace <n>` | Move window to the last focused sub-workspace in workspace `n` |
| `hyprtile:movetoworkspacesilent <n>` | Same as above, without following focus |

### Workspace Management

| Dispatcher | Description |
|------------|-------------|
| `hyprtile:cleancurrentcolumn` | Reorganize sub-workspaces to remove gaps (e.g., `1, 1c, 1e` becomes `1, 1a, 1b`) |
| `hyprtile:insertworkspace` | Insert new sub-workspace at current position, pushing others down |
| `hyprtile:moveworkspace <u/d>` | Swap current sub-workspace with the one above/below |
| `hyprtile:movecurrentcolumntomonitor <l/r/u/d>` | Move workspace and all its sub-workspaces to adjacent monitor |

### Overview Mode

The overview utility is built based on [hyprtasking](https://github.com/raybbian/hyprtasking) by [@raybbian](https://github.com/raybbian).

| Dispatcher | Description |
|------------|-------------|
| `hyprtile:expo <all\|cursor>` | Toggle overview. `all` for all monitors, `cursor` for monitor under cursor |
| `hyprtile:expo:move <direction>` | Navigate between workspaces in overview |
| `hyprtile:expo:movewindow <direction>` | Move window to adjacent workspace in overview |
| `hyprtile:expo:killhovered` | Close hovered window in overview |
| `hyprtile:expo:if_active <dispatcher>` | Execute dispatcher only if overview is active |
| `hyprtile:expo:if_not_active <dispatcher>` | Execute dispatcher only if overview is not active |

## Configuration

These are configurable options for hyprtile's overview mode. All options are prefixed with `plugin:hyprtile:expo:`.

### General

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `bg_color` | int | `0x00000000` | Background color (ARGB format) |
| `gap_size` | float | `10.0` | Gap between workspace thumbnails |
| `border_size` | float | `4.0` | Border size around thumbnails |
| `focus_scale` | float | `1.1` | Scale factor for focused workspace |
| `exit_on_hovered` | int | `0` | Exit to hovered workspace instead of active |
| `warp_on_move_window` | int | `1` | Warp cursor when moving window |
| `close_overview_on_reload` | int | `1` | Close overview when config reloads |

### Mouse

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `drag_button` | int | `272` | Mouse button for dragging windows (272 = left) |
| `select_button` | int | `273` | Mouse button for selecting workspace (273 = right) |

### Example

```conf
plugin {
    hyprtile {
        expo {
            gap_size = 15
            focus_scale = 1.2
        }
    }
}
```

## Usage Examples

Suggested keybindings for `~/.config/hypr/hyprland.conf`:

```conf
# Move focus
bind = $mainMod, H, hyprtile:movefocus, l
bind = $mainMod, L, hyprtile:movefocus, r
bind = $mainMod, J, hyprtile:movefocus, d
bind = $mainMod, K, hyprtile:movefocus, u

# Move window
bind = $mainMod+Ctrl, H, hyprtile:movewindow, l
bind = $mainMod+Ctrl, L, hyprtile:movewindow, r
bind = $mainMod+Ctrl, J, hyprtile:movewindow, d
bind = $mainMod+Ctrl, K, hyprtile:movewindow, u

# Switch to workspace
bind = $mainMod, 1, hyprtile:workspace, 1
bind = $mainMod, 2, hyprtile:workspace, 2
# ... (3-9)
bind = $mainMod, 0, hyprtile:workspace, 10
bind = $mainMod, Tab, hyprtile:workspace, previous

# Move window to workspace
bind = $mainMod+Shift, 1, hyprtile:movetoworkspace, 1
bind = $mainMod+Shift, 2, hyprtile:movetoworkspace, 2
# ... (3-9)
bind = $mainMod+Shift, 0, hyprtile:movetoworkspace, 10

# Move window to workspace (silent)
bind = $mainMod+Alt, 1, hyprtile:movetoworkspacesilent, 1
# ...

# Overview
bind = $mainMod, grave, hyprtile:expo, cursor

# Workspace management
bind = $mainMod, C, hyprtile:cleancurrentcolumn
bind = $mainMod, I, hyprtile:insertworkspace
bind = $mainMod+Ctrl+Shift, J, hyprtile:moveworkspace, d
bind = $mainMod+Ctrl+Shift, K, hyprtile:moveworkspace, u

# Multi-monitor
bind = $mainMod+Alt, H, hyprtile:movefocustomonitor, l
bind = $mainMod+Alt, L, hyprtile:movefocustomonitor, r
bind = $mainMod+Alt, J, hyprtile:movefocustomonitor, d
bind = $mainMod+Alt, K, hyprtile:movefocustomonitor, u

bind = $mainMod+Alt, Left, hyprtile:movecurrentcolumntomonitor, l
bind = $mainMod+Alt, Right, hyprtile:movecurrentcolumntomonitor, r
bind = $mainMod+Alt, Up, hyprtile:movecurrentcolumntomonitor, u
bind = $mainMod+Alt, Down, hyprtile:movecurrentcolumntomonitor, d
```

## Roadmap

- [x] Rewriting into plugin
  - [x] General animation control
  - [x] Dispatcher for moving the current workspace around the column
  - [ ] Dispatcher for moving the current workspace to other columns
  - [ ] Moving windows to adjacent workspaces should move to edge
- [x] Name-based instead of id-based management
- [x] Natural waybar sorting support
- [x] Overview mode
  - [x] Custom layout for hyprtasking
- [ ] Gesture support
