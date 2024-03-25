#include "log.h"

#ifdef WIN32
#include <Windows.h>
#else 
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

std::vector<std::string> g_log_buffer = { "" };
std::vector<unsigned int> g_color_buffer = { 0 };

std::vector<std::string> g_console_log_buffer = { "" };
std::vector<unsigned int> g_console_color_buffer = { 0 };

unsigned int last_console_color = PRINT_BLUE | PRINT_GREEN | PRINT_RED | PRINT_INTENSITY;
double flushConsoleTime = 0.0;

void FlushConsoleLog()
{
	if (g_console_log_buffer.empty())
	{
		return;
	}

	std::thread t
	([]()
		{
			if (g_mutex_list[6].try_lock())
			{
				// copy for real async ?
				std::vector<std::string> tmp_log_buffer;
				std::vector<unsigned int> tmp_color_buffer;

				g_mutex_list[5].lock();
				tmp_log_buffer = g_console_log_buffer;
				tmp_color_buffer = g_console_color_buffer;
				g_console_log_buffer.clear();
				g_console_color_buffer.clear();
				g_mutex_list[5].unlock();
				

				for (size_t i = 0; i < tmp_log_buffer.size(); i++)
				{
					const std::string& str = tmp_log_buffer[i];
					unsigned int color = tmp_color_buffer[i];

#ifndef NDEBUG
					static std::ofstream outfile("log.txt", std::ios_base::app);
					outfile << str;
					outfile.flush();
#endif

					set_console_colors(color);
					std::cout << str;
				}



				g_mutex_list[6].unlock();
			}
		}
	);
	t.detach();
}


bool g_console_visible = true;
void showConsoleWindow(bool show)
{
	g_console_visible = show;
#ifdef WIN32		
	if (::GetConsoleWindow())
	{
		::ShowWindow(::GetConsoleWindow(), show ? SW_SHOW : SW_HIDE);
	}
#endif
}

#ifdef WIN32
void set_console_colors(unsigned int colors)
{
	static HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

	if (!console)
		console = ::GetConsoleWindow();
	if (console)
	{
		SetConsoleTextAttribute(console, (WORD)colors);
	}
}
#else 
void set_console_colors(unsigned int colors)
{
	colors = colors ? colors : (PRINT_GREEN | PRINT_BLUE | PRINT_RED | PRINT_INTENSITY);
	if (colors == 0)
	{
		std::cout << "\x1B[0m";
		return;
	}
	const char* mode = colors & PRINT_INTENSITY ? "1" : "0";
	const char* color = "37";
	switch (colors & ~PRINT_INTENSITY)
	{
	case PRINT_RED:								color = "31"; break;
	case PRINT_GREEN:							color = "32"; break;
	case PRINT_RED | PRINT_GREEN:				color = "33"; break;
	case PRINT_BLUE:							color = "34"; break;
	case PRINT_RED | PRINT_BLUE:				color = "35"; break;
	case PRINT_GREEN | PRINT_BLUE:				color = "36"; break;
	case PRINT_GREEN | PRINT_BLUE | PRINT_RED:	color = "36"; break;
	}
	std::cout << "\x1B[" << mode << ";" << color << "m";
}
#endif

