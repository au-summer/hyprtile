#include "OverviewPassElement.h"
#include "overview.h"
#include <hyprland/src/render/OpenGL.hpp>

/**
 * Constructor for the overview render pass element.
 */
COverviewPassElement::COverviewPassElement()
{
    ;
}

/**
 * Draw the overview to the screen.
 * Called by Hyprland's rendering pipeline.
 */
void COverviewPassElement::draw(const CRegion &damage)
{
    g_pOverview->fullRender();
}

/**
 * Indicates whether this element needs live blur.
 * Returns false to save resources.
 */
bool COverviewPassElement::needsLiveBlur()
{
    return false;
}

/**
 * Indicates whether this element needs precomputed blur.
 * Returns false to save resources.
 */
bool COverviewPassElement::needsPrecomputeBlur()
{
    return false;
}

/**
 * Returns the bounding box of this render element.
 * The overview covers the entire monitor.
 */
std::optional<CBox> COverviewPassElement::boundingBox()
{
    if (!g_pOverview->pMonitor)
        return std::nullopt;

    return CBox{{}, g_pOverview->pMonitor->m_size};
}

/**
 * Returns the opaque region of this render element.
 * The overview is fully opaque across the entire monitor.
 */
CRegion COverviewPassElement::opaqueRegion()
{
    if (!g_pOverview->pMonitor)
        return CRegion{};

    return CBox{{}, g_pOverview->pMonitor->m_size};
}
