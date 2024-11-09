#pragma once
#include <fmt/format.h>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <mutex>
#include <fstream>

#include "util.h"

enum PRINT_CONST : unsigned int
{
	PRINT_BLUE = 1,
	PRINT_GREEN = 2,
	PRINT_RED = 4,
	PRINT_INTENSITY = 8
};

#define DEFAULT_CONSOLE_COLOR (PRINT_BLUE | PRINT_GREEN | PRINT_RED | PRINT_INTENSITY)

extern std::vector<std::string> g_log_buffer;
extern std::vector<unsigned int> g_color_buffer;

extern std::vector<std::string> g_console_log_buffer;
extern std::vector<unsigned int> g_console_color_buffer;

extern bool g_console_visible;
void showConsoleWindow(bool show);
void set_console_colors(unsigned int colors = DEFAULT_CONSOLE_COLOR);

template<class ...Args>
void print_log(unsigned int colors, const std::string& format, Args ...args) noexcept
{
	std::string line = fmt::vformat(format, fmt::make_format_args(args...));

	if (!line.size())
		return;

	g_mutex_list[5].lock();
	g_console_log_buffer.push_back(line);
	g_console_color_buffer.push_back(colors);
	g_mutex_list[5].unlock();

	g_mutex_list[0].lock();

	//replaceAll(line, " ", "+");
	auto newline = ends_with(line,'\n');
	auto splitstr = splitString(line, "\n");

	bool ret = line[0] == '\r';
	if (ret)
		line.erase(line.begin());

	if (splitstr.size() == 1)
	{
		if (!newline && g_log_buffer.size())
		{
			if (ret)
			{
				g_log_buffer[g_log_buffer.size() - 1] = std::move(line);
				g_color_buffer[g_log_buffer.size() - 1] = colors;
			}
			else
			{
				g_log_buffer[g_log_buffer.size() - 1] += line;
				g_color_buffer[g_log_buffer.size() - 1] = colors;
			}
		}
		else
		{
			line.pop_back();

			g_log_buffer[g_log_buffer.size() - 1] = std::move(line);
			g_color_buffer[g_log_buffer.size() - 1] = colors;

			g_log_buffer.emplace_back("");
			g_color_buffer.emplace_back(DEFAULT_CONSOLE_COLOR);
		}
	}
	else
	{
		for (auto& s : splitstr)
		{
			if (s.size())
			{
				g_log_buffer[g_log_buffer.size() - 1] = s;
				g_color_buffer[g_log_buffer.size() - 1] = colors;

				g_log_buffer.emplace_back("");
				g_color_buffer.emplace_back(DEFAULT_CONSOLE_COLOR);
			}
		}
	}
	g_mutex_list[0].unlock();
}

template<class ...Args>
void print_log(const std::string& format, Args ...args) noexcept
{
	std::string line = fmt::vformat(format, fmt::make_format_args(args...));
	print_log(DEFAULT_CONSOLE_COLOR, "{}", line);
}

extern double flushConsoleTime;
void FlushConsoleLog();