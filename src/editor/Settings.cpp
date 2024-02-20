#include "lang.h"
#include "Settings.h"
#include "Renderer.h"
#include "util.h"
#include <iostream>
#include <fstream>
#include <string>

std::string g_settings_path = "";
std::string g_game_dir = "/";
std::string g_working_dir = "./";
std::string g_startup_dir = "";

AppSettings g_settings{};

void AppSettings::loadDefault()
{
	settingLoaded = false;

	windowWidth = 800;
	windowHeight = 600;
	windowX = 0;
#ifdef WIN32
	windowY = 30;
#else
	windowY = 0;
#endif
	maximized = 0;
	fontSize = 22.f;
	gamedir = std::string();
	workingdir = "./bspguy_work/";

	lastdir = "";
	selected_lang = "EN";
	languages.clear();
	languages.push_back("EN");

	undoLevels = 64;
	fpslimit = 100;

	verboseLogs = false;
#ifndef NDEBUG
	verboseLogs = true;
#endif
	save_windows = false;
	debug_open = false;
	keyvalue_open = false;
	transform_open = false;
	log_open = false;
	limits_open = false;
	entreport_open = false;
	goto_open = false;

	settings_tab = 0;

	render_flags = g_render_flags = RENDER_TEXTURES | RENDER_LIGHTMAPS | RENDER_SPECIAL
		| RENDER_ENTS | RENDER_SPECIAL_ENTS | RENDER_POINT_ENTS | RENDER_WIREFRAME | RENDER_ENT_CONNECTIONS
		| RENDER_ENT_CLIPNODES | RENDER_MODELS | RENDER_MODELS_ANIMATED;

	vsync = true;
	merge_verts = false;
	merge_edges = false;
	mark_unused_texinfos = false;
	start_at_entity = false;
	backUpMap = true;
	preserveCrc32 = false;
	save_cam = false;
	autoImportEnt = false;
	sameDirForEnt = false;

	moveSpeed = 500.0f;
	fov = 75.0f;
	zfar = 262144.0f;
	rotSpeed = 5.0f;

	fgdPaths.clear();
	resPaths.clear();


	rad_path = "hlrad.exe";
	rad_options = "{map_path}";

	conditionalPointEntTriggers.clear();
	entsThatNeverNeedAnyHulls.clear();
	entsThatNeverNeedCollision.clear();
	passableEnts.clear();
	playerOnlyTriggers.clear();
	monsterOnlyTriggers.clear();
	entsNegativePitchPrefix.clear();
	transparentTextures.clear();
	transparentEntities.clear();

	defaultIsEmpty = true;

	entListReload = true;
	stripWad = false;

	palette_name = "quake_1";

	unsigned char default_data[0x300] = {
	 0x00, 0x00, 0x00, 0x0F, 0x0F, 0x0F, 0x1F, 0x1F, 0x1F, 0x2F, 0x2F, 0x2F, 0x3F, 0x3F, 0x3F, 0x4B,
	 0x4B, 0x4B, 0x5B, 0x5B, 0x5B, 0x6B, 0x6B, 0x6B, 0x7B, 0x7B, 0x7B, 0x8B, 0x8B, 0x8B, 0x9B, 0x9B,
	 0x9B, 0xAB, 0xAB, 0xAB, 0xBB, 0xBB, 0xBB, 0xCB, 0xCB, 0xCB, 0xDB, 0xDB, 0xDB, 0xEB, 0xEB, 0xEB,
	 0x0F, 0x0B, 0x07, 0x17, 0x0F, 0x0B, 0x1F, 0x17, 0x0B, 0x27, 0x1B, 0x0F, 0x2F, 0x23, 0x13, 0x37,
	 0x2B, 0x17, 0x3F, 0x2F, 0x17, 0x4B, 0x37, 0x1B, 0x53, 0x3B, 0x1B, 0x5B, 0x43, 0x1F, 0x63, 0x4B,
	 0x1F, 0x6B, 0x53, 0x1F, 0x73, 0x57, 0x1F, 0x7B, 0x5F, 0x23, 0x83, 0x67, 0x23, 0x8F, 0x6F, 0x23,
	 0x0B, 0x0B, 0x0F, 0x13, 0x13, 0x1B, 0x1B, 0x1B, 0x27, 0x27, 0x27, 0x33, 0x2F, 0x2F, 0x3F, 0x37,
	 0x37, 0x4B, 0x3F, 0x3F, 0x57, 0x47, 0x47, 0x67, 0x4F, 0x4F, 0x73, 0x5B, 0x5B, 0x7F, 0x63, 0x63,
	 0x8B, 0x6B, 0x6B, 0x97, 0x73, 0x73, 0xA3, 0x7B, 0x7B, 0xAF, 0x83, 0x83, 0xBB, 0x8B, 0x8B, 0xCB,
	 0x00, 0x00, 0x00, 0x07, 0x07, 0x00, 0x0B, 0x0B, 0x00, 0x13, 0x13, 0x00, 0x1B, 0x1B, 0x00, 0x23,
	 0x23, 0x00, 0x2B, 0x2B, 0x07, 0x2F, 0x2F, 0x07, 0x37, 0x37, 0x07, 0x3F, 0x3F, 0x07, 0x47, 0x47,
	 0x07, 0x4B, 0x4B, 0x0B, 0x53, 0x53, 0x0B, 0x5B, 0x5B, 0x0B, 0x63, 0x63, 0x0B, 0x6B, 0x6B, 0x0F,
	 0x07, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x17, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x27, 0x00, 0x00, 0x2F,
	 0x00, 0x00, 0x37, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x47, 0x00, 0x00, 0x4F, 0x00, 0x00, 0x57, 0x00,
	 0x00, 0x5F, 0x00, 0x00, 0x67, 0x00, 0x00, 0x6F, 0x00, 0x00, 0x77, 0x00, 0x00, 0x7F, 0x00, 0x00,
	 0x13, 0x13, 0x00, 0x1B, 0x1B, 0x00, 0x23, 0x23, 0x00, 0x2F, 0x2B, 0x00, 0x37, 0x2F, 0x00, 0x43,
	 0x37, 0x00, 0x4B, 0x3B, 0x07, 0x57, 0x43, 0x07, 0x5F, 0x47, 0x07, 0x6B, 0x4B, 0x0B, 0x77, 0x53,
	 0x0F, 0x83, 0x57, 0x13, 0x8B, 0x5B, 0x13, 0x97, 0x5F, 0x1B, 0xA3, 0x63, 0x1F, 0xAF, 0x67, 0x23,
	 0x23, 0x13, 0x07, 0x2F, 0x17, 0x0B, 0x3B, 0x1F, 0x0F, 0x4B, 0x23, 0x13, 0x57, 0x2B, 0x17, 0x63,
	 0x2F, 0x1F, 0x73, 0x37, 0x23, 0x7F, 0x3B, 0x2B, 0x8F, 0x43, 0x33, 0x9F, 0x4F, 0x33, 0xAF, 0x63,
	 0x2F, 0xBF, 0x77, 0x2F, 0xCF, 0x8F, 0x2B, 0xDF, 0xAB, 0x27, 0xEF, 0xCB, 0x1F, 0xFF, 0xF3, 0x1B,
	 0x0B, 0x07, 0x00, 0x1B, 0x13, 0x00, 0x2B, 0x23, 0x0F, 0x37, 0x2B, 0x13, 0x47, 0x33, 0x1B, 0x53,
	 0x37, 0x23, 0x63, 0x3F, 0x2B, 0x6F, 0x47, 0x33, 0x7F, 0x53, 0x3F, 0x8B, 0x5F, 0x47, 0x9B, 0x6B,
	 0x53, 0xA7, 0x7B, 0x5F, 0xB7, 0x87, 0x6B, 0xC3, 0x93, 0x7B, 0xD3, 0xA3, 0x8B, 0xE3, 0xB3, 0x97,
	 0xAB, 0x8B, 0xA3, 0x9F, 0x7F, 0x97, 0x93, 0x73, 0x87, 0x8B, 0x67, 0x7B, 0x7F, 0x5B, 0x6F, 0x77,
	 0x53, 0x63, 0x6B, 0x4B, 0x57, 0x5F, 0x3F, 0x4B, 0x57, 0x37, 0x43, 0x4B, 0x2F, 0x37, 0x43, 0x27,
	 0x2F, 0x37, 0x1F, 0x23, 0x2B, 0x17, 0x1B, 0x23, 0x13, 0x13, 0x17, 0x0B, 0x0B, 0x0F, 0x07, 0x07,
	 0xBB, 0x73, 0x9F, 0xAF, 0x6B, 0x8F, 0xA3, 0x5F, 0x83, 0x97, 0x57, 0x77, 0x8B, 0x4F, 0x6B, 0x7F,
	 0x4B, 0x5F, 0x73, 0x43, 0x53, 0x6B, 0x3B, 0x4B, 0x5F, 0x33, 0x3F, 0x53, 0x2B, 0x37, 0x47, 0x23,
	 0x2B, 0x3B, 0x1F, 0x23, 0x2F, 0x17, 0x1B, 0x23, 0x13, 0x13, 0x17, 0x0B, 0x0B, 0x0F, 0x07, 0x07,
	 0xDB, 0xC3, 0xBB, 0xCB, 0xB3, 0xA7, 0xBF, 0xA3, 0x9B, 0xAF, 0x97, 0x8B, 0xA3, 0x87, 0x7B, 0x97,
	 0x7B, 0x6F, 0x87, 0x6F, 0x5F, 0x7B, 0x63, 0x53, 0x6B, 0x57, 0x47, 0x5F, 0x4B, 0x3B, 0x53, 0x3F,
	 0x33, 0x43, 0x33, 0x27, 0x37, 0x2B, 0x1F, 0x27, 0x1F, 0x17, 0x1B, 0x13, 0x0F, 0x0F, 0x0B, 0x07,
	 0x6F, 0x83, 0x7B, 0x67, 0x7B, 0x6F, 0x5F, 0x73, 0x67, 0x57, 0x6B, 0x5F, 0x4F, 0x63, 0x57, 0x47,
	 0x5B, 0x4F, 0x3F, 0x53, 0x47, 0x37, 0x4B, 0x3F, 0x2F, 0x43, 0x37, 0x2B, 0x3B, 0x2F, 0x23, 0x33,
	 0x27, 0x1F, 0x2B, 0x1F, 0x17, 0x23, 0x17, 0x0F, 0x1B, 0x13, 0x0B, 0x13, 0x0B, 0x07, 0x0B, 0x07,
	 0xFF, 0xF3, 0x1B, 0xEF, 0xDF, 0x17, 0xDB, 0xCB, 0x13, 0xCB, 0xB7, 0x0F, 0xBB, 0xA7, 0x0F, 0xAB,
	 0x97, 0x0B, 0x9B, 0x83, 0x07, 0x8B, 0x73, 0x07, 0x7B, 0x63, 0x07, 0x6B, 0x53, 0x00, 0x5B, 0x47,
	 0x00, 0x4B, 0x37, 0x00, 0x3B, 0x2B, 0x00, 0x2B, 0x1F, 0x00, 0x1B, 0x0F, 0x00, 0x0B, 0x07, 0x00,
	 0x00, 0x00, 0xFF, 0x0B, 0x0B, 0xEF, 0x13, 0x13, 0xDF, 0x1B, 0x1B, 0xCF, 0x23, 0x23, 0xBF, 0x2B,
	 0x2B, 0xAF, 0x2F, 0x2F, 0x9F, 0x2F, 0x2F, 0x8F, 0x2F, 0x2F, 0x7F, 0x2F, 0x2F, 0x6F, 0x2F, 0x2F,
	 0x5F, 0x2B, 0x2B, 0x4F, 0x23, 0x23, 0x3F, 0x1B, 0x1B, 0x2F, 0x13, 0x13, 0x1F, 0x0B, 0x0B, 0x0F,
	 0x2B, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x4B, 0x07, 0x00, 0x5F, 0x07, 0x00, 0x6F, 0x0F, 0x00, 0x7F,
	 0x17, 0x07, 0x93, 0x1F, 0x07, 0xA3, 0x27, 0x0B, 0xB7, 0x33, 0x0F, 0xC3, 0x4B, 0x1B, 0xCF, 0x63,
	 0x2B, 0xDB, 0x7F, 0x3B, 0xE3, 0x97, 0x4F, 0xE7, 0xAB, 0x5F, 0xEF, 0xBF, 0x77, 0xF7, 0xD3, 0x8B,
	 0xA7, 0x7B, 0x3B, 0xB7, 0x9B, 0x37, 0xC7, 0xC3, 0x37, 0xE7, 0xE3, 0x57, 0x7F, 0xBF, 0xFF, 0xAB,
	 0xE7, 0xFF, 0xD7, 0xFF, 0xFF, 0x67, 0x00, 0x00, 0x8B, 0x00, 0x00, 0xB3, 0x00, 0x00, 0xD7, 0x00,
	 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xF3, 0x93, 0xFF, 0xF7, 0xC7, 0xFF, 0xFF, 0xFF, 0x9F, 0x5B, 0x53,
	};

	pal_id = -1;
	memcpy(palette_default, default_data, 0x300);

	ResetBspLimits();
}

