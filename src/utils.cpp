#include "utils.h"

#include "hyprland/src/plugins/PluginAPI.hpp"

std::string remove_padding(const std::string &name)
{
    // remove zero-width characters
    std::string result;
    for (char c : name)
    {
        if (c != '\xE2' && c != '\x80' && c != '\x8B') // zero-width space
        {
            result += c;
        }
    }
    return result;
}

std::string generate_padding(int digits)
{
    std::string padding;
    for (int i = 0; i < digits - 1; ++i)
    {
        padding += "\xE2\x80\x8B"; // zero-width character
    }
    return padding;
}

int name_to_column(const std::string &name)
{
    std::string clean_name = remove_padding(name);
    auto end_pos = clean_name.find_first_not_of("0123456789");
    if (end_pos == 0)
    {
        return -1;
    }

    return std::stoi(clean_name.substr(0, end_pos));
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
    // to make 10 sorted after 1a
    int digits = std::log10(column_id) + 1;

    if (index == 0)
    {
        return generate_padding(digits) + std::to_string(column_id);
    }
    else
    {
        // return std::to_string(column_id) + static_cast<char>('a' + index - 1);
        // return std::to_string(column_id) + "." + std::to_string(index);
        // return std::to_string(column_id) + "\xE2\x80\x8B" + static_cast<char>('a' + index - 1);

        return generate_padding(digits) + std::to_string(column_id) + static_cast<char>('a' + index - 1);
    }
}

// when I am too lazy to use logging
void notify(const std::string &message)
{
    HyprlandAPI::invokeHyprctlCommand("notify", "1 3000 0 " + message);
}
