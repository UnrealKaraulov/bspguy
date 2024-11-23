#include "as.h"

#include "vectors.h"
#include "Entity.h"

#include "log.h"
#include "util.h"
#include "Bsp.h"
#include "Renderer.h"
#include "BspRenderer.h"
#include "PointEntRenderer.h"
#include "Texture.h"
#include "Sprite.h"
#include "mdl_studio.h"
#include "Gui.h"

#include "angelscript.h"
#include "../../add_on/scriptstdstring/scriptstdstring.h"
#include "../../add_on/scriptbuilder/scriptbuilder.h"
#include "../../add_on/scriptarray/scriptarray.h"

void RegisterNatives(asIScriptEngine* engine);
void RegisterStructs(asIScriptEngine* engine);
void RegisterClasses(asIScriptEngine* engine);

enum AS_FUNCS : int
{
	AS_FUNC_GET_NAME,
	AS_FUNC_GET_CATEGORY,
	AS_FUNC_GET_DESCRIPTION,
	AS_FUNC_ON_MAPCHANGE,
	AS_FUNC_ON_MENUCALL,
	AS_FUNC_ON_FRAMETICK,

	AS_FUNC_COUNT
};

const char* funcNames[AS_FUNC_COUNT] =
{
	"string GetScriptName()",
	"string GetScriptDirectory()",
	"string GetScriptDescription()",
	"void OnMapChange()",
	"void OnMenuCall()",
	"void OnFrameTick()"
};

std::vector<std::string> funcNamesStr(std::begin(funcNames), std::end(funcNames));


struct AScript
{
	bool isBad;
	std::string path;
	std::string name;
	std::string modulename;
	std::string category_name;
	std::string description;
	std::string bsp_name;
	asIScriptEngine* engine;
	asIScriptContext* ctx;
	asIScriptModule* module;
	asIScriptFunction* funcs[AS_FUNC_COUNT];

	AScript(std::string file) : path(std::move(file)) 
	{
		isBad = true;
		name = basename(path);
		modulename = stripExt(name);
		description = "";
		memset(funcs, 0, sizeof(funcs));

		modulename.erase(std::remove_if(modulename.begin(), modulename.end(),
			[](unsigned char c) { return !std::isalnum(c); }),
			modulename.end());

		if (!modulename.empty() && std::isdigit(modulename[0])) {
			modulename.insert(modulename.begin(), 'm');
		}

		if (modulename.length() < 2) {
			modulename = "m" + std::to_string(reinterpret_cast<uintptr_t>(this));
		}

        engine = asCreateScriptEngine();
		ctx = nullptr;
		module = nullptr;
		bsp_name = "";
		if (engine) 
		{
			// Register the string type
			RegisterStdString(engine);
			// Register the script array type
			RegisterScriptArray(engine, false);

			// Register other
			RegisterStructs(engine);
			RegisterClasses(engine);
			RegisterNatives(engine);

			// Register the application interface with the script engine
			CScriptBuilder builder;
			int r = builder.StartNewModule(engine, modulename.c_str());
			if (r >= 0) {
				r = builder.AddSectionFromFile(path.c_str());
				if (r >= 0) {
					r = builder.BuildModule();
					if (r >= 0) {
						ctx = engine->CreateContext();
						module = engine->GetModule(modulename.c_str());
						if (module)
						{
							for (int f = 0; f < AS_FUNC_COUNT; f++)
							{
								funcs[f] = module->GetFunctionByDecl(funcNames[f]);
							}
							isBad = false;
						}
					}
					else
					{
						print_log(PRINT_RED, "[BuildModule] Error {} loading path:{}\n", r, path);
					}
				}
				else
				{
					print_log(PRINT_RED, "[AddSectionFromFile] Error {} loading path:{}\n", r, path);
				}
			}
			else
			{
				print_log(PRINT_RED, "[StartNewModule] Error {} loading path:{}\n", r, path);
			}

			if (isBad) 
			{
				if (ctx)
				{
					ctx->Release();
					ctx = nullptr;
				}
				if (engine)
				{
					engine->Release();
					engine = nullptr;
				}
				module = nullptr;
			}
		}
	}