void AppSettings::reset()
{
	loadDefault();

	fgdPaths.clear();
	fgdPaths.push_back({ "/moddir/GameDefinitionFile.fgd",true });

	resPaths.clear();
	resPaths.push_back({ "/moddir/",true });
	resPaths.push_back({ "/moddir_addon/",true });

	conditionalPointEntTriggers.clear();
	conditionalPointEntTriggers.push_back("trigger_once");
	conditionalPointEntTriggers.push_back("trigger_multiple");
	conditionalPointEntTriggers.push_back("trigger_counter");
	conditionalPointEntTriggers.push_back("trigger_gravity");
	conditionalPointEntTriggers.push_back("trigger_teleport");

	entsThatNeverNeedAnyHulls.clear();
	entsThatNeverNeedAnyHulls.push_back("env_bubbles");
	entsThatNeverNeedAnyHulls.push_back("func_tankcontrols");
	entsThatNeverNeedAnyHulls.push_back("func_traincontrols");
	entsThatNeverNeedAnyHulls.push_back("func_vehiclecontrols");
	entsThatNeverNeedAnyHulls.push_back("trigger_autosave"); // obsolete in sven
	entsThatNeverNeedAnyHulls.push_back("trigger_endsection"); // obsolete in sven

	entsThatNeverNeedCollision.clear();
	entsThatNeverNeedCollision.push_back("func_illusionary");
	entsThatNeverNeedCollision.push_back("func_mortar_field");

	passableEnts.clear();
	passableEnts.push_back("func_door");
	passableEnts.push_back("func_door_rotating");
	passableEnts.push_back("func_pendulum");
	passableEnts.push_back("func_tracktrain");
	passableEnts.push_back("func_train");
	passableEnts.push_back("func_water");
	passableEnts.push_back("momentary_door");

	playerOnlyTriggers.clear();
	playerOnlyTriggers.push_back("func_ladder");
	playerOnlyTriggers.push_back("game_zone_player");
	playerOnlyTriggers.push_back("player_respawn_zone");
	playerOnlyTriggers.push_back("trigger_cdaudio");
	playerOnlyTriggers.push_back("trigger_changelevel");
	playerOnlyTriggers.push_back("trigger_transition");

	monsterOnlyTriggers.clear();
	monsterOnlyTriggers.push_back("func_monsterclip");
	monsterOnlyTriggers.push_back("trigger_monsterjump");

	entsNegativePitchPrefix.clear();

	entsNegativePitchPrefix.push_back("ammo_");
	entsNegativePitchPrefix.push_back("env_sprite");
	entsNegativePitchPrefix.push_back("cycler");
	entsNegativePitchPrefix.push_back("item_");
	entsNegativePitchPrefix.push_back("monster_");
	entsNegativePitchPrefix.push_back("weaponbox");
	entsNegativePitchPrefix.push_back("worlditems");
	entsNegativePitchPrefix.push_back("xen_");

	transparentTextures.clear();
	transparentTextures.push_back("AAATRIGGER");

	transparentEntities.clear();
	transparentEntities.push_back("func_buyzone");
}

