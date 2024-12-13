#include "lang.h"
#include "Command.h"
#include "Gui.h"
#include <lodepng.h>
#include "log.h"


Bsp* Command::getBsp()
{
	if (mapIdx < 0 || mapIdx >= (int)mapRenderers.size())
	{
		return NULL;
	}

	return mapRenderers[mapIdx]->map;
}

BspRenderer* Command::getBspRenderer()
{
	if (mapIdx < 0 || mapIdx >= (int)mapRenderers.size())
	{
		return NULL;
	}

	return mapRenderers[mapIdx];
}

EditEntityCommand::EditEntityCommand(std::string desc, int entIdx, Entity _oldEntData, Entity _newEntData)
	: Command(desc, g_app->getSelectedMapId()), oldEntData(std::move(_oldEntData)), newEntData(std::move(_newEntData))
{
	this->entIdx = entIdx;
	this->allowedDuringLoad = true;
}

void EditEntityCommand::execute()
{
	Entity* target = getEnt();
	*target = newEntData;
	refresh();
	BspRenderer* renderer = getBspRenderer();
	renderer->saveEntityState(entIdx);
}

void EditEntityCommand::undo()
{
	Entity* target = getEnt();
	*target = oldEntData;
	refresh();
	BspRenderer* renderer = getBspRenderer();
	renderer->saveEntityState(entIdx);
}

Entity* EditEntityCommand::getEnt()
{
	Bsp* map = getBsp();

	if (!map || entIdx < 0 || entIdx >= (int)map->ents.size())
	{
		return NULL;
	}

	return map->ents[entIdx];
}

void EditEntityCommand::refresh()
{
	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;
	Entity* ent = getEnt();
	if (!ent)
		return;
	renderer->refreshEnt(entIdx);
	pickCount++; // force GUI update
}

size_t EditEntityCommand::memoryUsage()
{
	return sizeof(EditEntityCommand) + oldEntData.getMemoryUsage() + newEntData.getMemoryUsage();
}

//
// Delete entity
//
DeleteEntityCommand::DeleteEntityCommand(std::string desc, int entIdx)
	: Command(desc, g_app->getSelectedMapId())
{
	this->entIdx = entIdx;
	this->entData = new Entity();
	*this->entData = *(g_app->getSelectedMap()->ents[entIdx]);
	this->allowedDuringLoad = true;
}

DeleteEntityCommand::~DeleteEntityCommand()
{
	delete entData;
}

void DeleteEntityCommand::execute()
{
	Bsp* map = getBsp();

	if (!map)
		return;

	g_app->deselectObject();

	Entity* ent = map->ents[entIdx];

	map->ents.erase(map->ents.begin() + entIdx);

	refresh();

	delete ent;
}

void DeleteEntityCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.insert(map->ents.begin() + entIdx, newEnt);

	g_app->pickInfo.SetSelectedEnt(entIdx);

	refresh();

	if (newEnt->hasKey("model") &&
		toLowerCase(newEnt->keyvalues["model"]).find(".bsp") != std::string::npos)
	{
		g_app->reloadBspModels();
	}
}

void DeleteEntityCommand::refresh()
{
	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;

	renderer->preRenderEnts();
	g_app->gui->refresh();
}

size_t DeleteEntityCommand::memoryUsage()
{
	return sizeof(DeleteEntityCommand) + entData->getMemoryUsage();
}


//
// Create Entity
//
CreateEntityCommand::CreateEntityCommand(std::string desc, int mapIdx, Entity* entData) : Command(desc, mapIdx)
{
	this->entData = new Entity();
	*this->entData = *entData;
	this->allowedDuringLoad = true;
}

CreateEntityCommand::~CreateEntityCommand()
{
	delete entData;
}

void CreateEntityCommand::execute()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.push_back(newEnt);
	map->update_ent_lump();
	BspRenderer* renderer = getBspRenderer();
	if (!renderer || map->ents.size() == 0)
		return;
	renderer->refreshEnt((int)(map->ents.size()) - 1);
	g_app->gui->refresh();
}

