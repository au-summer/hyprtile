#include "overview.h"
#include <any>
#define private public
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/managers/animation/AnimationManager.hpp>
#include <hyprland/src/managers/animation/DesktopAnimationManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#undef private
#include "OverviewPassElement.h"
#include <algorithm>
#include <cctype>

/**
 * Animation callback to damage the monitor when animated variables change
 */
static void damageMonitor(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr)
{
    g_pOverview->damage();
}

/**
 * Animation callback to remove the overview when animation completes
 */
static void removeOverview(WP<Hyprutils::Animation::CBaseAnimatedVariable> thisptr)
{
    g_pOverview.reset();
}

COverview::~COverview()
{
    // Clean up framebuffers to avoid VRAM leak
    g_pHyprRenderer->makeEGLCurrent();
    for (auto &col : columns)
    {
        col.framebuffers.clear();
    }
    g_pInputManager->unsetCursorImage();
    g_pHyprOpenGL->markBlurDirtyForMonitor(pMonitor.lock());
}

COverview::COverview(PHLWORKSPACE startedOn_) : startedOn(startedOn_)
{
    const auto PMONITOR = g_pCompositor->m_lastMonitor.lock();
    pMonitor = PMONITOR;

    // Get configuration values
    static auto *const *PGAPS =
        (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtileexpo:gap_size")
            ->getDataStaticPtr();
    static auto *const *PCOL =
        (Hyprlang::INT *const *)HyprlandAPI::getConfigValue(PHANDLE, "plugin:hyprtileexpo:bg_col")->getDataStaticPtr();

    GAP_SIZE = **PGAPS;
    BG_COLOR = **PCOL;

    g_pHyprRenderer->makeEGLCurrent();

    // Collect and organize workspaces into columns
    collectWorkspaces();

    // Calculate layout (position and size of each workspace thumbnail)
    calculateLayout();

    // Render all workspace thumbnails to framebuffers
    renderWorkspaces();

    // Set up animations - start zoomed in on current workspace, then zoom out to overview
    // Calculate the size and position of the current workspace's box in the overview
    Vector2D wsBoxSize = {pMonitor->m_size.x / 2, pMonitor->m_size.y / 2}; // Default fallback
    Vector2D wsBoxPos = {0, 0};

    if (openedColIdx >= 0 && openedColIdx < (int)columns.size() && openedWsIdx >= 0 &&
        openedWsIdx < (int)columns[openedColIdx].boxes.size())
    {
        const auto &box = columns[openedColIdx].boxes[openedWsIdx];
        wsBoxSize = {box.w, box.h};
        wsBoxPos = {box.x, box.y};
    }
    else if (!columns.empty() && !columns[0].boxes.empty())
    {
        // Fallback to first workspace
        const auto &box = columns[0].boxes[0];
        wsBoxSize = {box.w, box.h};
        wsBoxPos = {box.x, box.y};
    }

    // Calculate zoom factor - how much bigger the monitor is compared to the workspace box
    Vector2D zoomFactor = pMonitor->m_size / wsBoxSize;

    // Initial state: zoomed in on the current workspace
    Vector2D initialSize = pMonitor->m_size * zoomFactor;
    Vector2D initialPos = -wsBoxPos * zoomFactor;

    g_pAnimationManager->createAnimation(initialSize, size, g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
                                         AVARDAMAGE_NONE);
    g_pAnimationManager->createAnimation(initialPos, pos, g_pConfigManager->getAnimationPropertyConfig("windowsMove"),
                                         AVARDAMAGE_NONE);

    size->setUpdateCallback(damageMonitor);
    pos->setUpdateCallback(damageMonitor);

    // Animate to overview layout (normal size and position)
    *size = pMonitor->m_size;
    *pos = {0, 0};

    // Set up mouse cursor
    g_pInputManager->setCursorImageUntilUnset("left_ptr");
    lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;

    // Hook mouse movement to track cursor position
    auto onCursorMove = [this](void *self, SCallbackInfo &info, std::any param) {
        if (closing)
            return;

        info.cancelled = true;
        lastMousePosLocal = g_pInputManager->getMouseCoordsInternal() - pMonitor->m_position;
    };

    // Hook mouse click to select workspace
    auto onCursorSelect = [this](void *self, SCallbackInfo &info, std::any param) {
        if (closing)
            return;

        info.cancelled = true;
        selectHoveredWorkspace();
        close();
    };

    mouseMoveHook = g_pHookSystem->hookDynamic("mouseMove", onCursorMove);
    mouseButtonHook = g_pHookSystem->hookDynamic("mouseButton", onCursorSelect);
}

/**
 * Parse column number from workspace name.
 * Examples: "1" -> 1, "1a" -> 1, "2b" -> 2, "10" -> 10
 * Handles zero-width padding characters used by hyprtile for sorting.
 */
int COverview::parseColumn(const std::string &name)
{
    std::string clean_name;

    // Remove zero-width space characters (U+200B = E2 80 8B in UTF-8)
    for (size_t i = 0; i < name.length(); ++i)
    {
        if (i + 2 < name.length() && (unsigned char)name[i] == 0xE2 && (unsigned char)name[i + 1] == 0x80 &&
            (unsigned char)name[i + 2] == 0x8B)
        {
            i += 2; // Skip zero-width space
            continue;
        }
        clean_name += name[i];
    }

    // Find first non-digit character
    auto end_pos = clean_name.find_first_not_of("0123456789");
    if (end_pos == 0)
        return -1; // Not a valid hyprtile workspace name

    try
    {
        return std::stoi(clean_name.substr(0, end_pos));
    }
    catch (...)
    {
        return -1;
    }
}

/**
 * Parse index within column from workspace name.
 * Examples: "1" -> 0, "1a" -> 1, "1b" -> 2, "1z" -> 26
 */
int COverview::parseIndex(const std::string &name)
{
    if (name.empty())
        return -1;

    char last_char = name.back();

    // If name ends with a digit, it's the base workspace (index 0)
    if (std::isdigit(last_char))
        return 0;

    // If it ends with a letter, calculate index
    if (std::isalpha(last_char) && std::islower(last_char))
        return last_char - 'a' + 1;

    return -1;
}

/**
 * Collect all workspaces and organize them into columns.
 * Creates the column structure used for layout and rendering.
 */
void COverview::collectWorkspaces()
{
    std::map<int, SWorkspaceColumn> columnMap;

    // Iterate through all workspaces on this monitor
    for (const auto &ws : g_pCompositor->getWorkspaces())
    {
        // Skip workspaces on other monitors
        if (ws->m_monitor != pMonitor->m_id)
            continue;

        // Parse workspace name
        int col = parseColumn(ws->m_name);
        int idx = parseIndex(ws->m_name);

        // Skip invalid or special workspaces
        if (col == -1 || idx == -1)
            continue;

        // Add to column map
        if (columnMap.find(col) == columnMap.end())
        {
            columnMap[col].columnID = col;
        }

        auto &column = columnMap[col];

        // Ensure vectors are large enough
        if ((size_t)idx >= column.workspaceIDs.size())
        {
            column.workspaceIDs.resize(idx + 1, -1);
            column.workspaces.resize(idx + 1, nullptr);
        }

        column.workspaceIDs[idx] = ws->m_id;
        column.workspaces[idx] = ws.lock(); // Convert weak pointer to shared pointer

        // Track which workspace was active when we opened
        if (ws.lock() == startedOn)
        {
            openedColIdx = -1; // Will be set after we convert map to vector
            openedWsIdx = idx;
        }
    }

    // Convert map to vector (sorted by column ID)
    for (auto &[colID, column] : columnMap)
    {
        columns.push_back(std::move(column));
    }

    // Find the opened workspace column index
    for (size_t i = 0; i < columns.size(); ++i)
    {
        for (size_t j = 0; j < columns[i].workspaces.size(); ++j)
        {
            if (columns[i].workspaces[j] == startedOn)
            {
                openedColIdx = i;
                openedWsIdx = j;
                break;
            }
        }
        if (openedColIdx != -1)
            break;
    }

    // If we didn't find the opened workspace, default to first workspace
    if (openedColIdx == -1 && !columns.empty())
    {
        openedColIdx = 0;
        openedWsIdx = 0;
    }
}

/**
 * Calculate layout for all workspace thumbnails.
 * Arranges columns horizontally and workspaces within columns vertically.
 * Maintains aspect ratio of the monitor for each workspace thumbnail.
 */
void COverview::calculateLayout()
{
    if (columns.empty())
        return;

    const Vector2D monitorSize = pMonitor->m_size;
    const double monitorAspect = monitorSize.x / monitorSize.y;

    // Calculate total width needed (columns side by side with gaps)
    const double totalGapWidth = GAP_SIZE * (columns.size() - 1);
    const double columnWidth = (monitorSize.x - totalGapWidth) / columns.size();

    double xOffset = 0;

    for (auto &column : columns)
    {
        const int wsCount = column.workspaces.size();
        if (wsCount == 0)
            continue;

        // Calculate total available height for this column
        const double totalGapHeight = GAP_SIZE * (wsCount - 1);
        const double availableHeight = monitorSize.y - totalGapHeight;

        // Calculate workspace height while maintaining aspect ratio
        // Each workspace should have the same aspect ratio as the monitor
        const double wsWidth = columnWidth;
        const double wsHeight = wsWidth / monitorAspect;

        // If workspaces don't fit vertically, scale them down
        const double totalNeededHeight = wsHeight * wsCount;
        double scaleFactor = 1.0;
        if (totalNeededHeight > availableHeight)
        {
            scaleFactor = availableHeight / totalNeededHeight;
        }

        const double finalWsHeight = wsHeight * scaleFactor;
        const double finalWsWidth = wsWidth * scaleFactor;

        // Center the column horizontally if it was scaled down
        const double xPadding = (columnWidth - finalWsWidth) / 2.0;

        // Allocate framebuffers and boxes
        column.framebuffers.resize(wsCount);
        column.boxes.resize(wsCount);

        double yOffset = 0;

        for (int i = 0; i < wsCount; ++i)
        {
            column.boxes[i] = CBox{xOffset + xPadding, yOffset, finalWsWidth, finalWsHeight};

            yOffset += finalWsHeight + GAP_SIZE;
        }

        xOffset += columnWidth + GAP_SIZE;
    }
}

/**
 * Render all workspace thumbnails to framebuffers.
 * This captures the current state of each workspace.
 */
void COverview::renderWorkspaces()
{
    PHLWORKSPACE openSpecial = pMonitor->m_activeSpecialWorkspace;
    if (openSpecial)
        pMonitor->m_activeSpecialWorkspace.reset();

    g_pHyprRenderer->m_bBlockSurfaceFeedback = true;
    startedOn->m_visible = false;

    for (size_t colIdx = 0; colIdx < columns.size(); ++colIdx)
    {
        auto &column = columns[colIdx];

        for (size_t wsIdx = 0; wsIdx < column.workspaces.size(); ++wsIdx)
        {
            redrawWorkspace(colIdx, wsIdx);
        }
    }

    g_pHyprRenderer->m_bBlockSurfaceFeedback = false;
    pMonitor->m_activeSpecialWorkspace = openSpecial;
    pMonitor->m_activeWorkspace = startedOn;
    startedOn->m_visible = true;
    g_pDesktopAnimationManager->startAnimation(startedOn, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);
}

/**
 * Redraw a specific workspace thumbnail.
 * @param colIdx Column index
 * @param wsIdx Workspace index within column
 */
void COverview::redrawWorkspace(int colIdx, int wsIdx)
{
    if (colIdx < 0 || colIdx >= (int)columns.size())
        return;

    auto &column = columns[colIdx];
    if (wsIdx < 0 || wsIdx >= (int)column.workspaces.size())
        return;

    const auto PWORKSPACE = column.workspaces[wsIdx];
    auto &fb = column.framebuffers[wsIdx];

    // Allocate framebuffer at full monitor resolution to preserve quality
    CBox monbox = {{0, 0}, pMonitor->m_pixelSize};
    if (fb.m_size != monbox.size())
    {
        fb.release();
        fb.alloc(monbox.w, monbox.h, pMonitor->m_output->state->state().drmFormat);
    }

    blockOverviewRendering = true;

    // Render workspace to framebuffer
    CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
    g_pHyprRenderer->beginRender(pMonitor.lock(), fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, &fb);

    g_pHyprOpenGL->clear(CHyprColor{0, 0, 0, 1.0});

    PHLWORKSPACE openSpecial = pMonitor->m_activeSpecialWorkspace;
    if (openSpecial)
        pMonitor->m_activeSpecialWorkspace.reset();

    bool wasVisible = startedOn->m_visible;
    startedOn->m_visible = false;

    if (PWORKSPACE)
    {
        pMonitor->m_activeWorkspace = PWORKSPACE;
        g_pDesktopAnimationManager->startAnimation(PWORKSPACE, CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);
        PWORKSPACE->m_visible = true;

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace = openSpecial;

        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);

        PWORKSPACE->m_visible = false;
        g_pDesktopAnimationManager->startAnimation(PWORKSPACE, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false,
                                                   true);

        if (PWORKSPACE == startedOn)
            pMonitor->m_activeSpecialWorkspace.reset();
    }
    else
    {
        g_pHyprRenderer->renderWorkspace(pMonitor.lock(), PWORKSPACE, Time::steadyNow(), monbox);
    }

    g_pHyprOpenGL->m_renderData.blockScreenShader = true;
    g_pHyprRenderer->endRender();

    pMonitor->m_activeSpecialWorkspace = openSpecial;
    pMonitor->m_activeWorkspace = startedOn;
    startedOn->m_visible = wasVisible;

    blockOverviewRendering = false;
}

