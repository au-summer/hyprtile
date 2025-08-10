#ifndef HYPRTILE_DISPATCHERS_H
#define HYPRTILE_DISPATCHERS_H

#include <hyprland/src/SharedDefs.hpp>

#include <string>

extern char anim_type;

namespace dispatchers
{

void addDispatchers();
SDispatchResult dispatch_movefocus(std::string arg);
SDispatchResult dispatch_movewindow(std::string arg);
SDispatchResult dispatch_movetoworkspace(std::string arg);
SDispatchResult dispatch_movetoworkspacesilent(std::string arg);
SDispatchResult dispatch_cleanworkspaces(std::string arg);
SDispatchResult dispatch_insertworkspace(std::string arg);
SDispatchResult dispatch_movecurrentworkspacetomonitor(std::string arg);
SDispatchResult dispatch_movefocustomonitor(std::string arg);

} // namespace dispatchers

#endif // HYPRTILE_DISPATCHERS_H