void CreateEntityCommand::undo()
{
	Bsp* map = getBsp();
	if (!map || !map->ents.size())
		return;

	g_app->deselectObject();

	delete map->ents[map->ents.size() - 1];
	map->ents.pop_back();
	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;
	renderer->preRenderEnts();
	g_app->gui->refresh();
}

size_t CreateEntityCommand::memoryUsage()
{
	return sizeof(CreateEntityCommand) + entData->getMemoryUsage();
}

//
// Create Entities From Text
//
CreateEntityFromTextCommand::CreateEntityFromTextCommand(std::string desc, int mapIdx, std::string textData) : Command(desc, mapIdx) {
	this->textData = std::move(textData);
	this->allowedDuringLoad = true;
	createdEnts = 0;
}

CreateEntityFromTextCommand::~CreateEntityFromTextCommand() {
}

void CreateEntityFromTextCommand::execute() {
	Bsp* map = getBsp();

	std::istringstream in(textData);

	int lineNum = 0;
	int lastBracket = -1;
	Entity* ent = NULL;

	std::vector<Entity*> ents;

	std::string line = "";
	while (std::getline(in, line))
	{
		lineNum++;
		if (line.length() < 1 || line[0] == '\n')
			continue;

		if (line[0] == '{')
		{
			if (lastBracket == 0)
			{
				print_log("clipboard ent text data (line {}): Unexpected '{'\n", lineNum);
				continue;
			}
			lastBracket = 0;

			delete ent;
			ent = new Entity();
		}
		else if (line[0] == '}')
		{
			if (lastBracket == 1)
				print_log("clipboard ent text data (line {}): Unexpected '}'\n", lineNum);
			lastBracket = 1;

			if (ent == NULL)
				continue;

			if (ent->keyvalues.count("classname"))
				ents.push_back(ent);
			else
				print_log("Found unknown classname entity. Skip it.\n");
			ent = NULL;

			// you can end/start an ent on the same line, you know
			if (line.find('{') != std::string::npos)
			{
				ent = new Entity();
				lastBracket = 0;
			}
		}
		else if (lastBracket == 0 && ent != NULL) // currently defining an entity
		{
			Keyvalues keyvals(line);
			for (size_t k = 0; k < keyvals.keys.size();k++)
			{
				if (keyvals.keys[k].length() && keyvals.values[k].length())
					ent->addKeyvalue(keyvals.keys[k], keyvals.values[k]);
			}
		}
	}

	for (Entity* e : ents) {
		map->ents.push_back(e);
	}
	createdEnts = ents.size();
	print_log("Pasted {} entities from clipboard\n", createdEnts);
	
	refresh();
}

void CreateEntityFromTextCommand::undo() {
	Bsp* map = getBsp();

	g_app->deselectObject();

	for (size_t i = 0; i < createdEnts; i++) {
		delete map->ents[map->ents.size() - 1];
		map->ents.pop_back();
	}
	
	refresh();
}

void CreateEntityFromTextCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();
	renderer->preRenderEnts();
	g_app->gui->refresh();
	g_app->updateCullBox();
}

size_t CreateEntityFromTextCommand::memoryUsage() {
	return sizeof(CreateEntityFromTextCommand) + textData.size();
}

//
// Duplicate BSP Model command
//
DuplicateBspModelCommand::DuplicateBspModelCommand(std::string desc, int entIdx)
	: Command(desc, g_app->getSelectedMapId())
{
	int tmpentIdx = entIdx;
	int modelIdx = -1;
	Bsp* map = g_app->getSelectedMap();
	if (map && tmpentIdx >= 0)
	{
		modelIdx = map->ents[tmpentIdx]->getBspModelIdx();
		if (modelIdx < 0 && map->is_worldspawn_ent(tmpentIdx))
		{
			modelIdx = 0;
		}
	}

	this->oldModelIdx = modelIdx;
	this->newModelIdx = -1;
	this->entIdx = tmpentIdx;
	this->allowedDuringLoad = false;
}

