#ifndef HYPRTILE_UTILS_H
#define HYPRTILE_UTILS_H

#include <string>

int name_to_column(const std::string &name)
{
    auto end_pos = name.find_first_not_of("0123456789");
    if (end_pos == 0)
    {
        return -1;
    }

    return std::stoi(name.substr(0, end_pos));
}

int name_to_index(const std::string &name)
{
    char last_char = name.back();

    // if the name itself is a number, return 0
    if (std::isdigit(last_char))
    {
        return 0;
    }

    return last_char - 'a' + 1;
}

std::string get_workspace_name(int column_id, int index)
{
    if (index == 0)
    {
        return std::to_string(column_id);
    }
    else
    {
        return std::to_string(column_id) + static_cast<char>('a' + index - 1);
    }
}

#endif // HYPRTILE_UTILS_H