void AppSettings::fillLanguages(const std::string& folderPath)
{
	languages.clear();
	languages.push_back("EN");
	for (const auto& entry : fs::directory_iterator(folderPath))
	{
		if (!entry.is_directory()) {
			std::string filename = entry.path().filename().string();
			if (filename.starts_with("language_") && filename.ends_with(".ini"))
			{
				std::string language = filename.substr(9);
				language.erase(language.size() - 4);
				language = toUpperCase(language);
				if (std::find(languages.begin(), languages.end(), language) == languages.end())
					languages.push_back(language);
			}
		}
	}
}

void AppSettings::fillPalettes(const std::string& folderPath)
{
	palettes.clear();
	for (const auto& entry : fs::directory_iterator(folderPath))
	{
		if (!entry.is_directory()) {
			std::string filename = entry.path().filename().string();
			if (filename.ends_with(".pal"))
			{
				int len;
				char* data = loadFile(entry.path().string(), len);
				if (data)
				{
					if (len > 256 * sizeof(COLOR3))
					{
						print_log(PRINT_RED, "Bad palette \"{}\" size : {} bytes!", entry.path().string(), len);
					}
					else
					{
						filename.pop_back(); filename.pop_back(); filename.pop_back(); filename.pop_back();
						palettes.push_back({ toUpperCase(filename), (unsigned int)(len / sizeof(COLOR3)), NULL });
						memcpy(palettes[palettes.size() - 1].data, data, len);
						delete[] data;
					}
				}
			}
		}
	}
}