DuplicateBspModelCommand::~DuplicateBspModelCommand()
{
}

void DuplicateBspModelCommand::execute()
{
	Bsp* map = getBsp();
	Entity* ent = map->ents[entIdx];
	BspRenderer* renderer = getBspRenderer();

	//int dupLumps = FL_CLIPNODES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS;
	oldLumps = map->duplicate_lumps(0xFFFFFFFF);

	newModelIdx = map->duplicate_model(oldModelIdx);

	ent->setOrAddKeyvalue("model", "*" + std::to_string(newModelIdx));

	map->remove_unused_model_structures(CLEAN_LEAVES);

	renderer->loadLightmaps();
	renderer->refreshEnt(entIdx);

	renderer->refreshModel(newModelIdx);

	g_app->pickInfo.selectedFaces.clear();

	if (g_app->pickInfo.selectedEnts.size())
		g_app->pickInfo.SetSelectedEnt(g_app->pickInfo.selectedEnts[0]);

	pickCount++;
	vertPickCount++;

	g_app->gui->refresh();
}

void DuplicateBspModelCommand::undo()
{
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	g_app->deselectObject();
	map->replace_lumps(oldLumps);

	Entity* ent = map->ents[entIdx];
	ent->setOrAddKeyvalue("model", "*" + std::to_string(oldModelIdx));


	map->update_ent_lump();
	renderer->reuploadTextures();
	renderer->loadLightmaps();
	renderer->preRenderEnts();
	g_app->gui->refresh();
}

size_t DuplicateBspModelCommand::memoryUsage()
{
	size_t size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size();
	}

	return size;
}


//
// Create BSP model
//
CreateBspModelCommand::CreateBspModelCommand(std::string desc, int mapIdx, Entity* entData, float size, bool empty) : Command(desc, mapIdx)
{
	this->entData = new Entity();
	*this->entData = *entData;
	this->mdl_size = size;
	this->empty = empty;
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		oldLumps.lumps[i].clear();
	}
}

CreateBspModelCommand::~CreateBspModelCommand()
{
	if (entData)
	{
		delete entData;
		entData = NULL;
	}
}

void CreateBspModelCommand::execute()
{
	Bsp* map = getBsp();
	if (!map)
		return;
	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;

	int aaatriggerIdx = getDefaultTextureIdx();

	int dupLumps = FL_MARKSURFACES | FL_EDGES | FL_FACES | FL_NODES | FL_PLANES | FL_CLIPNODES | FL_SURFEDGES | FL_TEXINFO | FL_VERTICES | FL_LIGHTING | FL_MODELS | FL_LEAVES;
	if (aaatriggerIdx == -1)
	{
		dupLumps |= FL_TEXTURES;
	}
	oldLumps = map->duplicate_lumps(dupLumps);

	bool NeedreloadTextures = false;
	// add the aaatrigger texture if it doesn't already exist
	if (aaatriggerIdx == -1)
	{
		NeedreloadTextures = true;
		aaatriggerIdx = addDefaultTexture();
	}

	vec3 mins = vec3(-mdl_size, -mdl_size, -mdl_size);
	vec3 maxs = vec3(mdl_size, mdl_size, mdl_size);
	int modelIdx = map->create_solid(mins, maxs, aaatriggerIdx, empty);
	//BSPMODEL& model = map->models[modelIdx];

	entData->addKeyvalue("model", "*" + std::to_string(modelIdx));

	Entity* newEnt = new Entity();
	*newEnt = *entData;
	map->ents.push_back(newEnt);

	g_app->deselectObject();

	//renderer->updateLightmapInfos();
	//renderer->preRenderFaces();
	//renderer->preRenderEnts();
	//renderer->reloadTextures();
	//renderer->reloadLightmaps();
	//renderer->refreshModel(modelIdx);
	//

	map->update_ent_lump();

	map->save_undo_lightmaps();
	map->resize_all_lightmaps();

	if (NeedreloadTextures)
		renderer->reuploadTextures();
	renderer->loadLightmaps();
	renderer->refreshModel(modelIdx);
	if (map->ents.size() > 0)
		renderer->refreshEnt((int)(map->ents.size()) - 1);
	renderer->calcFaceMaths();
	//g_app->reloading = true;
	//renderer->reload();
	//g_app->reloading = false;

	g_app->gui->refresh();
}

