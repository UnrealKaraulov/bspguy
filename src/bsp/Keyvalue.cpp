#include "Keyvalue.h"
#include "util.h"

const std::regex Keyvalues::kv_regex("\"(.*?)\"\\s*\"(.*?)\"");

Keyvalues::Keyvalues(std::string& line)
{
    keys.clear();
    values.clear();

    std::smatch matches;
    std::string remaining_line = line;

    while (std::regex_search(remaining_line, matches, kv_regex))
    {
        keys.push_back(matches[1]);
        values.push_back(matches[2]);
        remaining_line = matches.suffix().str();
    }

    line = remaining_line;
}

Keyvalues::Keyvalues(void)
{
	keys.clear();
	values.clear();
}

Keyvalues::Keyvalues(const std::string& key, const std::string& value)
{
	keys.push_back(key);
	values.push_back(value);
}