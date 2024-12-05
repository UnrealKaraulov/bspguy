#pragma once

#ifdef WIN_XP_86
typedef struct IUnknown IUnknown;
#ifndef E_BOUNDS
#define E_BOUNDS _HRESULT_TYPEDEF_(0x8000000BL)
#endif
#endif


#include "lang_defs.h"
#include <string>
#include "ini.h"

extern inih::INIReader * lang_ini;

extern std::map<int, std::string> lang_db;

std::string get_localized_string(int id);
std::string get_localized_string(const std::string& str_id);

void set_localize_lang(const std::string & lang);