void COverview::damage()
{
    blockDamageReporting = true;
    g_pHyprRenderer->damageMonitor(pMonitor.lock());
    blockDamageReporting = false;
}

void COverview::onDamageReported()
{
    damageDirty = true;
    damage();
    g_pCompositor->scheduleFrameForMonitor(pMonitor.lock());
}

void COverview::onPreRender()
{
    if (damageDirty)
    {
        damageDirty = false;
        // Redraw the currently focused workspace
        if (openedColIdx >= 0 && openedWsIdx >= 0)
            redrawWorkspace(openedColIdx, openedWsIdx);
    }
}

/**
 * Select the workspace currently under the mouse cursor.
 * Sets closeOnColIdx and closeOnWsIdx for the close() function.
 */
void COverview::selectHoveredWorkspace()
{
    if (closing)
        return;

    // Find which workspace the mouse is over
    for (size_t colIdx = 0; colIdx < columns.size(); ++colIdx)
    {
        const auto &column = columns[colIdx];

        for (size_t wsIdx = 0; wsIdx < column.boxes.size(); ++wsIdx)
        {
            const auto &box = column.boxes[wsIdx];

            if (box.containsPoint(lastMousePosLocal))
            {
                closeOnColIdx = colIdx;
                closeOnWsIdx = wsIdx;
                return;
            }
        }
    }
}

