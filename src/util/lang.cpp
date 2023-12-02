#pragma once
#include "lang.h"
#include "Settings.h"
#include "util.h"
#include <fmt/format.h>

INI::File * ft = NULL;

std::map<int, std::string> lang_db;
std::map<std::string, std::string> lang_db_str;

std::string get_localized_string(int id)
{
	if (ft == NULL)
	{
		set_localize_lang("EN");
	}

	std::map<int, std::string>::iterator itr = lang_db.find(id);

	if (itr == lang_db.end())
	{
		std::string value = ft->GetSection(g_settings.language)->GetValue(fmt::format("LANG_{:04}", id), fmt::format("LANG_{:04}", id)).AsString();
		replaceAll(value, "\\n", "\n");
		lang_db[id] = value;
		return value;
	}

	return itr->second;
}

std::string get_localized_string(const std::string & str_id)
{
	if (ft == NULL)
	{
		set_localize_lang("EN");
	}

	std::map<std::string, std::string>::iterator itr = lang_db_str.find(str_id);

	if (itr == lang_db_str.end())
	{
		std::string value = ft->GetSection(g_settings.language)->GetValue(str_id,str_id).AsString();
		replaceAll(value, "\\n", "\n");
		lang_db_str[str_id] = value;
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
	lang_db_str.clear();
}