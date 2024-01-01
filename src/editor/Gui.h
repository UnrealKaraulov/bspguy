#include <GL/glew.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "Entity.h"
#include "remap.h"
#include "bsptypes.h"
#include "Texture.h"
#include "qtools/rad.h"
#include <GLFW/glfw3.h>

struct ModelInfo
{
	std::string classname;
	std::string targetname;
	std::string model;
	std::string val;
	std::string usage;
	int entIdx;
};

struct StatInfo
{
	std::string name;
	std::string val;
	std::string max;
	std::string fullness;
	float progress;
	ImVec4 color;
};

class Renderer;

#define SHOW_IMPORT_OPEN 1
#define SHOW_IMPORT_ADD_NEW 2
#define SHOW_IMPORT_MODEL_ENTITY 3
#define SHOW_IMPORT_MODEL_BSP 4

extern bool filterNeeded;

class Gui
{
	friend class Renderer;

public:
	Renderer* app;

	bool settingLoaded = false;

	Gui(Renderer* app);

	void init();
	void draw();

	// -1 for empty selection
	void openContextMenu(int entIdx);
	void copyTexture();
	void pasteTexture();
	void copyLightmap();
	void pasteLightmap();
	void refresh();

private:
	ImGuiIO* imgui_io = nullptr;

	bool showDebugWidget = false;
	bool showKeyvalueWidget = false;
	bool showTransformWidget = false;
	bool showLogWidget = false;
	bool showSettingsWidget = false;
	bool showHelpWidget = false;
	bool showAboutWidget = false;
	int showImportMapWidget_Type = 0;
	bool showImportMapWidget = false;
	bool showMergeMapWidget = false;
	bool showLimitsWidget = true;
	bool showFaceEditWidget = false;
	bool showLightmapEditorWidget = false;
	bool showLightmapEditorUpdate = true;
	bool showEntityReport = false;
	bool showGOTOWidget_update = true;
	bool showGOTOWidget = false;
	bool showTextureBrowser = false;
	bool reloadSettings = true;
	int settingsTab = 0;
	bool openSavedTabs = false;

	ImFont* defaultFont;
	ImFont* smallFont;
	ImFont* largeFont;
	ImFont* consoleFont;
	ImFont* consoleFontLarge;
	float fontSize = 22.f;
	bool shouldReloadFonts = false;
	bool shouldReloadTextureInfo = false;

	Texture* objectIconTexture;
	Texture* faceIconTexture;
	Texture* leafIconTexture;

	bool badSurfaceExtents = false;
	bool lightmapTooLarge = false;

	bool loadedLimit[SORT_MODES] = {false};
	std::vector<ModelInfo> limitModels[SORT_MODES];
	bool loadedStats = false;
	std::vector<StatInfo> stats;

	bool anyHullValid[MAX_MAP_HULLS] = {false};

	int guiHoverAxis; // axis being hovered in the transform menu
	int contextMenuEnt = -1; // open entity context menu if >= 0
	int emptyContextMenu = 0; // open context menu for rightclicking world/void

	int copiedMiptex = -1;
	LIGHTMAP copiedLightmap = LIGHTMAP();
	bool pasteTextureNow = false;

	void drawBspContexMenu();
	void drawMenuBar();
	void drawToolbar();
	void FaceSelectPressed();
	void drawFpsOverlay();
	void drawStatusMessage();
	void drawDebugWidget();
	void drawTextureBrowser();
	void drawKeyvalueEditor();
	void drawKeyvalueEditor_SmartEditTab(int entIdx);
	void drawKeyvalueEditor_FlagsTab(int entIdx);
	void drawKeyvalueEditor_RawEditTab(int entIdx);
	void drawGOTOWidget();
	void drawMDLWidget();
	void drawTransformWidget();
	void drawLog();
	void drawSettings();
	void drawHelp();
	void drawAbout();
	void drawImportMapWidget();
	void drawMergeWindow();
	void drawLimits();
	void drawLightMapTool();
	void drawFaceEditorWidget();
	void drawLimitTab(Bsp* map, int sortMode);
	void drawEntityReport();
	StatInfo calcStat(std::string name, unsigned int val, unsigned int max, bool isMem);
	ModelInfo calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, unsigned int val, unsigned int max, bool isMem);
	void checkValidHulls();
	void reloadLimits();
	void ExportOneBigLightmap(Bsp* map);
	void loadFonts();
	void checkFaceErrors();
};