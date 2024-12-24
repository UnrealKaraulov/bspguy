#pragma once
#include "util.h"
#include <regex>

class Keyvalues
{
public:
	std::vector<std::string> keys;
	std::vector<std::string> values;

	Keyvalues(std::string& line);
	Keyvalues(const std::string& key, const std::string& value);
	Keyvalues(void);
	~Keyvalues(void) = default;
private:
	static const std::regex kv_regex;
};