/**
 * Close the overview and optionally switch to selected workspace.
 * Animates back to full size and removes the overview.
 */
void COverview::close()
{
    if (closing)
        return;

    closing = true;

    // If no workspace was selected, close on the workspace we opened with
    if (closeOnColIdx == -1)
    {
        closeOnColIdx = openedColIdx;
        closeOnWsIdx = openedWsIdx;
    }

    // Calculate zoom animation for closing
    Vector2D wsBoxSize = {pMonitor->m_size.x / 2, pMonitor->m_size.y / 2}; // Default fallback
    Vector2D wsBoxPos = {0, 0};

    if (closeOnColIdx >= 0 && closeOnColIdx < (int)columns.size() && closeOnWsIdx >= 0 &&
        closeOnWsIdx < (int)columns[closeOnColIdx].boxes.size())
    {
        const auto &box = columns[closeOnColIdx].boxes[closeOnWsIdx];
        wsBoxSize = {box.w, box.h};
        wsBoxPos = {box.x, box.y};
    }

    // Calculate zoom factor
    Vector2D zoomFactor = pMonitor->m_size / wsBoxSize;

    // Animate back to zoomed-in state
    *size = pMonitor->m_size * zoomFactor;
    *pos = -wsBoxPos * zoomFactor;

    size->setCallbackOnEnd(removeOverview);

    // Switch workspace if needed
    if (closeOnColIdx >= 0 && closeOnColIdx < (int)columns.size() && closeOnWsIdx >= 0 &&
        closeOnWsIdx < (int)columns[closeOnColIdx].workspaces.size())
    {

        const auto TARGETWS = columns[closeOnColIdx].workspaces[closeOnWsIdx];

        if (TARGETWS && TARGETWS != pMonitor->m_activeWorkspace)
        {
            pMonitor->setSpecialWorkspace(0);

            const auto OLDWS = pMonitor->m_activeWorkspace;

            // Use changeworkspace to properly switch workspaces
            if (TARGETWS)
                g_pKeybindManager->changeworkspace(TARGETWS->getConfigName());

            g_pDesktopAnimationManager->startAnimation(pMonitor->m_activeWorkspace,
                                                       CDesktopAnimationManager::ANIMATION_TYPE_IN, true, true);
            g_pDesktopAnimationManager->startAnimation(OLDWS, CDesktopAnimationManager::ANIMATION_TYPE_OUT, false,
                                                       true);

            startedOn = pMonitor->m_activeWorkspace;
        }
    }
}

