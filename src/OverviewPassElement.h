#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>

class COverview;

/**
 * Render pass element for the workspace overview.
 * This integrates with Hyprland's rendering pipeline to draw the overview.
 */
class COverviewPassElement : public IPassElement {
  public:
    COverviewPassElement();
    virtual ~COverviewPassElement() = default;

    // Required IPassElement interface implementations
    virtual void                draw(const CRegion& damage);
    virtual bool                needsLiveBlur();
    virtual bool                needsPrecomputeBlur();
    virtual std::optional<CBox> boundingBox();
    virtual CRegion             opaqueRegion();

    virtual const char*         passName() {
        return "COverviewPassElement";
    }
};