void CreateBspModelCommand::undo()
{
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();

	if (!map || !renderer || !map->ents.size())
		return;

	map->replace_lumps(oldLumps);

	delete map->ents[map->ents.size() - 1];
	map->ents.pop_back();

	map->save_undo_lightmaps();
	map->resize_all_lightmaps();

	renderer->reload();

	g_app->gui->refresh();
	g_app->deselectObject();
}

size_t CreateBspModelCommand::memoryUsage()
{
	size_t size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size();
	}

	return size;
}

int CreateBspModelCommand::getDefaultTextureIdx()
{
	Bsp* map = getBsp();
	if (!map)
		return -1;

	unsigned int totalTextures = ((unsigned int*)map->textures)[0];
	for (unsigned int i = 0; i < totalTextures; i++)
	{
		int texOffset = ((int*)map->textures)[i + 1];
		if (texOffset >= 0)
		{
			BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
			if (tex.szName[0] != '\0' && strcasecmp(tex.szName, "aaatrigger") == 0)
			{
				tex.nWidth = aaatriggerTex->width;
				tex.nHeight = aaatriggerTex->height;
				print_log(get_localized_string(LANG_0295));
				return i;
			}
		}
	}

	return -1;
}

int CreateBspModelCommand::addDefaultTexture()
{
	Bsp* map = getBsp();
	if (!map)
		return -1;
	int aaatriggerIdx = map->add_texture("aaatrigger", aaatriggerTex->get_data(), aaatriggerTex->width, aaatriggerTex->height);

	return aaatriggerIdx;
}

//
// Edit BSP model
//
EditBspModelCommand::EditBspModelCommand(std::string desc, int entIdx, LumpState oldLumps, LumpState newLumps,
	vec3 oldOrigin, unsigned int targetLumps) : Command(desc, g_app->getSelectedMapId())
{

	this->oldLumps = oldLumps;
	this->newLumps = newLumps;
	this->targetLumps = targetLumps;
	this->allowedDuringLoad = false;
	this->oldOrigin = oldOrigin;
	this->entIdx = entIdx;

	Bsp* map = g_app->getSelectedMap();
	if (map && entIdx >= 0)
	{
		this->modelIdx = map->ents[entIdx]->getBspModelIdx();
		this->newOrigin = map->ents[entIdx]->origin;
	}
	else
	{
		this->modelIdx = -1;
		this->newOrigin = this->oldOrigin;
	}
}

EditBspModelCommand::~EditBspModelCommand()
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		oldLumps.lumps[i].clear();
		newLumps.lumps[i].clear();
	}
}

void EditBspModelCommand::execute()
{
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();
	if (!map || !renderer)
		return;

	map->replace_lumps(newLumps);
	if (entIdx < (int)map->ents.size())
	{
		map->ents[entIdx]->setOrAddKeyvalue("origin", newOrigin.toKeyvalueString());
		map->getBspRender()->undoEntityStateMap[entIdx].setOrAddKeyvalue("origin", newOrigin.toKeyvalueString());
		refresh();
	}
}

void EditBspModelCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	map->replace_lumps(oldLumps);
	map->ents[entIdx]->setOrAddKeyvalue("origin", oldOrigin.toKeyvalueString());
	map->getBspRender()->undoEntityStateMap[entIdx].setOrAddKeyvalue("origin", oldOrigin.toKeyvalueString());
	refresh();

	BspRenderer* renderer = getBspRenderer();

	if (targetLumps & FL_VERTICES)
	{
		if (renderer)
		{
			renderer->preRenderFaces();
		}
	}

	if (targetLumps & FL_TEXTURES)
	{
		if (renderer)
		{
			renderer->reuploadTextures();
			renderer->preRenderFaces();
		}
	}
	if (targetLumps & FL_LIGHTING)
	{
		if (renderer)
		{
			renderer->loadLightmaps();
		}
	}
}

void EditBspModelCommand::refresh()
{
	Bsp* map = getBsp();
	if (!map)
		return;
	BspRenderer* renderer = getBspRenderer();

	bool updateVerts = false;

	if (newLumps.lumps[LUMP_LIGHTING].size())
	{
		map->getBspRender()->loadLightmaps();
		updateVerts = true;
	}

	if (newLumps.lumps[LUMP_ENTITIES].size())
	{
		renderer->refreshEnt(entIdx);
	}

	renderer->refreshModel(modelIdx);

	g_app->gui->refresh();

	vertPickCount++;
}

size_t EditBspModelCommand::memoryUsage()
{
	size_t size = sizeof(DuplicateBspModelCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size() + newLumps.lumps[i].size();
	}

	return size;
}


//
// Delete boxed data
//
DeleteBoxedDataCommand::DeleteBoxedDataCommand(std::string desc, int mapIdx, vec3 mins, vec3 maxs, LumpState oldLumps) : Command(desc, mapIdx) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
	this->mins = mins;
	this->maxs = maxs;
}

DeleteBoxedDataCommand::~DeleteBoxedDataCommand()
{

}

void DeleteBoxedDataCommand::execute() {
	Bsp* map = getBsp();

	map->delete_box_data(mins, maxs);

	refresh();
}

void DeleteBoxedDataCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);
	map->load_ents();

	refresh();
}

void DeleteBoxedDataCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
}

size_t DeleteBoxedDataCommand::memoryUsage() 
{
	size_t size = sizeof(DeleteBoxedDataCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumps[i].size();
	}

	return size;
}



//
// Clean Map
//
CleanMapCommand::CleanMapCommand(std::string desc, int mapIdx, LumpState oldLumps) : Command(desc, mapIdx)
{
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
}

CleanMapCommand::~CleanMapCommand()
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		oldLumps.lumps[i].clear();
	}
}

void CleanMapCommand::execute()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	BspRenderer* renderer = getBspRenderer();
	if (!renderer)
		return;
	print_log(get_localized_string(LANG_0296), map->bsp_name);
	map->remove_unused_model_structures().print_delete_stats(1);

	refresh();
}

void CleanMapCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	map->replace_lumps(oldLumps);

	refresh();
}

void CleanMapCommand::refresh()
{
	Bsp* map = getBsp();
	if (!map)
		return;
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
}

size_t CleanMapCommand::memoryUsage()
{
	size_t size = sizeof(CleanMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size();
	}

	return size;
}


//
// Delete OOB data
//
DeleteOobDataCommand::DeleteOobDataCommand(std::string desc, int mapIdx, int clipFlags, LumpState oldLumps) : Command(desc, mapIdx) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
	this->clipFlags = clipFlags;
}

DeleteOobDataCommand::~DeleteOobDataCommand() 
{

}

void DeleteOobDataCommand::execute() {
	Bsp* map = getBsp();

	map->delete_oob_data(clipFlags);

	refresh();
}

void DeleteOobDataCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);
	map->load_ents();

	refresh();
}

void DeleteOobDataCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
}

size_t DeleteOobDataCommand::memoryUsage() {
	size_t size = sizeof(DeleteOobDataCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumps[i].size();
	}

	return size;
}


//
// Fix bad surface extents
//
FixSurfaceExtentsCommand::FixSurfaceExtentsCommand(std::string desc, int mapIdx, bool scaleNotSubdivide,
		bool downscaleOnly, int maxTextureDim, LumpState oldLumps) : Command(desc, mapIdx) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
	this->scaleNotSubdivide = scaleNotSubdivide;
	this->downscaleOnly = downscaleOnly;
	this->maxTextureDim = maxTextureDim;
}