	~AScript()
	{
		if (ctx)
			ctx->Release();
		if (engine)
			engine->ShutDownAndRelease();
	}
};

static std::vector<AScript> scriptList{};

void PrintString(const std::string& str)
{
	print_log("{}", str);
}

void PrintError(const std::string& str)
{
	print_log(PRINT_RED, "{}", str);
}

void PrintColored(int color, const std::string& str)
{
	print_log(color, "{}", str);
}

int Native_GetSelectedMap()
{
	if (g_app->SelectedMap)
	{
		return (int)g_app->SelectedMap->realIdx;
	}
	return -1;
}

int Native_GetSelectedEnt()
{
	if (!g_app->pickInfo.selectedEnts.empty())
	{
		if (g_app->SelectedMap)
		{
			return (int)g_app->SelectedMap->ents[g_app->pickInfo.selectedEnts[0]]->realIdx;
		}
	}
	return -1;
}

std::string Native_GetMapName(int map)
{
	for (auto& rend : mapRenderers)
	{
		if (rend->map && rend->map->realIdx == map)
		{
			return rend->map->bsp_name;
		}
	}
	return "";
}

std::string Native_GetEntClassname(int entIdx)
{
	for (auto& rend : mapRenderers)
	{
		if (rend->map)
		{
			for (auto ent : rend->map->ents)
			{
				if (ent->realIdx == entIdx)
				{
					return ent->classname;
				}
			}
		}
	}
	return "";
}


