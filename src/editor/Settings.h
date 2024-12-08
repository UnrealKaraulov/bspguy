#pragma once

#include "bsptypes.h"
#include <string>
#include "ini.h"

extern inih::INIReader* settings_ini;

extern std::string g_settings_path;
extern std::string g_game_dir;
extern std::string g_working_dir;
extern std::string g_startup_dir;

extern int g_render_flags;

class Renderer;
extern Renderer* g_app;

enum RenderFlags : unsigned int
{
	RENDER_TEXTURES = 1 << 0,
	RENDER_LIGHTMAPS = 1 << 1,
	RENDER_WIREFRAME = 1 << 2,
	RENDER_ENTS = 1 << 3,
	RENDER_SPECIAL = 1 << 4,
	RENDER_SPECIAL_ENTS = 1 << 5,
	RENDER_POINT_ENTS = 1 << 6,
	RENDER_ORIGIN = 1 << 7,
	RENDER_WORLD_CLIPNODES = 1 << 8,
	RENDER_ENT_CLIPNODES = 1 << 9,
	RENDER_ENT_CONNECTIONS = 1 << 10,
	RENDER_TRANSPARENT = 1 << 11,
	RENDER_MODELS = 1 << 12,
	RENDER_MODELS_ANIMATED = 1 << 13,
	RENDER_SELECTED_AT_TOP = 1 << 14,
	RENDER_TEXTURES_NOFILTER = 1 << 15,
	RENDER_MAP_BOUNDARY = 1 << 16
};

struct PathToggleStruct 
{
	std::string path;
	bool enabled;

	PathToggleStruct(std::string filePath, bool isEnable)
		: enabled(isEnable), path(std::move(filePath)) {
	}
};

struct PaletteData
{
	std::string name;
	unsigned int colors;
	unsigned char data[0x300];
};

struct Settings
{
	int windowWidth;
	int windowHeight;
	int windowX;
	int windowY;
	int maximized;
	int undoLevels;
	int fpslimit;
	int settings_tab;
	int render_flags;

	float fov;
	float zfar;
	float moveSpeed;
	float rotSpeed;
	float fontSize;

	std::string gamedir;
	std::string workingdir;
	std::string lastdir;

	std::string engineLimits;

	std::string selected_lang;
	std::vector<std::string> languages;

	std::vector<PaletteData> palettes;
	std::string palette_name;

	unsigned char palette_default[256 * sizeof(COLOR3)];

	int pal_id;

	bool settingLoaded; // Settings loaded
	bool verboseLogs;
	bool save_windows;
	bool debug_open;
	bool keyvalue_open;
	bool transform_open;
	bool log_open;
	bool limits_open;
	bool entreport_open;
	bool texbrowser_open;
	bool goto_open;
	bool vsync;
	bool mark_unused_texinfos;
	bool merge_verts;
	bool merge_edges;
	bool start_at_entity;
	bool savebackup;
	bool save_crc;
	bool save_cam;
	bool auto_import_ent;
	bool same_dir_for_ent;
	bool reload_ents_list;
	bool strip_wad_path;
	bool default_is_empty;

	std::string rad_path;
	std::string rad_options;

	std::vector<PathToggleStruct> fgdPaths;
	std::vector<PathToggleStruct> resPaths;

	std::vector<std::string> conditionalPointEntTriggers;
	std::vector<std::string> entsThatNeverNeedAnyHulls;
	std::vector<std::string> entsThatNeverNeedCollision;
	std::vector<std::string> passableEnts;
	std::vector<std::string> playerOnlyTriggers;
	std::vector<std::string> monsterOnlyTriggers;
	std::vector<std::string> entsNegativePitchPrefix;
	std::vector<std::string> transparentTextures;
	std::vector<std::string> transparentEntities;

	void loadDefaultSettings();
	void loadSettings();
	void saveSettings();
	void saveSettings(std::string path);
	void fillLanguages(const std::string& folderPath);
	void fillPalettes(const std::string& folderPath);
};

extern Settings g_settings;
std::string ConvertFromCFGtoINI(const std::string& cfgData);