FixSurfaceExtentsCommand::~FixSurfaceExtentsCommand() 
{
	
}

void FixSurfaceExtentsCommand::execute() {
	Bsp* map = getBsp();

	map->fix_bad_surface_extents(scaleNotSubdivide, downscaleOnly, maxTextureDim);

	refresh();
}

void FixSurfaceExtentsCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);

	refresh();
}

void FixSurfaceExtentsCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
}

size_t FixSurfaceExtentsCommand::memoryUsage() {
	size_t size = sizeof(FixSurfaceExtentsCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumps[i].size();
	}

	return size;
}


//
// Deduplicate models
//
DeduplicateModelsCommand::DeduplicateModelsCommand(std::string desc, int mapIdx, LumpState oldLumps) : Command(desc, mapIdx) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
}

DeduplicateModelsCommand::~DeduplicateModelsCommand() {

}

void DeduplicateModelsCommand::execute() {
	Bsp* map = getBsp();

	map->deduplicate_models();

	refresh();
}

void DeduplicateModelsCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);
	map->load_ents();

	refresh();
}

void DeduplicateModelsCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
}

size_t DeduplicateModelsCommand::memoryUsage() {
	size_t size = sizeof(DeduplicateModelsCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumps[i].size();
	}

	return size;
}


//
// Move the entire map
//
MoveMapCommand::MoveMapCommand(std::string desc, int mapIdx, vec3 offset, LumpState oldLumps) : Command(desc, mapIdx) {
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
	this->offset = offset;
}

MoveMapCommand::~MoveMapCommand() {

}

void MoveMapCommand::execute() {
	Bsp* map = getBsp();

	map->ents[0]->removeKeyvalue("origin");
	map->move(offset);

	refresh();
}

void MoveMapCommand::undo() {
	Bsp* map = getBsp();

	map->replace_lumps(oldLumps);
	map->ents[0]->setOrAddKeyvalue("origin", offset.toKeyvalueString());

	refresh();
}

void MoveMapCommand::refresh() {
	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
}

size_t MoveMapCommand::memoryUsage() {
	size_t size = sizeof(MoveMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++) {
		size += oldLumps.lumps[i].size();
	}

	return size;
}

//
// Optimize Map
//
OptimizeMapCommand::OptimizeMapCommand(std::string desc, int mapIdx, LumpState oldLumps) : Command(desc, mapIdx)
{
	this->oldLumps = oldLumps;
	this->allowedDuringLoad = false;
}

OptimizeMapCommand::~OptimizeMapCommand()
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		oldLumps.lumps[i].clear();
	}
}

void OptimizeMapCommand::execute()
{
	Bsp* map = getBsp();
	BspRenderer* renderer = getBspRenderer();
	if (!map || !renderer)
		return;
	map->update_ent_lump();

	print_log(get_localized_string(LANG_0297), map->bsp_name);
	if (!map->has_hull2_ents())
	{
		print_log(get_localized_string(LANG_0298));
		map->delete_hull(2, 1);
	}

	bool oldVerbose = g_settings.verboseLogs;
	g_settings.verboseLogs = true;
	auto removestats = map->delete_unused_hulls(true);

	removestats.print_delete_stats(1);
	g_settings.verboseLogs = oldVerbose;

	refresh();
}

void OptimizeMapCommand::undo()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	map->replace_lumps(oldLumps);

	refresh();
}

void OptimizeMapCommand::refresh()
{
	Bsp* map = getBsp();
	if (!map)
		return;

	BspRenderer* renderer = getBspRenderer();

	renderer->reload();
	g_app->deselectObject();
	g_app->gui->refresh();
}

size_t OptimizeMapCommand::memoryUsage()
{
	size_t size = sizeof(OptimizeMapCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size();
	}

	return size;
}