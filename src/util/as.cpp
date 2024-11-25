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
#include "../../add_on/scriptmath/scriptmath.h"
#include "../../add_on/scriptmath/scriptmathcomplex.h"
#include "../../add_on/datetime/datetime.h"
#include "../../add_on/scriptfile/scriptfile.h"
#include "../../add_on/scriptfile/scriptfilesystem.h"

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

	bool isBad;
	bool isActive;

	AScript(std::string file) : path(std::move(file)) 
	{
		isBad = true;
		isActive = false;
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
			RegisterScriptArray(engine, true);
			// Register the math
			RegisterScriptMath(engine);
			RegisterScriptMathComplex(engine);
			// Register the datatime
			RegisterScriptDateTime(engine);
			// Register the filesystem
			RegisterScriptFile(engine);
			RegisterScriptFileSystem(engine);
			// Register other
			RegisterStructs(engine);
			RegisterClasses(engine);
			RegisterNatives(engine);

			// Register the application interface with the script engine
			CScriptBuilder builder;
			int r = builder.StartNewModule(engine, modulename.c_str());
			if (r >= 0)
			{
				r = builder.AddSectionFromFile(path.c_str());
				if (r >= 0) 
				{
					r = builder.BuildModule();
					if (r >= 0)
					{
						ctx = engine->CreateContext();
						if (ctx)
						{
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

static std::vector<AScript *> scriptList{};

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

std::string Native_GetWorkDir()
{
	return g_working_dir;
}

int Native_CreateEntity(int mapIdx, const std::string& classname)
{
	for (auto& rend : mapRenderers)
	{
		if (rend->map && rend->map->realIdx == mapIdx)
		{
			rend->map->ents.push_back(new Entity(classname));
			rend->map->update_ent_lump();
			return (int)rend->map->ents.back()->realIdx;
		}
	}
	return -1;
}

bool Native_RemoveEntity(int entIdx)
{
	for (auto& rend : mapRenderers)
	{
		if (rend->map)
		{
			for (size_t i = 0; i < rend->map->ents.size(); i++)
			{
				if (rend->map->ents[i]->realIdx == entIdx)
				{
					rend->map->ents.erase(rend->map->ents.begin() + i);
					rend->map->update_ent_lump();
					return true;
				}
			}
		}
	}
	return false;
}

void Native_SetEntKeyVal(int entIdx, const std::string& key, const std::string& val)
{
	for (auto& rend : mapRenderers)
	{
		if (rend->map)
		{
			for (auto ent : rend->map->ents)
			{
				if (ent->realIdx == entIdx)
				{
					ent->setOrAddKeyvalue(key, val);
					return;
				}
			}
		}
	}
}

void Native_RefreshEnt(int entIdx, int flags)
{
	for (auto& rend : mapRenderers)
	{
		if (rend->map)
		{
			for (size_t i = 0; i < rend->map->ents.size(); i++)
			{
				if (rend->map->ents[i]->realIdx == entIdx)
				{
					rend->refreshEnt(i, flags);
					return;
				}
			}
		}
	}
}

int Native_FindEntityByKeyVal(int mapIdx, const std::string& key, const std::string& val)
{
	for (auto& rend : mapRenderers)
	{
		if (rend->map && rend->map->realIdx == mapIdx)
		{
			for (auto & ent : rend->map->ents)
			{
				if (ent->hasKey(key) && ent->keyvalues[key] == val)
				{
					return (int)ent->realIdx;
				}
			}
		}
	}
	return -1;
}

double AS_flLastFrameTime = 0.0;

float Native_GetFrameTime()
{
	return (float)(AS_flLastFrameTime);
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

	r = engine->RegisterGlobalFunction("string GetWorkDir()",
		asFUNCTIONPR(Native_GetWorkDir, (void), std::string), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("int CreateEntity(int mapIdx, const string &in)",
		asFUNCTIONPR(Native_CreateEntity, (int, const std::string&), int), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("bool RemoveEntity(int entIdx)",
		asFUNCTIONPR(Native_RemoveEntity, (int), bool), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("void SetEntKeyVal(int entIdx, const string &in, const string &in)",
		asFUNCTIONPR(Native_SetEntKeyVal, (int, const std::string&, const std::string&), void), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("void RefreshEnt(int entIdx, int flags)",
		asFUNCTIONPR(Native_RefreshEnt, (int, int), void), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("int FindEntityByKeyVal(int mapIdx, const string &in, const string &in)",
		asFUNCTIONPR(Native_FindEntityByKeyVal, (int, const std::string&, const std::string&), int), asCALL_CDECL); print_assert(r >= 0);

	r = engine->RegisterGlobalFunction("float GetLastFrameTime()",
		asFUNCTIONPR(Native_GetFrameTime, (void), float), asCALL_CDECL); print_assert(r >= 0);

	
}

void RegisterStructs(asIScriptEngine* engine)
{
	int r = engine->RegisterObjectType("vec3", sizeof(vec3), asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_CDK); print_assert(r >= 0);
	r = engine->RegisterObjectProperty("vec3", "float x", asOFFSET(vec3, x)); print_assert(r >= 0);
	r = engine->RegisterObjectProperty("vec3", "float y", asOFFSET(vec3, y)); print_assert(r >= 0);
	r = engine->RegisterObjectProperty("vec3", "float z", asOFFSET(vec3, z)); print_assert(r >= 0);

	r = engine->RegisterObjectBehaviour("vec3", asBEHAVE_CONSTRUCT, "void f()", asFUNCTIONPR([](void* memory) { new(memory) vec3(); }, (void*), void), asCALL_CDECL_OBJFIRST); print_assert(r >= 0);
	r = engine->RegisterObjectBehaviour("vec3", asBEHAVE_CONSTRUCT, "void f(float, float, float)", asFUNCTIONPR([](void* memory, float x, float y, float z) { new(memory) vec3(x, y, z); }, (void*, float, float, float), void), asCALL_CDECL_OBJFIRST); print_assert(r >= 0);
	r = engine->RegisterObjectBehaviour("vec3", asBEHAVE_DESTRUCT, "void f()", asFUNCTIONPR([](vec3* memory) { memory->~vec3(); }, (vec3*), void), asCALL_CDECL_OBJFIRST); print_assert(r >= 0);

	r = engine->RegisterObjectMethod("vec3", "void Copy(const vec3 &in)", asMETHOD(vec3, Copy), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "vec3& opAssign(const vec3 &in)", asMETHOD(vec3, operator=), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "vec3 opNeg()", asMETHOD(vec3, invert), asCALL_THISCALL); print_assert(r >= 0);

	r = engine->RegisterObjectMethod("vec3", "void opSubAssign(const vec3 &in)", asMETHODPR(vec3, operator-=, (const vec3&), void), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "void opAddAssign(const vec3 &in)", asMETHODPR(vec3, operator+=, (const vec3&), void), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "void opMulAssign(const vec3 &in)", asMETHODPR(vec3, operator*=, (const vec3&), void), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "void opDivAssign(const vec3 &in)", asMETHODPR(vec3, operator/=, (const vec3&), void), asCALL_THISCALL); print_assert(r >= 0);

	r = engine->RegisterObjectMethod("vec3", "void opSubAssign(float)", asMETHODPR(vec3, operator-=, (float), void), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "void opAddAssign(float)", asMETHODPR(vec3, operator+=, (float), void), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "void opMulAssign(float)", asMETHODPR(vec3, operator*=, (float), void), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "void opDivAssign(float)", asMETHODPR(vec3, operator/=, (float), void), asCALL_THISCALL); print_assert(r >= 0);

	r = engine->RegisterObjectMethod("vec3", "float& opIndex(int)", asMETHODPR(vec3, operator[], (size_t), float&), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "float opIndex(int) const", asMETHODPR(vec3, operator[], (size_t) const, float), asCALL_THISCALL); print_assert(r >= 0);

	r = engine->RegisterObjectMethod("vec3", "vec3 opAdd(const vec3 &in) const", asMETHODPR(vec3, operator+, (const vec3&) const, vec3), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "vec3 opSub(const vec3 &in) const", asMETHODPR(vec3, operator-, (const vec3&) const, vec3), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "vec3 opMul(float) const", asMETHODPR(vec3, operator*, (float) const, vec3), asCALL_THISCALL); print_assert(r >= 0);
	r = engine->RegisterObjectMethod("vec3", "vec3 opDiv(float) const", asMETHODPR(vec3, operator/, (float) const, vec3), asCALL_THISCALL); print_assert(r >= 0);

	// WHY?
	/*r = engine->RegisterGlobalFunction("vec3 opMul(float, const vec3 &in)", asFUNCTIONPR(operator*, (float, const vec3&), vec3), asCALL_CDECL); print_assert(r >= 0);
	r = engine->RegisterGlobalFunction("vec3 opDiv(float, const vec3 &in)", asFUNCTIONPR(operator/, (float, const vec3&), vec3), asCALL_CDECL); print_assert(r >= 0);
	*/
}

void RegisterClasses(asIScriptEngine* engine)
{

}

template <typename T>
int ExecuteFunction(AScript * script, AS_FUNCS funcEnum, T& resultVar)
{
	int r = asEXECUTION_FINISHED;
	if (script->funcs[funcEnum])
	{
		r = script->ctx->Prepare(script->funcs[funcEnum]);
		if (r >= 0)
		{
			r = script->ctx->Execute();
			if (r == asEXECUTION_FINISHED) {
				resultVar = *static_cast<T*>(script->ctx->GetAddressOfReturnValue());
				return r;
			}
			else
			{
				PrintError("Error executing \"" + funcNamesStr[funcEnum] + "\" in script: " + script->path + "\n");
			}
		}
	}
	return r;
}

int ExecuteFunctionNoRet(const AScript * script, AS_FUNCS funcEnum)
{
	int r = asEXECUTION_FINISHED;
	if (script->funcs[funcEnum])
	{
		r = script->ctx->Prepare(script->funcs[funcEnum]);
		if (r >= 0)
		{
			r = script->ctx->Execute();
			if (r == asEXECUTION_FINISHED) 
			{
				return r;
			}
			else
			{
				PrintError("Error executing \"" + funcNamesStr[funcEnum] + "\" in script: " + script->path + "\n");
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
				scriptList.push_back(new AScript(entry.path().string()));
				if (scriptList.back()->isBad)
				{
					print_log(PRINT_RED, "Error loading {} script.\n", scriptList.back()->path);
					delete scriptList.back();
					scriptList.pop_back();
				}
				else
				{
					scriptList.back()->isActive = true;
				}
			}
		}
	}
	if (ec) {
		PrintError("Error loading global scripts: " + ec.message());
	}
	ec = {};

	// Load all scripts from ./scripts/maps/ directory
	if (dirExists("./scripts/maps")) 
	{
		for (const auto& dirEntry : fs::directory_iterator("./scripts/maps", ec)) 
		{
			if (dirEntry.is_directory()) 
			{
				std::string bsp_name = dirEntry.path().filename().string();
				std::string mapScriptsPath = dirEntry.path().string();
				for (const auto& entry : fs::recursive_directory_iterator(mapScriptsPath, ec)) 
				{
					if (entry.is_regular_file() && entry.path().extension() == ".as") 
					{
						scriptList.push_back(new AScript(entry.path().string()));
						scriptList.back()->bsp_name = bsp_name;
						if (scriptList.back()->isBad)
						{
							print_log(PRINT_RED, "Error loading {} script.\n", scriptList.back()->path);
							delete scriptList.back();
							scriptList.pop_back();
						}
					}
				}
			}
		}
	}

	if (ec) {
		PrintError("Error loading map scripts: " + ec.message());
	}

	for (auto& script : scriptList) 
	{
		ExecuteFunction(script, AS_FUNC_GET_NAME, script->name);
		ExecuteFunction(script, AS_FUNC_GET_CATEGORY, script->category_name);
		ExecuteFunction(script, AS_FUNC_GET_DESCRIPTION, script->description);
	}
}

static Bsp* AS_LastSelectedMap = NULL;

void AS_OnMapChange()
{
	if (!g_app)
		return;

	for (const auto& script : scriptList)
	{
		if (!script->isActive)
		{
			continue;
		}
		ExecuteFunctionNoRet(script, AS_FUNC_ON_MAPCHANGE);
	}

	if (AS_LastSelectedMap != g_app->SelectedMap)
	{
		AS_LastSelectedMap = g_app->SelectedMap;

		std::string bspName = "";

		if (AS_LastSelectedMap)
		{
			bspName = AS_LastSelectedMap->bsp_name;
		}

		for (auto& s : scriptList)
		{
			s->isActive = s->bsp_name.empty() || s->bsp_name == bspName;
		}
	}
}

void AS_OnGuiTick()
{
	if (ImGui::BeginMenu("Scripts###ScriptsMenu", !scriptList.empty())) 
	{
		std::set<std::string> globalCategories;
		std::set<std::string> mapCategories;

		for (const auto& script : scriptList) 
		{
			if (!script->isActive)
			{
				continue;
			}
			if (script->bsp_name.empty())
			{
				globalCategories.insert(script->category_name);
			}
			else 
			{
				mapCategories.insert(script->bsp_name);
			}
		}

		if (ImGui::BeginMenu("Global###GlobalMenu", !globalCategories.empty())) 
		{
			for (const auto& category : globalCategories) 
			{
				if (ImGui::BeginMenu((category + "###GlobalCategory_" + category).c_str())) 
				{
					for (const auto& script : scriptList) 
					{
						if (!script->isActive)
						{
							continue;
						}
						if (script->bsp_name.empty() && script->category_name == category)
						{
							if (ImGui::MenuItem((script->name + "###GlobalScript_" + script->name).c_str()))
							{
								ExecuteFunctionNoRet(script, AS_FUNC_ON_MENUCALL);
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::TextUnformatted(script->description.c_str());
								ImGui::EndTooltip();
							}
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Map Scripts###MapScriptsMenu", !mapCategories.empty())) 
		{
			for (const auto& bspName : mapCategories) 
			{
				if (ImGui::BeginMenu((bspName + "###MapBsp_" + bspName).c_str())) 
				{
					for (const auto& script : scriptList) 
					{
						if (!script->isActive)
						{
							continue;
						}
						if (!script->bsp_name.empty() && script->bsp_name == bspName)
						{
							if (ImGui::MenuItem((script->name + "###MapScript_" + script->name).c_str()))
							{
								ExecuteFunctionNoRet(script, AS_FUNC_ON_MENUCALL);
							}
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::TextUnformatted(script->description.c_str());
								ImGui::EndTooltip();
							}
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Hot reload"))
		{
			for (auto& s : scriptList)
				delete s;
			scriptList.clear();
			AS_LastSelectedMap = NULL;
			InitializeAngelScripts();
			AS_OnMapChange();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Warning! Hot reload can cause issues!");
			ImGui::EndTooltip();
		}

		ImGui::EndMenu();
	}
}

void AS_OnSelectEntity()
{
}
void AS_OnFrameTick(double msec)
{
	AS_flLastFrameTime = msec;

	for (const auto& script : scriptList)
	{
		if (!script->isActive)
		{
			continue;
		}
		ExecuteFunctionNoRet(script, AS_FUNC_ON_FRAMETICK);
	}
}