void COverview::render()
{
    g_pHyprRenderer->m_renderPass.add(makeUnique<COverviewPassElement>());
}

/**
 * Perform the actual rendering of the overview.
 * Called by COverviewPassElement::draw().
 */
void COverview::fullRender()
{
    g_pHyprOpenGL->clear(BG_COLOR.stripA());

    Vector2D SIZE = size->value();

    // Calculate scaling factor
    const double scale = SIZE.x / pMonitor->m_size.x;

    for (size_t colIdx = 0; colIdx < columns.size(); ++colIdx)
    {
        auto &column = columns[colIdx];

        for (size_t wsIdx = 0; wsIdx < column.workspaces.size(); ++wsIdx)
        {
            if (wsIdx >= column.framebuffers.size() || wsIdx >= column.boxes.size())
                continue;

            auto &fb = column.framebuffers[wsIdx];
            const auto &originalBox = column.boxes[wsIdx];

            // Scale and position the workspace thumbnail
            CBox texbox = {originalBox.x * scale + pos->value().x, originalBox.y * scale + pos->value().y,
                           originalBox.w * scale, originalBox.h * scale};

            texbox.scale(pMonitor->m_scale);
            texbox.round();

            CRegion damage{0, 0, INT16_MAX, INT16_MAX};
            g_pHyprOpenGL->renderTextureInternal(fb.getTexture(), texbox, {.damage = &damage, .a = 1.0});
        }
    }
}
