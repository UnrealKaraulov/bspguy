#pragma once
#include "lang_defs.h"
#include <string>
#include "iniparser.hpp"

extern INI::File* ft;

extern std::map<int, std::string> lang_db;
extern std::string lang_selected;

std::string get_localized_string(int id);
void set_localize_lang(std::string lang);