void AppSettings::load()
{
	set_localize_lang("EN");

	std::ifstream file(g_settings_path);
	if (!file.is_open() || fileSize(g_settings_path) == 0)
	{
		file.close();

		bool settings_deleted = true;

		if (fileExists(g_settings_path))
		{
			settings_deleted = removeFile(g_settings_path);
		}

		if (settings_deleted)
		{
			if (fileExists(g_settings_path + ".bak"))
			{
				copyFile(g_settings_path + ".bak", g_settings_path);
				file = std::ifstream(g_settings_path);
			}

			if (!file.is_open() || fileSize(g_settings_path) == 0)
			{
				print_log(PRINT_RED, get_localized_string(LANG_0926), g_settings_path);
				reset();
				return;
			}
		}
		else
		{
			if (fileExists(g_settings_path + ".bak"))
			{
				file = std::ifstream(g_settings_path + ".bak");
			}


			if (!file.is_open() || fileSize(g_settings_path + ".bak") == 0)
			{
				print_log(PRINT_RED, get_localized_string(LANG_0926), g_settings_path);
				reset();
				return;
			}


			print_log(PRINT_GREEN | PRINT_RED, "Warning! Settings restored from {} file!\n", g_settings_path + ".bak");
		}
	}



	fillLanguages("./languages/");

	fillPalettes("./palettes/");

	palette_name = "quake_1";

	int lines_readed = 0;
	std::string line;
	while (getline(file, line))
	{
		if (line.empty())
			continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		lines_readed++;

		std::string key = trimSpaces(line.substr(0, eq));
		std::string val = trimSpaces(line.substr(eq + 1));

		if (key == "window_width")
		{
			g_settings.windowWidth = atoi(val.c_str());
		}
		else if (key == "window_height")
		{
			g_settings.windowHeight = atoi(val.c_str());
		}
		else if (key == "window_x")
		{
			g_settings.windowX = atoi(val.c_str());
		}
		else if (key == "window_y")
		{
			g_settings.windowY = atoi(val.c_str());
		}
		else if (key == "window_maximized")
		{
			g_settings.maximized = atoi(val.c_str());
		}
		else if (key == "save_windows")
		{
			g_settings.save_windows = atoi(val.c_str()) != 0;
		}
		else if (key == "debug_open")
		{
			g_settings.debug_open = atoi(val.c_str()) != 0 && save_windows;
		}
		else if (key == "keyvalue_open")
		{
			g_settings.keyvalue_open = atoi(val.c_str()) != 0 && save_windows;
		}
		else if (key == "transform_open")
		{
			g_settings.transform_open = atoi(val.c_str()) != 0 && save_windows;
		}
		else if (key == "log_open")
		{
			g_settings.log_open = atoi(val.c_str()) != 0 && save_windows;
		}
		else if (key == "limits_open")
		{
			g_settings.limits_open = atoi(val.c_str()) != 0 && save_windows;
		}
		else if (key == "entreport_open")
		{
			g_settings.entreport_open = atoi(val.c_str()) != 0 && save_windows;
		}
		else if (key == "texbrowser_open")
		{
			g_settings.texbrowser_open = atoi(val.c_str()) != 0 && save_windows;
		}
		else if (key == "goto_open")
		{
			g_settings.goto_open = atoi(val.c_str()) != 0 && save_windows;
		}
		else if (key == "settings_tab")
		{
			if (save_windows)
				g_settings.settings_tab = atoi(val.c_str());
		}
		else if (key == "vsync")
		{
			g_settings.vsync = atoi(val.c_str()) != 0;
		}
		else if (key == "mark_unused_texinfos")
		{
			g_settings.mark_unused_texinfos = atoi(val.c_str()) != 0;
		}
		else if (key == "merge_verts")
		{
			g_settings.merge_verts = atoi(val.c_str()) != 0;
		}
		else if (key == "merge_edges")
		{
			g_settings.merge_edges = atoi(val.c_str()) != 0;
		}
		else if (key == "start_at_entity")
		{
			g_settings.start_at_entity = atoi(val.c_str()) != 0;
		}
		else if (key == "verbose_logs")
		{
			g_settings.verboseLogs = atoi(val.c_str()) != 0;
#ifndef NDEBUG
			g_settings.verboseLogs = true;
#endif
		}
		else if (key == "fov")
		{
			g_settings.fov = (float)atof(val.c_str());
		}
		else if (key == "zfar")
		{
			g_settings.zfar = (float)atof(val.c_str());
		}
		else if (key == "move_speed")
		{
			g_settings.moveSpeed = (float)atof(val.c_str());
			if (g_settings.moveSpeed < 100)
			{
				print_log(get_localized_string(LANG_0927));
				g_settings.moveSpeed = 500;
			}
		}
		else if (key == "rot_speed")
		{
			g_settings.rotSpeed = (float)atof(val.c_str());
		}
		else if (key == "renders_flags")
		{
			g_settings.render_flags = atoi(val.c_str());
		}
		else if (key == "font_size")
		{
			g_settings.fontSize = (float)atof(val.c_str());
		}
		else if (key == "undo_levels")
		{
			g_settings.undoLevels = atoi(val.c_str());
		}
		else if (key == "fpslimit")
		{
			g_settings.fpslimit = atoi(val.c_str());
			if (g_settings.fpslimit < 30)
				g_settings.fpslimit = 30;
			if (g_settings.fpslimit > 1000)
				g_settings.fpslimit = 1000;
		}
		else if (key == "gamedir")
		{
			g_settings.gamedir = val;
		}
		else if (key == "workingdir")
		{
			g_settings.workingdir = val;
		}
		else if (key == "lastdir")
		{
			g_settings.lastdir = val;
		}
		else if (key == "language")
		{
			g_settings.selected_lang = val;
			set_localize_lang(g_settings.selected_lang);
		}
		else if (key == "hlrad_path")
		{
			g_settings.rad_path = val;
		}
		else if (key == "hlrad_options")
		{
			g_settings.rad_options = val;
		}
		else if (key == "palette")
		{
			g_settings.palette_name = val;
			pal_id = -1;
			for (size_t i = 0; i < palettes.size(); i++)
			{
				if (toLowerCase(palettes[i].name) == toLowerCase(g_settings.palette_name))
				{
					pal_id = (int)i;
				}
			}
		}
		else if (key == "fgd")
		{
			if (val.find('?') == std::string::npos)
				fgdPaths.push_back({ val,true });
			else
			{
				auto vals = splitString(val, "?");
				if (vals.size() == 2)
				{
					fgdPaths.push_back({ vals[1],vals[0] == "enabled" });
				}
			}
		}
		else if (key == "res")
		{
			if (val.find('?') == std::string::npos)
				resPaths.push_back({ val,true });
			else
			{
				auto vals = splitString(val, "?");
				if (vals.size() == 2)
				{
					resPaths.push_back({ vals[1],vals[0] == "enabled" });
				}
			}
		}
		else if (key == "savebackup")
		{
			g_settings.backUpMap = atoi(val.c_str()) != 0;
		}
		else if (key == "save_crc")
		{
			g_settings.preserveCrc32 = atoi(val.c_str()) != 0;
		}
		else if (key == "save_cam")
		{
			g_settings.save_cam = atoi(val.c_str()) != 0;
		}
		else if (key == "auto_import_ent")
		{
			g_settings.autoImportEnt = atoi(val.c_str()) != 0;
		}
		else if (key == "same_dir_for_ent")
		{
			g_settings.sameDirForEnt = atoi(val.c_str()) != 0;
		}
		else if (key == "reload_ents_list")
		{
			entListReload = atoi(val.c_str()) != 0;
		}
		else if (key == "strip_wad_path")
		{
			stripWad = atoi(val.c_str()) != 0;
		}
		else if (key == "default_is_empty")
		{
			defaultIsEmpty = atoi(val.c_str()) != 0;
		}
		else if (key == "FLT_MAX_COORD")
		{
			FLT_MAX_COORD = (float)atof(val.c_str());
		}
		else if (key == "MAX_MAP_MODELS")
		{
			MAX_MAP_MODELS = atoi(val.c_str());
		}
		else if (key == "MAX_MAP_NODES")
		{
			MAX_MAP_NODES = atoi(val.c_str());
		}
		else if (key == "MAX_MAP_CLIPNODES")
		{
			MAX_MAP_CLIPNODES = atoi(val.c_str());
		}
		else if (key == "MAX_MAP_LEAVES")
		{
			MAX_MAP_LEAVES = atoi(val.c_str());
		}
		else if (key == "MAX_MAP_VISDATA")
		{
			MAX_MAP_VISDATA = atoi(val.c_str()) * (1024 * 1024);
		}
		else if (key == "MAX_MAP_ENTS")
		{
			MAX_MAP_ENTS = atoi(val.c_str());
		}
		else if (key == "MAX_MAP_SURFEDGES")
		{
			MAX_MAP_SURFEDGES = atoi(val.c_str());
		}
		else if (key == "MAX_MAP_EDGES")
		{
			MAX_MAP_EDGES = atoi(val.c_str());
		}
		else if (key == "MAX_MAP_TEXTURES")
		{
			MAX_MAP_TEXTURES = atoi(val.c_str());
		}
		else if (key == "MAX_MAP_LIGHTDATA")
		{
			MAX_MAP_LIGHTDATA = atoi(val.c_str()) * (1024 * 1024);
		}
		else if (key == "MAX_TEXTURE_DIMENSION")
		{
			MAX_TEXTURE_DIMENSION = atoi(val.c_str());
			MAX_TEXTURE_SIZE = ((MAX_TEXTURE_DIMENSION * MAX_TEXTURE_DIMENSION * 2 * 3) / 2);
		}
		else if (key == "TEXTURE_STEP")
		{
			TEXTURE_STEP = atoi(val.c_str());
		}
		else if (key == "optimizer_cond_ents")
		{
			conditionalPointEntTriggers.push_back(val);
		}
		else if (key == "optimizer_no_hulls_ents")
		{
			entsThatNeverNeedAnyHulls.push_back(val);
		}
		else if (key == "optimizer_no_collision_ents")
		{
			entsThatNeverNeedCollision.push_back(val);
		}
		else if (key == "optimizer_passable_ents")
		{
			passableEnts.push_back(val);
		}
		else if (key == "optimizer_player_hull_ents")
		{
			playerOnlyTriggers.push_back(val);
		}
		else if (key == "optimizer_monster_hull_ents")
		{
			monsterOnlyTriggers.push_back(val);
		}
		else if (key == "negative_pitch_ents")
		{
			entsNegativePitchPrefix.push_back(val);
		}
		else if (key == "transparent_textures")
		{
			transparentTextures.push_back(val);
		}
		else if (key == "transparent_entities")
		{
			transparentEntities.push_back(val);
		}
	}

	if (g_settings.windowY == -32000 &&
		g_settings.windowX == -32000)
	{
		g_settings.windowY = 0;
		g_settings.windowX = 0;
	}

#ifdef WIN32
	// Fix invisible window header for primary screen.
	if (g_settings.windowY >= 0 && g_settings.windowY < 30)
	{
		g_settings.windowY = 30;
	}
#endif

	// Restore default window height if invalid.
	if (windowHeight <= 0 || windowWidth <= 0)
	{
		windowHeight = 600;
		windowWidth = 800;
	}

	if (lines_readed > 0)
	{
		g_settings.settingLoaded = true;

		removeFile(g_settings_path + ".bak");
		copyFile(g_settings_path, g_settings_path + ".bak");
	}
	else
	{
		print_log(get_localized_string(LANG_0928), g_settings_path);
	}

	if (defaultIsEmpty && fgdPaths.empty())
	{
		fgdPaths.push_back({ "/moddir/GameDefinitionFile.fgd",true });
	}

	if (defaultIsEmpty && resPaths.empty())
	{
		resPaths.push_back({ "/moddir/",true });
		resPaths.push_back({ "/moddir_addon/",true });
	}

	if (entListReload || defaultIsEmpty)
	{
		if ((defaultIsEmpty && conditionalPointEntTriggers.empty()) || entListReload)
		{
			conditionalPointEntTriggers.clear();
			conditionalPointEntTriggers.push_back("trigger_once");
			conditionalPointEntTriggers.push_back("trigger_multiple");
			conditionalPointEntTriggers.push_back("trigger_counter");
			conditionalPointEntTriggers.push_back("trigger_gravity");
			conditionalPointEntTriggers.push_back("trigger_teleport");
		}
		if ((defaultIsEmpty && entsThatNeverNeedAnyHulls.empty()) || entListReload)
		{
			entsThatNeverNeedAnyHulls.clear();
			entsThatNeverNeedAnyHulls.push_back("env_bubbles");
			entsThatNeverNeedAnyHulls.push_back("func_tankcontrols");
			entsThatNeverNeedAnyHulls.push_back("func_traincontrols");
			entsThatNeverNeedAnyHulls.push_back("func_vehiclecontrols");
			entsThatNeverNeedAnyHulls.push_back("trigger_autosave"); // obsolete in sven
			entsThatNeverNeedAnyHulls.push_back("trigger_endsection"); // obsolete in sven
		}
		if ((defaultIsEmpty && entsThatNeverNeedCollision.empty()) || entListReload)
		{
			entsThatNeverNeedCollision.clear();
			entsThatNeverNeedCollision.push_back("func_illusionary");
			entsThatNeverNeedCollision.push_back("func_mortar_field");
		}
		if ((defaultIsEmpty && passableEnts.empty()) || entListReload)
		{
			passableEnts.clear();
			passableEnts.push_back("func_door");
			passableEnts.push_back("func_door_rotating");
			passableEnts.push_back("func_pendulum");
			passableEnts.push_back("func_tracktrain");
			passableEnts.push_back("func_train");
			passableEnts.push_back("func_water");
			passableEnts.push_back("momentary_door");
		}
		if ((defaultIsEmpty && playerOnlyTriggers.empty()) || entListReload)
		{
			playerOnlyTriggers.clear();
			playerOnlyTriggers.push_back("func_ladder");
			playerOnlyTriggers.push_back("game_zone_player");
			playerOnlyTriggers.push_back("player_respawn_zone");
			playerOnlyTriggers.push_back("trigger_cdaudio");
			playerOnlyTriggers.push_back("trigger_changelevel");
			playerOnlyTriggers.push_back("trigger_transition");
		}
		if ((defaultIsEmpty && monsterOnlyTriggers.empty()) || entListReload)
		{
			monsterOnlyTriggers.clear();
			monsterOnlyTriggers.push_back("func_monsterclip");
			monsterOnlyTriggers.push_back("trigger_monsterjump");
		}
		if ((defaultIsEmpty && entsNegativePitchPrefix.empty()) || entListReload)
		{
			entsNegativePitchPrefix.clear();
			entsNegativePitchPrefix.push_back("ammo_");
			entsNegativePitchPrefix.push_back("cycler");
			entsNegativePitchPrefix.push_back("item_");
			entsNegativePitchPrefix.push_back("monster_");
			entsNegativePitchPrefix.push_back("weaponbox");
			entsNegativePitchPrefix.push_back("worlditems");
			entsNegativePitchPrefix.push_back("xen_");
		}
	}

	if (defaultIsEmpty && transparentTextures.empty())
	{
		transparentTextures.push_back("AAATRIGGER");
	}

	if (defaultIsEmpty && transparentEntities.empty())
	{
		transparentEntities.push_back("func_buyzone");
	}


	FixupAllSystemPaths();

	entListReload = false;
}

