#pragma once
#include "lang_defs.h"
#include <string>
#include "ini.h"

extern inih::INIReader * ft;

extern std::map<int, std::string> lang_db;
extern std::string lang_selected;

std::string get_localized_string(int id);
std::string get_localized_string(const std::string& str_id);

void set_localize_lang(std::string lang);