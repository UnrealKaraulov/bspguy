#include "Keyvalue.h"
#include "util.h"

Keyvalues::Keyvalues(std::string& line)
{
	keys.clear();
	values.clear();

	std::vector<std::string> allstrings = splitString(line, "\"");
	if (allstrings.size() > 1)
	{
		if (allstrings[0].find('{') != std::string::npos)
		{
			allstrings.erase(allstrings.begin());
		}
		while (allstrings.size() > 2)
		{
			std::string tmpkey = allstrings[0];
			std::string tmpvalue = allstrings[2];
			allstrings.erase(allstrings.begin());
			allstrings.erase(allstrings.begin());
			allstrings.erase(allstrings.begin());
			if (allstrings.size() > 1)
				allstrings.erase(allstrings.begin());
			keys.push_back(tmpkey);
			values.push_back(tmpvalue);
		}
	}
	line.clear();
	if (!allstrings.empty())
	{
		line = allstrings[allstrings.size() - 1];
	}
}

Keyvalues::Keyvalues(void)
{
	keys.clear();
	values.clear();
}

Keyvalues::Keyvalues(const std::string & key, const std::string & value)
{
	keys.push_back(key);
	values.push_back(value);
}