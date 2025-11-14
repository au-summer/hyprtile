# Hyprtile

A Hyprland plugin that brings tiling window concepts to workspace themselves. Organize your workspaces in a logical, two-dimensional grid where workspaces are arranged in vertical columns.

## Concept

Hyprtile organizes workspaces into **vertical columns** that are arranged horizontally across your screen. Think of it as applying tiling window manager principles to workspaces themselves.

```
Column 1    Column 2    Column 3
┌─────┐     ┌─────┐     ┌─────┐
│  1  │     │  2  │     │  3  │
├─────┤     ├─────┤     ├─────┤
│ 1a  │     │ 2a  │     │ 3a  │
├─────┤     ├─────┤     └─────┘
│ 1b  │     │ 2b  │
└─────┘     └─────┘
```

## Features

- **2D Workspace Navigation**: Move between workspaces left/right (columns) and up/down (within columns)
- **Smart Window Movement**: Move windows directionally across workspace boundaries
- **Workspace Overview Mode**: Grid view of all workspaces (expo mode)
- **Smooth Directional Animations**: Workspace transitions animate based on direction
- **Column Management**: Insert, clear, and reorganize workspace columns
- **Waybar Integration**: Natural workspace sorting in status bars
- **Multi-monitor Support**: Move columns and focus between monitors

## Demo

(TODO)

## Installation

### Build and Install

1. **Clone the repository**
   ```bash
   git clone https://github.com/yourusername/hyprtile
   cd hyprtile
   ```

2. **Build the plugin**
   ```bash
   make
   ```

3. **Load the plugin**
   ```bash
   hyprctl plugin load /path/to/hyprtile/hyprtile.so
   ```

4. **Optional: Auto-load on startup**

   Add to your `~/.config/hypr/hyprland.conf`:
   ```
   exec-once = hyprctl plugin load /path/to/hyprtile/hyprtile.so
   ```

### Waybar Integration

For proper workspace sorting in Waybar, add this to your waybar config:

```json
"hyprland/workspaces": {
    "sort-by": "name"
}
```

## Configuration

Add these options to your `~/.config/hypr/hyprland.conf`:

```conf
# Overview mode settings
plugin {
    hyprtileexpo {
        gap_size = 10        # Gap between workspace thumbnails in overview
        bg_col = 0xFF111111  # Background color (ARGB format)
    }
}
```

## Dispatchers

Hyprtile provides custom dispatchers for workspace and window management. Add these to your Hyprland keybindings.

### Workspace Navigation

| Dispatcher | Description | Example |
|------------|-------------|---------|
| `hyprtile:workspace <number>` | Switch to the last focused workspace in column `<number>` | `bind = $mod, 1, hyprtile:workspace, 1` |
| `hyprtile:movefocus <l/r/u/d>` | Move focus in the specified direction (left/right/up/down) | `bind = $mod, left, hyprtile:movefocus, l` |
| `hyprtile:movefocustomonitor <l/r/u/d>` | Move focus to the next monitor in the specified direction | `bind = $mod SHIFT, left, hyprtile:movefocustomonitor, l` |

### Window Management

| Dispatcher | Description | Example |
|------------|-------------|---------|
| `hyprtile:movewindow <l/r/u/d>` | Move the focused window in the specified direction, can cross workspace boundaries | `bind = $mod SHIFT, right, hyprtile:movewindow, r` |
| `hyprtile:movetoworkspace <number>` | Move the focused window to the last focused workspace in column `<number>` | `bind = $mod SHIFT, 2, hyprtile:movetoworkspace, 2` |
| `hyprtile:movetoworkspacesilent <number>` | Move window to workspace in column `<number>` without switching focus | `bind = $mod CTRL, 3, hyprtile:movetoworkspacesilent, 3` |

### Column Management

| Dispatcher | Description | Example |
|------------|-------------|---------|
| `hyprtile:clearworkspace` | Remove all empty workspaces from the current column | `bind = $mod, C, hyprtile:clearworkspace` |
| `hyprtile:insertworkspace` | Insert a new workspace at the current position, pushing workspaces below down | `bind = $mod, I, hyprtile:insertworkspace` |
| `hyprtile:movecurrentworkspacetomonitor <l/r/u/d>` | Move the current column to the next monitor | `bind = $mod ALT, right, hyprtile:movecurrentworkspacetomonitor, r` |

### Overview Mode

| Dispatcher | Description | Example |
|------------|-------------|---------|
| `hyprtile:expo <toggle/on/off>` | Toggle/enable/disable overview mode showing all workspaces in a grid | `bind = $mod, Tab, hyprtile:expo, toggle` |

## Usage Examples

### Basic Keybindings

Here's a suggested keybinding configuration for `~/.config/hypr/hyprland.conf`:

```conf
# Workspace navigation
bind = $mod, 1, hyprtile:workspace, 1
bind = $mod, 2, hyprtile:workspace, 2
bind = $mod, 3, hyprtile:workspace, 3
bind = $mod, 4, hyprtile:workspace, 4

# Directional focus
bind = $mod, left,  hyprtile:movefocus, l
bind = $mod, right, hyprtile:movefocus, r
bind = $mod, up,    hyprtile:movefocus, u
bind = $mod, down,  hyprtile:movefocus, d

# Move windows
bind = $mod SHIFT, left,  hyprtile:movewindow, l
bind = $mod SHIFT, right, hyprtile:movewindow, r
bind = $mod SHIFT, up,    hyprtile:movewindow, u
bind = $mod SHIFT, down,  hyprtile:movewindow, d

# Move window to column
bind = $mod SHIFT, 1, hyprtile:movetoworkspace, 1
bind = $mod SHIFT, 2, hyprtile:movetoworkspace, 2
bind = $mod SHIFT, 3, hyprtile:movetoworkspace, 3
bind = $mod SHIFT, 4, hyprtile:movetoworkspace, 4

# Overview mode
bind = $mod, Tab, hyprtile:expo, toggle

# Column management
bind = $mod, C, hyprtile:clearworkspace
bind = $mod, I, hyprtile:insertworkspace
```

### Workflow Example

1. **Start with workspace `1`** (column 1, position 0)
2. **Create workspace `1a`**: Use `hyprtile:insertworkspace` or move a window down
3. **Navigate down**: Press `$mod + down` to move to workspace `1a`
4. **Switch to column 2**: Press `$mod + 2` to go to workspace `2`
5. **Move window between columns**: Press `$mod SHIFT + left` to move the current window to column 1
6. **Overview all workspaces**: Press `$mod + Tab` to see the grid view

## Development

### Building

```bash
make          # Build the plugin
make clean    # Clean build artifacts
```

### Reloading During Development

```bash
./reload.sh   # Unload and reload the plugin
```

## Roadmap

### Completed Features
- ✅ Plugin architecture
- ✅ General animation control
- ✅ Dispatcher for moving workspace within column
- ✅ Name-based workspace management
- ✅ Waybar integration
- ✅ Overview mode (single monitor)

### Planned Features
- ⏳ Dispatcher for moving workspace between columns
- ⏳ Window edge positioning when moving to adjacent workspaces
- ⏳ Overview mode for multiple monitors
- ⏳ Gesture support

## Contributing

Contributions are welcome! Feel free to open issues or submit pull requests.

## License

See LICENSE file for details.
