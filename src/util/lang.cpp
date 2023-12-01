#pragma once
#include "lang.h"
#include "Settings.h"
#include <fmt/format.h>

INI::File * ft = NULL;

std::map<int, std::string> lang_db;

std::string get_localized_string(int id)
{
	if (ft == NULL)
	{
		set_localize_lang("EN");
	}

	std::map<int,std::string>::iterator itr = lang_db.find(id);

	if (itr == lang_db.end())
	{
		std::string value = ft->GetSection(g_settings.language)->GetValue(fmt::format("LANG_{:04}",id), fmt::format("LANG_{:04}", id)).AsString();
		lang_db[id] = value;
		return value;
	}
	
	return itr->second;
}

void set_localize_lang(std::string lang)
{
	if (ft != NULL)
	{
		delete ft;
	}

	ft = new INI::File(g_config_dir + "language.ini");

	g_settings.language = lang;
	lang_db.clear();
}