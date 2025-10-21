#pragma once

#define WLR_USE_UNSTABLE

#include "globals.h"
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/AnimatedVariable.hpp>
#include <hyprland/src/managers/HookSystemManager.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <map>
#include <vector>

class CMonitor;

/**
 * Main overview class for hyprtileexpo.
 * Manages the workspace overview display, showing workspaces organized
 * in columns according to hyprtile's naming scheme (1, 1a, 1b, ..., 2, 2a, 2b, ...).
 */
class COverview
{
  public:
    /**
     * Constructor - initializes the overview
     * @param startedOn_ The workspace that was active when overview was opened
     */
    COverview(PHLWORKSPACE startedOn_);
    ~COverview();

    // Rendering functions
    void render();           // Adds render pass element
    void fullRender();       // Performs the actual rendering
    void damage();           // Marks the monitor as needing redraw
    void onDamageReported(); // Handles damage reports from compositor
    void onPreRender();      // Called before each frame render

    // Closing functions
    void close();                  // Closes overview and switches to selected workspace
    void selectHoveredWorkspace(); // Selects workspace under mouse cursor

    // Flags to control rendering
    bool blockOverviewRendering = false; // Prevents recursive rendering
    bool blockDamageReporting = false;   // Prevents recursive damage reports

    PHLMONITORREF pMonitor; // Monitor this overview is displayed on

  private:
    /**
     * Structure representing a workspace column in the overview.
     * Each column contains workspaces like 1, 1a, 1b, 1c...
     */
    struct SWorkspaceColumn
    {
        int columnID;                           // The column number (e.g., 1 for workspaces 1, 1a, 1b)
        std::vector<int64_t> workspaceIDs;      // Workspace IDs in this column, ordered by index
        std::vector<PHLWORKSPACE> workspaces;   // Workspace pointers
        std::vector<CFramebuffer> framebuffers; // Rendered workspace thumbnails
        std::vector<CBox> boxes;                // Position and size of each workspace thumbnail
    };

    /**
     * Helper functions for parsing hyprtile workspace names
     */
    int parseColumn(const std::string &name); // Extract column number from workspace name
    int parseIndex(const std::string &name);  // Extract index (0 for "1", 1 for "1a", etc.)

    /**
     * Layout calculation
     */
    void calculateLayout();                      // Calculates position and size of all workspace thumbnails
    void collectWorkspaces();                    // Collects and organizes workspaces into columns
    void renderWorkspaces();                     // Renders all workspace thumbnails to framebuffers
    void redrawWorkspace(int colIdx, int wsIdx); // Redraws a specific workspace thumbnail

    // Configuration
    int GAP_SIZE = 10;                                    // Gap between workspace thumbnails
    CHyprColor BG_COLOR = CHyprColor{0.1, 0.1, 0.1, 1.0}; // Background color

    // State
    bool damageDirty = false; // Whether damage has been reported
    bool closing = false;     // Whether overview is closing

    Vector2D lastMousePosLocal = Vector2D{}; // Last mouse position relative to monitor

    int openedColIdx = -1;  // Column index of workspace that was active when opened
    int openedWsIdx = -1;   // Workspace index within column
    int closeOnColIdx = -1; // Column to close on (for mouse selection)
    int closeOnWsIdx = -1;  // Workspace to close on (for mouse selection)

    std::vector<SWorkspaceColumn> columns; // All workspace columns to display

    PHLWORKSPACE startedOn; // Workspace that was active when overview was opened

    // Animation variables
    PHLANIMVAR<Vector2D> size; // Animated overview size
    PHLANIMVAR<Vector2D> pos;  // Animated overview position

    // Input hooks
    SP<HOOK_CALLBACK_FN> mouseMoveHook;
    SP<HOOK_CALLBACK_FN> mouseButtonHook;

    friend class COverviewPassElement;
};

// Global instance of the overview
inline std::unique_ptr<COverview> g_pOverview;
