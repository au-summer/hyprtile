#pragma once

#include <cmath>
#include <string>

std::string remove_padding(const std::string &name);
std::string generate_padding(int digits);
int name_to_column(const std::string &name);
int name_to_index(const std::string &name);
std::string get_workspace_name(int column_id, int index);

void notify(const std::string &message);
