#pragma once

#include "bsptypes.h"

extern std::string g_settings_path;
extern std::string g_game_dir;
extern std::string g_working_dir;

extern int g_render_flags;

class Renderer;

extern Renderer* g_app;

enum RenderFlags
{
	RENDER_TEXTURES = 1,
	RENDER_LIGHTMAPS = 2,
	RENDER_WIREFRAME = 4,
	RENDER_ENTS = 8,
	RENDER_SPECIAL = 16,
	RENDER_SPECIAL_ENTS = 32,
	RENDER_POINT_ENTS = 64,
	RENDER_ORIGIN = 128,
	RENDER_WORLD_CLIPNODES = 256,
	RENDER_ENT_CLIPNODES = 512,
	RENDER_ENT_CONNECTIONS = 1024,
	RENDER_TRANSPARENT = 2048,
	RENDER_MODELS = 4096,
	RENDER_MODELS_ANIMATED = 8192
};


struct PathToggleStruct
{
	std::string path;
	bool enabled;

	PathToggleStruct(std::string filePath, bool isEnable)
	{
		path = filePath;
		enabled = isEnable;
	}
};

struct AppSettings
{
	int windowWidth;
	int windowHeight;
	int windowX;
	int windowY;
	int maximized;
	int undoLevels;
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

	std::string selected_lang;

	std::vector<std::string> languages;

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
	bool start_at_entity;
	bool backUpMap;
	bool preserveCrc32;
	bool autoImportEnt;
	bool sameDirForEnt;
	bool entListReload;
	bool stripWad;
	bool defaultIsEmpty;

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

	void loadDefault();
	void load();
	void reset();
	void save();
	void save(std::string path);
	void fillLanguages(const std::string& folderPath);
};

extern AppSettings g_settings;