void RegisterNatives(asIScriptEngine* engine)
{
	int r = engine->RegisterGlobalFunction("void PrintString(const string &in)",
		asFUNCTIONPR(PrintString, (const std::string&), void), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("void PrintError(const string &in)", 
		asFUNCTIONPR(PrintError, (const std::string&), void), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("void PrintColored(int colorid, const string &in)", 
		asFUNCTIONPR(PrintColored, (int, const std::string&), void), asCALL_CDECL);	print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("int GetSelectedMap()",
		asFUNCTIONPR(Native_GetSelectedMap, (void), int), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("int GetSelectedEnt()",
		asFUNCTIONPR(Native_GetSelectedEnt, (void), int), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("string GetMapName(int mapIdx)",
		asFUNCTIONPR(Native_GetMapName, (int), std::string), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("string GetEntClassname(int entIdx)",
		asFUNCTIONPR(Native_GetEntClassname, (int), std::string), asCALL_CDECL); print_assert(r >= 0);
}

void RegisterStructs(asIScriptEngine* engine)
{

}

void RegisterClasses(asIScriptEngine* engine)
{

}

template <typename T>
int ExecuteFunction(AScript& script, AS_FUNCS funcEnum, T& resultVar)
{
	int r = asEXECUTION_FINISHED;
	if (script.funcs[funcEnum])
	{
		r = script.ctx->Prepare(script.funcs[funcEnum]);
		if (r >= 0)
		{
			r = script.ctx->Execute();
			if (r == asEXECUTION_FINISHED) {
				resultVar = *static_cast<T*>(script.ctx->GetAddressOfReturnValue());
				return r;
			}
			else
			{
				PrintError("Error executing \"" + funcNamesStr[funcEnum] + "\" in script: " + script.path + "\n");
			}
		}
	}
	return r;
}

int ExecuteFunctionNoRet(const AScript& script, AS_FUNCS funcEnum)
{
	int r = asEXECUTION_FINISHED;
	if (script.funcs[funcEnum])
	{
		r = script.ctx->Prepare(script.funcs[funcEnum]);
		if (r >= 0)
		{
			r = script.ctx->Execute();
			if (r == asEXECUTION_FINISHED) 
			{
				return r;
			}
			else
			{
				PrintError("Error executing \"" + funcNamesStr[funcEnum] + "\" in script: " + script.path + "\n");
			}
		}
	}
	return r;
}

void InitializeAngelScripts()
{
	std::error_code ec;

	// load all scripts from ./scripts/global directory with extension '.as'
	if (dirExists("./scripts/global"))
	{
		for (const auto& entry : fs::recursive_directory_iterator("./scripts/global", ec)) {
			if (entry.is_regular_file() && entry.path().extension() == ".as") {
				scriptList.emplace_back(entry.path().string());
				if (scriptList.back().isBad)
				{
					scriptList.pop_back();
				}
			}
		}
	}
	if (ec) {
		PrintError("Error loading global scripts: " + ec.message());
	}
	ec = {};

	// load all scripts from ./scripts/mapname direcfory for current map
	auto map = g_app ? g_app->SelectedMap : NULL;
	if (map) {
		std::string mapScriptsPath = "./scripts/" + map->bsp_name;
		if (dirExists(mapScriptsPath))
		{
			for (const auto& entry : fs::recursive_directory_iterator(mapScriptsPath, ec)) {
				if (entry.is_regular_file() && entry.path().extension() == ".as") {
					scriptList.emplace_back(entry.path().string());
					scriptList.back().bsp_name = map->bsp_name;
					if (scriptList.back().isBad)
					{
						scriptList.pop_back();
					}
				}
			}
		}
	}

	for (auto& script : scriptList) 
	{
		ExecuteFunction(script, AS_FUNC_GET_NAME, script.name);
		ExecuteFunction(script, AS_FUNC_GET_CATEGORY, script.category_name);
		ExecuteFunction(script, AS_FUNC_GET_DESCRIPTION, script.description);
	}
}

void AS_OnMapChange()
{
	static Bsp* lastmap = NULL;
	for (const auto& script : scriptList)
	{
		ExecuteFunctionNoRet(script, AS_FUNC_ON_MAPCHANGE);
	}

	if (lastmap != g_app->SelectedMap)
	{
		lastmap = g_app->SelectedMap;

		std::string bspName = "";

		auto map = g_app ? g_app->SelectedMap : NULL;
		if (map)
		{
			bspName = map->bsp_name;
		}

		if (!scriptList.empty())
		{
			for (int i = (int)scriptList.size() - 1; i >= 0; i--)
			{
				auto& script = scriptList[i];
				if (script.bsp_name.size() && bspName != script.bsp_name)
				{
					scriptList.erase(scriptList.begin() + i);
				}
			}
		}
	}
}

void AS_OnGuiTick()
{
	if (ImGui::BeginMenu("Scripts###ScriptsMenu", !scriptList.empty())) 
	{
		std::set<std::string> globalCategories;
		std::set<std::string> mapCategories;

		for (const auto& script : scriptList) {
			if (script.bsp_name.empty()) {
				globalCategories.insert(script.category_name);
			}
			else {
				mapCategories.insert(script.bsp_name);
			}
		}

		if (ImGui::BeginMenu("Global###GlobalMenu", !globalCategories.empty())) {
			for (const auto& category : globalCategories) {
				if (ImGui::BeginMenu((category + "###GlobalCategory_" + category).c_str())) {
					for (const auto& script : scriptList) {
						if (script.bsp_name.empty() && script.category_name == category) {
							if (ImGui::MenuItem((script.name + "###GlobalScript_" + script.name).c_str())) 
							{
								ExecuteFunctionNoRet(script, AS_FUNC_ON_MENUCALL);
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::TextUnformatted(script.description.c_str());
								ImGui::EndTooltip();
							}
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Map Scripts###MapScriptsMenu", !mapCategories.empty())) {
			for (const auto& bspName : mapCategories) {
				if (ImGui::BeginMenu((bspName + "###MapBsp_" + bspName).c_str())) {
					for (const auto& script : scriptList) {
						if (!script.bsp_name.empty() && script.bsp_name == bspName) {
							if (ImGui::MenuItem((script.name + "###MapScript_" + script.name).c_str())) 
							{
								ExecuteFunctionNoRet(script, AS_FUNC_ON_MENUCALL);
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::TextUnformatted(script.description.c_str());
								ImGui::EndTooltip();
							}
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}
}

void AS_OnSelectEntity()
{
}
void AS_OnFrameTick()
{
	for (const auto& script : scriptList)
	{
		ExecuteFunctionNoRet(script, AS_FUNC_ON_FRAMETICK);
	}
}