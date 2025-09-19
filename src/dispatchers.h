#pragma once

#include <hyprland/src/SharedDefs.hpp>

#include <string>

// extern char anim_type;

namespace dispatchers
{

void addDispatchers();
SDispatchResult dispatch_movefocus(std::string arg);
SDispatchResult dispatch_movewindow(std::string arg);
SDispatchResult dispatch_movetoworkspace(std::string arg);
SDispatchResult dispatch_movetoworkspacesilent(std::string arg);
SDispatchResult dispatch_cleancurrentcolumn(std::string arg);
SDispatchResult dispatch_insertworkspace(std::string arg);
SDispatchResult dispatch_moveworkspace(std::string arg);
SDispatchResult dispatch_movecurrentcolumntomonitor(std::string arg);
SDispatchResult dispatch_movefocustomonitor(std::string arg);

} // namespace dispatchers