void AppSettings::save(std::string path)
{
	std::ostringstream file = {};

	file << "window_width=" << g_settings.windowWidth << std::endl;
	file << "window_height=" << g_settings.windowHeight << std::endl;
	file << "window_x=" << g_settings.windowX << std::endl;
	file << "window_y=" << g_settings.windowY << std::endl;
	file << "window_maximized=" << g_settings.maximized << std::endl;

	file << "save_windows=" << g_settings.save_windows << std::endl;
	file << "debug_open=" << g_settings.debug_open << std::endl;
	file << "keyvalue_open=" << g_settings.keyvalue_open << std::endl;
	file << "transform_open=" << g_settings.transform_open << std::endl;
	file << "log_open=" << g_settings.log_open << std::endl;
	file << "limits_open=" << g_settings.limits_open << std::endl;
	file << "entreport_open=" << g_settings.entreport_open << std::endl;
	file << "texbrowser_open=" << g_settings.texbrowser_open << std::endl;
	file << "goto_open=" << g_settings.goto_open << std::endl;

	file << "settings_tab=" << g_settings.settings_tab << std::endl;

	file << "gamedir=" << g_settings.gamedir << std::endl;
	file << "workingdir=" << g_settings.workingdir << std::endl;
	file << "lastdir=" << g_settings.lastdir << std::endl;
	file << "language=" << g_settings.selected_lang << std::endl;
	file << "palette=" << g_settings.palette_name << std::endl;

	file << "hlrad_path=" << g_settings.rad_path << std::endl;
	file << "hlrad_options=" << g_settings.rad_options << std::endl;

	for (size_t i = 0; i < fgdPaths.size(); i++)
	{
		file << "fgd=" << (g_settings.fgdPaths[i].enabled ? "enabled" : "disabled") << "?" << g_settings.fgdPaths[i].path << std::endl;
	}

	for (size_t i = 0; i < resPaths.size(); i++)
	{
		file << "res=" << (g_settings.resPaths[i].enabled ? "enabled" : "disabled") << "?" << g_settings.resPaths[i].path << std::endl;
	}

	for (size_t i = 0; i < conditionalPointEntTriggers.size(); i++)
	{
		file << "optimizer_cond_ents=" << conditionalPointEntTriggers[i] << std::endl;
	}

	for (size_t i = 0; i < entsThatNeverNeedAnyHulls.size(); i++)
	{
		file << "optimizer_no_hulls_ents=" << entsThatNeverNeedAnyHulls[i] << std::endl;
	}

	for (size_t i = 0; i < entsThatNeverNeedCollision.size(); i++)
	{
		file << "optimizer_no_collision_ents=" << entsThatNeverNeedCollision[i] << std::endl;
	}

	for (size_t i = 0; i < passableEnts.size(); i++)
	{
		file << "optimizer_passable_ents=" << passableEnts[i] << std::endl;
	}

	for (size_t i = 0; i < playerOnlyTriggers.size(); i++)
	{
		file << "optimizer_player_hull_ents=" << playerOnlyTriggers[i] << std::endl;
	}

	for (size_t i = 0; i < monsterOnlyTriggers.size(); i++)
	{
		file << "optimizer_monster_hull_ents=" << monsterOnlyTriggers[i] << std::endl;
	}

	for (size_t i = 0; i < entsNegativePitchPrefix.size(); i++)
	{
		file << "negative_pitch_ents=" << entsNegativePitchPrefix[i] << std::endl;
	}

	for (size_t i = 0; i < transparentTextures.size(); i++)
	{
		file << "transparent_textures=" << transparentTextures[i] << std::endl;
	}

	for (size_t i = 0; i < transparentEntities.size(); i++)
	{
		file << "transparent_entities=" << transparentEntities[i] << std::endl;
	}

	file << "vsync=" << g_settings.vsync << std::endl;
	file << "mark_unused_texinfos=" << g_settings.mark_unused_texinfos << std::endl;
	file << "merge_verts=" << g_settings.merge_verts << std::endl;
	file << "merge_edges=" << g_settings.merge_edges << std::endl;
	file << "start_at_entity=" << g_settings.start_at_entity << std::endl;
#ifdef NDEBUG
	file << "verbose_logs=" << g_settings.verboseLogs << std::endl;
#endif
	file << "fov=" << g_settings.fov << std::endl;
	file << "zfar=" << g_settings.zfar << std::endl;
	file << "move_speed=" << g_settings.moveSpeed << std::endl;
	file << "rot_speed=" << g_settings.rotSpeed << std::endl;
	file << "renders_flags=" << g_settings.render_flags << std::endl;
	file << "font_size=" << g_settings.fontSize << std::endl;
	file << "undo_levels=" << g_settings.undoLevels << std::endl;
	file << "fpslimit=" << g_settings.fpslimit << std::endl;
	file << "savebackup=" << g_settings.backUpMap << std::endl;
	file << "save_crc=" << g_settings.preserveCrc32 << std::endl;
	file << "save_cam=" << g_settings.save_cam << std::endl;
	file << "auto_import_ent=" << g_settings.autoImportEnt << std::endl;
	file << "same_dir_for_ent=" << g_settings.sameDirForEnt << std::endl;
	file << "reload_ents_list=" << g_settings.entListReload << std::endl;
	file << "strip_wad_path=" << g_settings.stripWad << std::endl;
	file << "default_is_empty=" << g_settings.defaultIsEmpty << std::endl;

	file << "FLT_MAX_COORD=" << FLT_MAX_COORD << std::endl;
	file << "MAX_MAP_MODELS=" << MAX_MAP_MODELS << std::endl;
	file << "MAX_MAP_NODES=" << MAX_MAP_NODES << std::endl;
	file << "MAX_MAP_CLIPNODES=" << MAX_MAP_CLIPNODES << std::endl;
	file << "MAX_MAP_LEAVES=" << MAX_MAP_LEAVES << std::endl;
	file << "MAX_MAP_VISDATA=" << MAX_MAP_VISDATA / (1024 * 1024) << std::endl;
	file << "MAX_MAP_ENTS=" << MAX_MAP_ENTS << std::endl;
	file << "MAX_MAP_SURFEDGES=" << MAX_MAP_SURFEDGES << std::endl;
	file << "MAX_MAP_EDGES=" << MAX_MAP_EDGES << std::endl;
	file << "MAX_MAP_TEXTURES=" << MAX_MAP_TEXTURES << std::endl;
	file << "MAX_MAP_LIGHTDATA=" << MAX_MAP_LIGHTDATA / (1024 * 1024) << std::endl;
	file << "MAX_TEXTURE_DIMENSION=" << MAX_TEXTURE_DIMENSION << std::endl;
	file << "TEXTURE_STEP=" << TEXTURE_STEP << std::endl;

	file.flush();

	writeFile(g_settings_path, file.str());
}

void AppSettings::save()
{
	FixupAllSystemPaths();
	g_app->saveSettings();
	save(g_settings_path);
}
