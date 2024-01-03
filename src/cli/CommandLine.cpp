#include "lang.h"
#include "CommandLine.h"
#ifdef WIN32
#include <Windows.h>
#ifdef WIN_XP_86
#include <shellapi.h>
#endif
#endif
CommandLine::CommandLine(int argc, char* argv[])
{
	askingForHelp = false;
	for (int i = 0; i < argc; i++)
	{
		std::string arg = argv[i];
		std::string larg = toLowerCase(arg);

		if (i == 1)
		{
			command = larg;
		}
		if (i == 2)
		{
#ifdef WIN32
			int nArgs;
			LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
			bspfile = fs::path(szArglist[i]).string();
#else 
			bspfile = fs::path(argv[i]).string();
#endif
		}
		if (i > 2)
		{
			options.push_back(arg);
		}

		if ((i == 1 || i == 2) && larg.starts_with("help") || larg.starts_with("/?") || larg.starts_with("--help") || larg.starts_with("-help") || larg.starts_with("-h"))
		{
			askingForHelp = true;
		}
	}

	if (askingForHelp)
	{
		return;
	}

	for (size_t i = 0; i < options.size(); i++)
	{
		std::string opt = toLowerCase(options[i]);

		if (i < options.size() - 1)
		{
			optionVals[opt] = options[i + 1];
		}
		else
		{
			optionVals[opt].clear();
		}
	}

	if (argc == 2)
	{
#ifdef WIN32
		int nArgs;
		LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
		bspfile = fs::path(szArglist[1]).string();
#else
		bspfile = fs::path(argv[1]).string();
#endif
	}
}

bool CommandLine::hasOption(const std::string& optionName)
{
	return optionVals.find(optionName) != optionVals.end();
}

bool CommandLine::hasOptionVector(const std::string& optionName)
{
	if (!hasOption(optionName))
		return false;

	std::string val = optionVals[optionName];
	std::vector<std::string> parts = splitString(val, ",");

	if (parts.size() != 3)
	{
		print_log(get_localized_string(LANG_0265),optionName);
		return false;
	}

	return true;
}

std::string CommandLine::getOption(const std::string& optionName)
{
	return optionVals[optionName];
}

int CommandLine::getOptionInt(const std::string& optionName)
{
	return atoi(optionVals[optionName].c_str());
}

vec3 CommandLine::getOptionVector(const std::string& optionName)
{
	vec3 ret;
	std::vector<std::string> parts = splitString(optionVals[optionName], ",");

	if (parts.size() != 3)
	{
		print_log(get_localized_string(LANG_1045),optionName);
		return ret;
	}

	ret.x = (float)atof(parts[0].c_str());
	ret.y = (float)atof(parts[1].c_str());
	ret.z = (float)atof(parts[2].c_str());

	return ret;
}

std::vector<std::string> CommandLine::getOptionList(const std::string& optionName)
{
	std::vector<std::string> parts = splitString(optionVals[optionName], ",");

	for (size_t i = 0; i < parts.size(); i++)
	{
		parts[i] = trimSpaces(parts[i]);
	}

	return parts;
}
