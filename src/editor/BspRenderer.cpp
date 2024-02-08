#include "lang.h"
#include <string.h>
#include <algorithm>
#include <execution>
#include "BspRenderer.h"
#include "VertexBuffer.h"
#include "primitives.h"
#include "rad.h"
#include "vis.h"
#include "lodepng.h"
#include "Settings.h"
#include "Renderer.h"
#include "Clipper.h"
#include "Command.h"
#include "Sprite.h"
#include "Gui.h"

BspRenderer::BspRenderer(Bsp* _map)
{
	map = _map;
	map->setBspRender(this);
	lightmaps = NULL;
	glTexturesSwap = NULL;
	glTextures = NULL;
	faceMaths = NULL;
	leafCube = new EntCube();
	nodeCube = new EntCube();/*
	nodePlaneCube = new EntCube();*/
	old_rend_offs = vec3();

	leafCube->sel_color = { 0, 255, 255, 150 };
	leafCube->mins = { -32.0f,-32.0f,-32.0f };
	leafCube->maxs = { 32.0f ,32.0f ,32.0f };

	g_app->pointEntRenderer->genCubeBuffers(leafCube);

	nodeCube->sel_color = { 255, 0, 255, 150 };
	nodeCube->mins = { -32.0f,-32.0f,-32.0f };
	nodeCube->maxs = { 32.0f ,32.0f ,32.0f };

	g_app->pointEntRenderer->genCubeBuffers(nodeCube);

	//nodePlaneCube->sel_color = { 255, 255, 0, 150 };
	//nodePlaneCube->mins = { -32.0f,-32.0f,-32.0f };
	//nodePlaneCube->maxs = { 32.0f ,32.0f ,32.0f };

	//g_app->pointEntRenderer->genCubeBuffers(nodePlaneCube);


	lightEnableFlags[0] = lightEnableFlags[1] = lightEnableFlags[2] = lightEnableFlags[3] = true;

	intersectVec = mapOffset = renderOffset = localCameraOrigin = vec3();

	renderClipnodes = {};

	renderCameraOrigin = renderCameraAngles = vec3();
	renderCameraAngles.z = 90.0f;
	// Setup Deafult Camera

	if (g_settings.start_at_entity)
	{
		print_log(get_localized_string(LANG_0267));
		Entity* foundEnt = NULL;
		for (auto ent : map->ents)
		{
			if (ent->hasKey("classname") && ent->keyvalues["classname"] == "info_player_start")
			{
				foundEnt = ent;
				break;
			}
		}
		if (!foundEnt)
		{
			for (auto ent : map->ents)
			{
				if (ent->hasKey("classname") && ent->keyvalues["classname"] == "info_player_deathmatch")
				{
					foundEnt = ent;
					break;
				}
			}
		}
		if (!foundEnt)
		{
			for (auto ent : map->ents)
			{
				if (ent->hasKey("classname") && ent->keyvalues["classname"] == "trigger_camera")
				{
					foundEnt = ent;
					break;
				}
			}
		}


		if (foundEnt)
		{
			renderCameraOrigin = foundEnt->origin;
			renderCameraOrigin.z += 32;
			for (unsigned int i = 0; i < foundEnt->keyOrder.size(); i++)
			{
				if (foundEnt->keyOrder[i] == "angles")
				{
					renderCameraAngles = parseVector(foundEnt->keyvalues["angles"]);
				}
				if (foundEnt->keyOrder[i] == "angle")
				{
					float y = (float)atof(foundEnt->keyvalues["angle"].c_str());

					if (y >= 0.0f)
					{
						renderCameraAngles.y = y;
					}
					else if (y == -1.0f)
					{
						renderCameraAngles.x = -90.0f;
						renderCameraAngles.y = 0.0f;
						renderCameraAngles.z = 0.0f;
					}
					else if (y <= -2.0f)
					{
						renderCameraAngles.x = 90.0f;
						renderCameraAngles.y = 0.0f;
						renderCameraAngles.z = 0.0f;
					}
				}
			}

			renderCameraAngles = renderCameraAngles.flip();
			renderCameraAngles.z = renderCameraAngles.z + 90.0f;
			renderCameraAngles = renderCameraAngles.normalize_angles();
			renderCameraAngles.y = 0.0f;
		}


		/*for (auto ent : map->ents)
		{
			if (ent->hasKey("classname") && ent->keyvalues["classname"] == "info_player_start")
			{
				renderCameraOrigin = ent->origin;

				/*for (unsigned int i = 0; i < ent->keyOrder.size(); i++)
				{
					if (ent->keyOrder[i] == "angles")
					{
						renderCameraAngles = parseVector(ent->keyvalues["angles"]);
					}
					if (ent->keyOrder[i] == "angle")
					{
						float y = (float)atof(ent->keyvalues["angle"].c_str());

						if (y >= 0.0f)
						{
							renderCameraAngles.y = y;
						}
						else if (y == -1.0f)
						{
							renderCameraAngles.x = -90.0f;
							renderCameraAngles.y = 0.0f;
							renderCameraAngles.z = 0.0f;
						}
						else if (y <= -2.0f)
						{
							renderCameraAngles.x = 90.0f;
							renderCameraAngles.y = 0.0f;
							renderCameraAngles.z = 0.0f;
						}
					}
				}

				break;
			}
*/


//if (ent->hasKey("classname") && ent->keyvalues["classname"] == "trigger_camera")
//{
//	this->renderCameraOrigin = ent->origin;
	/*
	auto targets = ent->getTargets();
	bool found = false;
	for (auto ent2 : map->ents)
	{
		if (found)
			break;
		if (ent2->hasKey("targetname"))
		{
			for (auto target : targets)
			{
				if (ent2->keyvalues["targetname"] == target)
				{
					found = true;
					break;
				}
			}
		}
	}
	*/
	/*		break;
		}
}*/
	}


	if (g_settings.save_cam)
	{
		if (!map->save_cam_pos.IsZero())
		{
			renderCameraOrigin = map->save_cam_pos;
		}

		if (!map->save_cam_angles.IsZero())
		{
			renderCameraAngles = map->save_cam_angles;
		}
	}

	if (g_app->getSelectedMap() == NULL || map == g_app->getSelectedMap())
	{
		cameraOrigin = renderCameraOrigin;
		cameraAngles = renderCameraAngles;
	}

	renderEnts.clear();
	renderModels.clear();
	renderClipnodes.clear();

	faceMaths = NULL;


	nodesBufferCache.clear();
	clipnodesBufferCache.clear();
	clearDrawCache();
	//loadTextures();
	//loadLightmaps();
	calcFaceMaths();
	preRenderFaces();
	preRenderEnts();

	//numRenderClipnodes = map->modelCount;
	lightmapFuture = std::async(std::launch::async, &BspRenderer::loadLightmaps, this);
	texturesFuture = std::async(std::launch::async, &BspRenderer::loadTextures, this);
	clipnodesFuture = std::async(std::launch::async, &BspRenderer::loadClipnodes, this);

	// cache ent targets so first selection doesn't lag
	for (size_t i = 0; i < map->ents.size(); i++)
	{
		map->ents[i]->getTargets();
	}

	undoLumpState = LumpState();
	undoEntityStateMap = std::map<size_t, Entity>();

	saveLumpState();
}

void BspRenderer::loadTextures()
{
	for (size_t i = 0; i < wads.size(); i++)
	{
		delete wads[i];
	}
	wads.clear();

	std::vector<std::string> wadNames;

	bool foundInfoDecals = false;
	bool foundDecalWad = false;

	for (size_t i = 0; i < map->ents.size(); i++)
	{
		if (map->ents[i]->keyvalues["classname"] == "worldspawn")
		{
			wadNames = splitString(map->ents[i]->keyvalues["wad"], ";");

			for (size_t k = 0; k < wadNames.size(); k++)
			{
				wadNames[k] = basename(wadNames[k]);
				if (toLowerCase(wadNames[k]) == "decals.wad")
					foundDecalWad = true;
			}

			if (g_settings.stripWad)
			{
				std::string newWadString = "";

				for (size_t k = 0; k < wadNames.size(); k++)
				{
					newWadString += wadNames[k] + ";";
				}
				map->ents[i]->setOrAddKeyvalue("wad", newWadString);
			}
		}
		if (map->ents[i]->keyvalues["classname"] == "infodecal")
		{
			foundInfoDecals = true;
		}
	}

	std::vector<std::string> tryPaths{};
	tryPaths.push_back("./");

	for (auto& path : g_settings.resPaths)
	{
		if (path.enabled)
			tryPaths.push_back(path.path);
	}

	if (foundInfoDecals && !foundDecalWad)
	{
		wadNames.push_back("decals.wad");
	}

	for (size_t i = 0; i < wadNames.size(); i++)
	{
		std::string path = std::string();
		if (FindPathInAssets(map, wadNames[i], path))
		{
			print_log(get_localized_string(LANG_0269), path);
			Wad* wad = new Wad(path);
			if (wad->readInfo())
				wads.push_back(wad);
			else
			{
				print_log(get_localized_string(LANG_0270), path);
				delete wad;
			}
		}
		else if (path.empty())
		{
			print_log(get_localized_string(LANG_0268), wadNames[i]);
			FindPathInAssets(map, wadNames[i], path, true);
			continue;
		}
	}

	int wadTexCount = 0;
	int missingCount = 0;
	int embedCount = 0;

	glTexturesSwap = new Texture * [map->textureCount];
	for (int i = 0; i < map->textureCount; i++)
	{
		int texOffset = ((int*)map->textures)[i + 1];
		if (texOffset < 0)
		{
			glTexturesSwap[i] = missingTex;
			continue;
		}

		BSPMIPTEX* tex = ((BSPMIPTEX*)(map->textures + texOffset));
		if (tex->szName[0] == '\0' || tex->nWidth == 0 || tex->nHeight == 0 || strlen(tex->szName) >= MAXTEXTURENAME)
		{
			glTexturesSwap[i] = missingTex;
			continue;
		}

		if (strcasecmp(tex->szName, "aaatrigger") == 0)
		{
			glTexturesSwap[i] = aaatriggerTex_rgba;
			continue;
		}

		if (strcasecmp(tex->szName, "sky") == 0)
		{
			glTexturesSwap[i] = skyTex_rgba;
			continue;
		}

		COLOR3* imageData = NULL;
		WADTEX* wadTex = NULL;
		std::string wadName = "unknown.wad";
		if (tex->nOffsets[0] <= 0)
		{
			bool foundInWad = false;
			for (size_t k = 0; k < wads.size(); k++)
			{
				if (wads[k]->hasTexture(tex->szName))
				{
					foundInWad = true;
					wadName = wads[k]->wadname;
					wadTex = wads[k]->readTexture(tex->szName);
					imageData = ConvertWadTexToRGB(wadTex);
					wadTexCount++;
					break;
				}
			}

			if (!foundInWad)
			{
				glTexturesSwap[i] = missingTex;
				missingCount++;
				continue;
			}
		}
		else
		{
			COLOR3 palette[256];
			if (g_settings.pal_id >= 0)
			{
				memcpy(palette, g_settings.palettes[g_settings.pal_id].data, g_settings.palettes[g_settings.pal_id].colors * sizeof(COLOR3));
			}
			else
			{
				memcpy(palette, g_settings.palette_default,
					256 * sizeof(COLOR3));
			}

			imageData = ConvertMipTexToRGB(tex, map->is_texture_with_pal(i) ? NULL : (COLOR3*)palette);
			embedCount++;
		}

		if (imageData)
		{
			if (wadTex)
			{
				glTexturesSwap[i] = new Texture(wadTex->nWidth, wadTex->nHeight, (unsigned char*)imageData, wadTex->szName);
				glTexturesSwap[i]->setWadName(wadName);
			}
			else
			{
				glTexturesSwap[i] = new Texture(tex->nWidth, tex->nHeight, (unsigned char*)imageData, tex->szName);
				glTexturesSwap[i]->setWadName("internal");
			}
		}
		else
		{
			glTexturesSwap[i] = missingTex;
		}

		if (wadTex)
			delete wadTex;
	}

	if (wadTexCount)
		print_log(get_localized_string(LANG_0271), wadTexCount);
	if (embedCount)
		print_log(get_localized_string(LANG_0272), embedCount);
	if (missingCount)
		print_log(get_localized_string(LANG_0273), missingCount);
}

void BspRenderer::reload()
{
	map->update_lump_pointers();
	loadLightmaps();
	calcFaceMaths();
	preRenderFaces();
	preRenderEnts();
	reloadTextures();
	reloadClipnodes();
}

void BspRenderer::reloadTextures()
{
	if (texturesFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		texturesLoaded = false;
		texturesFuture = std::async(std::launch::async, &BspRenderer::loadTextures, this);
	}
}

void BspRenderer::reloadLightmaps()
{
	if (lightmapFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		lightmapsGenerated = false;
		lightmapsUploaded = false;
		deleteLightmapTextures();
		if (lightmaps)
		{
			delete[] lightmaps;
			lightmaps = NULL;
		}
		lightmapFuture = std::async(std::launch::async, &BspRenderer::loadLightmaps, this);
	}
}

void BspRenderer::reloadClipnodes()
{
	if (clipnodesFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		clipnodesLoaded = false;
		clipnodeLeafCount = 0;

		deleteRenderClipnodes();

		clipnodesBufferCache.clear();
		nodesBufferCache.clear();

		clipnodesFuture = std::async(std::launch::async, &BspRenderer::loadClipnodes, this);
	}
}

RenderClipnodes* BspRenderer::addClipnodeModel(int modelIdx)
{
	if (modelIdx < 0)
	{
		return NULL;
	}

	if (numRenderClipnodes <= 0)
	{
		reloadClipnodes();
		return NULL;
	}

	if (renderClipnodes.size() < std::max(modelIdx, numRenderClipnodes) + 1)
		renderClipnodes.resize(std::max(modelIdx, numRenderClipnodes) + 1);

	renderClipnodes[modelIdx] = RenderClipnodes();

	numRenderClipnodes = std::max(modelIdx, numRenderClipnodes) + 1;

	generateClipnodeBuffer(modelIdx);

	return &renderClipnodes[modelIdx];
}

void BspRenderer::loadLightmaps()
{
	std::vector<LightmapNode*> atlases;
	std::vector<Texture*> atlasTextures;
	atlases.push_back(new LightmapNode(0, 0, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
	atlasTextures.push_back(new Texture(LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE,
		new unsigned char[LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3)], "LIGHTMAP"));
	memset(atlasTextures[atlasTextures.size() - 1]->get_data(), 255, LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

	numRenderLightmapInfos = map->faceCount;
	if (lightmaps)
	{
		lightmapsGenerated = false;
		lightmapsUploaded = false;
		deleteLightmapTextures();
		delete[] lightmaps;
	}
	lightmaps = new LightmapInfo[map->faceCount]{};

	print_log(get_localized_string(LANG_0274));

	int lightmapCount = 0;

	//std::vector<int> tmpFaceCount;
	//for (int i = 0; i < map->faceCount; i++)
	//{
	//	tmpFaceCount.push_back(i);
	//}

	//std::vector<int> tmpLightmapCount;
	//for (int i = 0; i < MAXLIGHTMAPS; i++)
	//{
	//	tmpLightmapCount.push_back(i);
	//}

	for (int i = 0; i < map->faceCount; i++)
	{
		BSPFACE32& face = map->faces[i];
		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
		if (!atlases.size())
		{

		}
		else
		{
			int size[2];
			int imins[2];
			int imaxs[2];
			GetFaceLightmapSize(map, i, size);
			GetFaceExtents(map, i, imins, imaxs);

			LightmapInfo& info = lightmaps[i];
			info.w = size[0];
			info.h = size[1];
			info.midTexU = (float)(size[0]) / 2.0f;
			info.midTexV = (float)(size[1]) / 2.0f;

			// TODO: float mins/maxs not needed?
			info.midPolyU = (imins[0] + imaxs[0]) * 16.0f / 2.0f;
			info.midPolyV = (imins[1] + imaxs[1]) * 16.0f / 2.0f;

			for (int s = 0; s < MAX_LIGHTMAPS; s++)
			{
				if (face.nStyles[s] == 255)
					continue;
				size_t atlasId = atlases.size() - 1;

				// TODO: Try fitting in earlier atlases before using the latest one
				if (!atlases[atlasId]->insert(info.w, info.h, info.x[s], info.y[s]))
				{
					atlases.push_back(new LightmapNode(0, 0, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE));
					atlasTextures.push_back(new Texture(LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE, new unsigned char[LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3)], "LIGHTMAP"));

					atlasId++;
					memset(atlasTextures[atlasId]->get_data(), 255, LIGHTMAP_ATLAS_SIZE * LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

					if (!atlases[atlasId]->insert(info.w, info.h, info.x[s], info.y[s]))
					{
						print_log(get_localized_string(LANG_0275), info.w, info.h, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE);
						continue;
					}
				}
				lightmapCount++;

				info.atlasId[s] = (int)atlasId;

				// copy lightmap data into atlas
				int lightmapSz = info.w * info.h * sizeof(COLOR3);
				int offset = face.nLightmapOffset + s * lightmapSz;

				COLOR3* lightSrc = (COLOR3*)(map->lightdata + offset);
				COLOR3* lightDst = (COLOR3*)(atlasTextures[atlasId]->get_data());
				for (int y = 0; y < info.h; y++)
				{
					for (int x = 0; x < info.w; x++)
					{
						int src = y * info.w + x;
						int dst = (info.y[s] + y) * LIGHTMAP_ATLAS_SIZE + info.x[s] + x;
						if (face.nLightmapOffset < 0 || texinfo.nFlags & TEX_SPECIAL || offset + src * (int)sizeof(COLOR3) >= map->lightDataLength)
						{
							// missing lightmap default white
							lightDst[dst] = { 255,255,255 };
						}
						else
						{
							lightDst[dst] = lightSrc[src];
						}
					}
				}

			}

		}
	}


	glLightmapTextures = new Texture * [atlasTextures.size()];
	for (unsigned int i = 0; i < atlasTextures.size(); i++)
	{
		glLightmapTextures[i] = atlasTextures[i];
		delete atlases[i];
	}

	numLightmapAtlases = atlasTextures.size();
	//lodepng_encode24_file("atlas.png", atlasTextures[0]->data, LIGHTMAP_ATLAS_SIZE, LIGHTMAP_ATLAS_SIZE);
	print_log(get_localized_string(LANG_0276), lightmapCount, atlases.size());

	lightmapsGenerated = true;
}

void BspRenderer::updateLightmapInfos()
{
	if (numRenderLightmapInfos == map->faceCount)
	{
		return;
	}

	if (map->faceCount < numRenderLightmapInfos)
	{
		// Already done in remove_unused_structs!!!
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0277));
		return;
	}

	// assumes new faces have no light data
	int addedFaces = map->faceCount - numRenderLightmapInfos;

	LightmapInfo* newLightmaps = new LightmapInfo[map->faceCount];

	memcpy(newLightmaps, lightmaps, std::min(numRenderLightmapInfos, map->faceCount) * sizeof(LightmapInfo));

	if (addedFaces > 0)
		memset(newLightmaps + numRenderLightmapInfos, 0x00, addedFaces * sizeof(LightmapInfo));

	delete[] lightmaps;
	lightmaps = newLightmaps;
	numRenderLightmapInfos = map->faceCount;

	print_log(get_localized_string(LANG_0278), addedFaces);
}

void BspRenderer::preRenderFaces()
{
	genRenderFaces(numRenderModels);

	for (int i = 0; i < numRenderModels; i++)
	{
		RenderModel& model = renderModels[i];
		for (int k = 0; k < model.groupCount; k++)
		{
			model.renderGroups[k].buffer->uploaded = false;
		}
	}

	for (auto f : g_app->pickInfo.selectedFaces)
	{
		highlightFace(f, 1);
	}
}

void BspRenderer::genRenderFaces(int& renderModelCount)
{
	deleteRenderFaces();

	renderModels.resize(map->modelCount);

	renderModelCount = map->modelCount;

	int worldRenderGroups = 0;
	int modelRenderGroups = 0;

	for (int m = 0; m < map->modelCount; m++)
	{
		int groupCount = refreshModel(m, false);
		if (m == 0)
			worldRenderGroups += groupCount;
		else
			modelRenderGroups += groupCount;
	}

	print_log("Created {} solid render groups ({} world, {} entity)\n",
		worldRenderGroups + modelRenderGroups,
		worldRenderGroups,
		modelRenderGroups);
}

void BspRenderer::addNewRenderFace()
{
	renderModels.resize(map->modelCount + 1);
	renderModels[map->modelCount] = RenderModel();
	numRenderModels = map->modelCount + 1;
	print_log(get_localized_string(LANG_0279));
}

void BspRenderer::deleteRenderModel(RenderModel* renderModel)
{
	if (!renderModel)
	{
		return;
	}

	if (renderModel->wireframeBuffer)
		delete renderModel->wireframeBuffer;
	renderModel->wireframeBuffer = NULL;

	if (renderModel->renderGroups)
	{
		for (int k = 0; k < renderModel->groupCount; k++)
		{
			RenderGroup& group = renderModel->renderGroups[k];

			if (group.buffer)
				delete group.buffer;

			group.buffer = NULL;
		}
		delete[] renderModel->renderGroups;
	}

	if (renderModel->renderFaces)
		delete[] renderModel->renderFaces;

	renderModel->renderGroups = NULL;
	renderModel->renderFaces = NULL;
}

void BspRenderer::deleteRenderClipnodes()
{
	if (renderClipnodes.size())
	{
		for (int i = 0; i < numRenderClipnodes; i++)
		{
			deleteRenderModelClipnodes(&renderClipnodes[i]);
		}
		renderClipnodes.clear();
	}
}

void BspRenderer::deleteRenderModelClipnodes(RenderClipnodes* renderClip)
{
	for (int i = 0; i < MAX_MAP_HULLS; i++)
	{
		if (renderClip->clipnodeBuffer[i])
		{
			delete renderClip->clipnodeBuffer[i];
		}
		renderClip->clipnodeBuffer[i] = NULL;

		if (renderClip->wireframeClipnodeBuffer[i])
		{
			delete renderClip->wireframeClipnodeBuffer[i];
		}
		renderClip->wireframeClipnodeBuffer[i] = NULL;
	}
}

void BspRenderer::deleteRenderFaces()
{
	if (renderModels.size())
	{
		for (int i = 0; i < numRenderModels; i++)
		{
			deleteRenderModel(&renderModels[i]);
		}
		renderModels.clear();
	}
}

void BspRenderer::deleteTextures()
{
	if (glTextures)
	{
		for (int i = 0; i < numLoadedTextures; i++)
		{
			if (glTextures[i] != missingTex
				&& glTextures[i] != aaatriggerTex_rgba
				&& glTextures[i] != skyTex_rgba)
			{
				delete glTextures[i];
				glTextures[i] = missingTex;
			}
		}
		delete[] glTextures;
	}

	glTextures = NULL;
}

void BspRenderer::deleteLightmapTextures()
{
	if (glLightmapTextures)
	{
		for (size_t i = 0; i < numLightmapAtlases; i++)
		{
			if (glLightmapTextures[i])
			{
				delete glLightmapTextures[i];
				glLightmapTextures[i] = NULL;
			}
		}
		delete[] glLightmapTextures;
	}

	glLightmapTextures = NULL;
}

void BspRenderer::deleteFaceMaths()
{
	if (faceMaths)
	{
		delete[] faceMaths;
	}

	faceMaths = NULL;
}

int BspRenderer::refreshModel(int modelIdx, bool refreshClipnodes, bool noTriangulate)
{
	if (modelIdx < 0)
		return 0;

	BSPMODEL& model = map->models[modelIdx];
	RenderModel* renderModel = &renderModels[modelIdx];

	deleteRenderModel(renderModel);

	renderModel->renderFaces = new RenderFace[model.nFaces];

	std::vector<RenderGroup> renderGroups{};
	std::vector<std::vector<lightmapVert>> renderGroupVerts{};

	std::vector<cVert> wireframeVerts_full;

	for (int i = 0; i < model.nFaces; i++)
	{
		int faceIdx = model.iFirstFace + i;
		BSPFACE32& face = map->faces[faceIdx];

		if (face.nEdges <= 0)
			continue;

		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
		BSPMIPTEX* tex = NULL;

		int texWidth, texHeight;
		if (texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount)
		{
			int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
			if (texOffset >= 0)
			{
				tex = ((BSPMIPTEX*)(map->textures + texOffset));
				texWidth = tex->nWidth;
				texHeight = tex->nHeight;
			}
			else
			{
				// missing texture
				texWidth = 16;
				texHeight = 16;
			}
		}
		else
		{
			// missing texture
			texWidth = 16;
			texHeight = 16;
		}


		LightmapInfo* lmap = lightmapsGenerated && lightmaps ? &lightmaps[faceIdx] : NULL;

		lightmapVert* verts = new lightmapVert[face.nEdges];
		int vertCount = face.nEdges;
		Texture* lightmapAtlas[MAX_LIGHTMAPS]{ NULL };

		float lw = 0;
		float lh = 0;

		if (lmap)
		{
			lw = (float)lmap->w / (float)LIGHTMAP_ATLAS_SIZE;
			lh = (float)lmap->h / (float)LIGHTMAP_ATLAS_SIZE;
		}

		bool isSpecial = texinfo.nFlags & TEX_SPECIAL;
		bool hasLighting = face.nStyles[0] != 255 && face.nLightmapOffset >= 0 && !isSpecial;

		for (int s = 0; s < MAX_LIGHTMAPS; s++)
		{
			lightmapAtlas[s] = hasLighting && lmap ? glLightmapTextures[lmap->atlasId[s]] : NULL;
		}

		if (isSpecial)
		{
			lightmapAtlas[0] = whiteTex;
		}

		int entIdx = map->get_ent_from_model(modelIdx);
		Entity* ent = entIdx >= 0 ? map->ents[entIdx] : NULL;

		bool isOpacity = isSpecial || (tex && IsTextureTransparent(tex->szName)) || (ent && ent->hasKey("classname") && g_app->isEntTransparent(ent->keyvalues["classname"].c_str()));

		float opacity = isOpacity ? 0.50f : 1.0f;

		bool isSky = false;
		bool isTrigger = true;

		if (tex)
		{
			isTrigger = strcasecmp(tex->szName, "aaatrigger") == 0
				|| strcasecmp(tex->szName, "clip") == 0
				|| strcasecmp(tex->szName, "origin") == 0
				|| strcasecmp(tex->szName, "translucent") == 0
				|| strcasecmp(tex->szName, "skip") == 0
				|| strcasecmp(tex->szName, "hint") == 0
				|| strcasecmp(tex->szName, "null") == 0
				|| strcasecmp(tex->szName, "bevel") == 0
				|| strcasecmp(tex->szName, "noclip") == 0
				|| strcasecmp(tex->szName, "solidhint") == 0;

			isSky = strcasecmp(tex->szName, "sky") == 0 ||
				strcasecmp(tex->szName, "skycull") == 0;
		}

		if (ent)
		{
			if (ent->rendermode != kRenderNormal)
			{
				opacity = ent->renderamt / 255.f;
				if (opacity > 0.8f && isOpacity)
					opacity = 0.8f;
				else if (opacity > 1.0f)
					opacity = 1.0f;
				else if (opacity < 0.35f)
					opacity = 0.35f;
			}
		}

		for (int e = 0; e < face.nEdges; e++)
		{
			int edgeIdx = map->surfedges[face.iFirstEdge + e];
			BSPEDGE32& edge = map->edges[abs(edgeIdx)];
			int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];

			vec3& vert = map->verts[vertIdx];
			verts[e].pos = vert.flip();

			verts[e].r = 0.0f;
			if (ent && ent->rendermode > 0)
			{
				verts[e].g = 1.001f + abs((float)ent->rendermode);
			}
			else
			{
				verts[e].g = 0.0f;
			}
			verts[e].b = 0.0f;
			verts[e].a = isSky || isTrigger || (ent && ent->rendermode > 0) ? 1.0f - opacity : 0.0f;

			// texture coords
			float tw = 1.0f;
			float th = 1.0f;

			if (isSky)
			{
				tw /= skyTex_rgba->width;
				th /= skyTex_rgba->height;
			}
			else if (isTrigger)
			{
				tw /= aaatriggerTex_rgba->width;
				th /= aaatriggerTex_rgba->height;
			}
			else
			{
				tw /= texWidth;
				th /= texHeight;
			}

			float fU = dotProduct(isSky || isTrigger ? texinfo.vS.normalize(1.0f) : texinfo.vS, vert) + (texinfo.shiftS);
			float fV = dotProduct(isSky || isTrigger ? texinfo.vT.normalize(1.0f) : texinfo.vT, vert) + (texinfo.shiftT);
			verts[e].u = fU * tw;
			verts[e].v = fV * th;
			// lightmap texture coords
			if (hasLighting && lmap)
			{
				float fLightMapU = lmap->midTexU + (fU - lmap->midPolyU) / 16.0f;
				float fLightMapV = lmap->midTexV + (fV - lmap->midPolyV) / 16.0f;

				float uu = (fLightMapU / (float)lmap->w) * lw;
				float vv = (fLightMapV / (float)lmap->h) * lh;

				float pixelStep = 1.0f / (float)LIGHTMAP_ATLAS_SIZE;

				for (int s = 0; s < MAX_LIGHTMAPS; s++)
				{
					verts[e].luv[s][0] = uu + lmap->x[s] * pixelStep;
					verts[e].luv[s][1] = vv + lmap->y[s] * pixelStep;
				}
			}
			// set lightmap scales
			for (int s = 0; s < MAX_LIGHTMAPS; s++)
			{
				verts[e].luv[s][2] = (hasLighting && face.nStyles[s] != 255) ? 1.0f : 0.0f;
				if (isSpecial && s == 0)
				{
					verts[e].luv[s][2] = 1.0f;
				}
			}
		}

		int idx = 0;


		int wireframeVertCount = face.nEdges * 2;
		std::vector<cVert> wireframeVerts;
		wireframeVerts.resize(wireframeVertCount);
		for (int k = 0; k < face.nEdges && (k + 1) % face.nEdges < face.nEdges; k++)
		{
			wireframeVerts[idx++].pos = verts[k].pos;
			wireframeVerts[idx++].pos = verts[(k + 1) % face.nEdges].pos;
		}

		for (int w = 0; w < wireframeVertCount; w++)
		{
			if (modelIdx > 0)
			{
				wireframeVerts[w].c = COLOR4(0, 100, 255, 255);
			}
			else
			{
				wireframeVerts[w].c = COLOR4(30, 30, 30, 255);
			}
		}

		if (!noTriangulate)
		{
			idx = 0;
			// convert TRIANGLE_FAN verts to TRIANGLES so multiple faces can be drawn in a single draw call
			int newCount = face.nEdges + std::max(0, face.nEdges - 3) * 2;
			lightmapVert* newVerts = new lightmapVert[newCount];

			for (int k = 2; k < face.nEdges; k++)
			{
				newVerts[idx++] = verts[0];
				newVerts[idx++] = verts[k - 1];
				newVerts[idx++] = verts[k];
			}

			delete[] verts;
			verts = newVerts;
			vertCount = newCount;
		}


		// add face to a render group (faces that share that same textures and opacity flag)
		bool isTransparent = opacity < 1.0f || (tex && tex->szName[0] == '{');
		int groupIdx = -1;
		for (size_t k = 0; k < renderGroups.size(); k++)
		{
			if (texinfo.iMiptex == -1 || texinfo.iMiptex >= map->textureCount)
				continue;
			bool textureMatch = !texturesLoaded || renderGroups[k].texture == glTextures[texinfo.iMiptex];
			if (textureMatch && renderGroups[k].transparent == isTransparent)
			{
				bool allMatch = true;
				for (int s = 0; s < MAX_LIGHTMAPS; s++)
				{
					if (renderGroups[k].lightmapAtlas[s] != lightmapAtlas[s])
					{
						allMatch = false;
						break;
					}
				}
				if (allMatch)
				{
					groupIdx = (int)k;
					break;
				}
			}
		}

		// add the verts to a new group if no existing one share the same properties
		if (groupIdx == -1)
		{
			RenderGroup newGroup = RenderGroup();
			newGroup.transparent = isTransparent;
			newGroup.special = isSpecial;
			newGroup.texture = texturesLoaded && texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount ? glTextures[texinfo.iMiptex] : greyTex;
			for (int s = 0; s < MAX_LIGHTMAPS; s++)
			{
				newGroup.lightmapAtlas[s] = lightmapAtlas[s];
			}
			groupIdx = (int)renderGroups.size();
			renderGroups.push_back(newGroup);
			renderGroupVerts.emplace_back(std::vector<lightmapVert>());
		}

		renderModel->renderFaces[i].group = groupIdx;
		renderModel->renderFaces[i].vertOffset = (int)renderGroupVerts[groupIdx].size();
		renderModel->renderFaces[i].vertCount = vertCount;

		renderGroupVerts[groupIdx].insert(renderGroupVerts[groupIdx].end(), verts, verts + vertCount);
		wireframeVerts_full.insert(wireframeVerts_full.end(), wireframeVerts.begin(), wireframeVerts.end());

		delete[] verts;
	}

	renderModel->renderGroups = new RenderGroup[renderGroups.size()];
	renderModel->groupCount = (int)renderGroups.size();


	for (int i = 0; i < renderModel->groupCount; i++)
	{
		lightmapVert* result_verts = new lightmapVert[renderGroupVerts[i].size() + 1];
		if (renderGroupVerts[i].size() > 0)
			memcpy(result_verts, &renderGroupVerts[i][0], renderGroupVerts[i].size() * sizeof(lightmapVert));

		renderGroups[i].buffer = new VertexBuffer(g_app->bspShader, result_verts, renderGroupVerts[i].size(), GL_TRIANGLES);
		renderGroups[i].buffer->ownData = true;
		renderGroups[i].buffer->frameId = 0;
		renderModel->renderGroups[i] = renderGroups[i];
	}

	if (wireframeVerts_full.size())
	{
		std::vector<cVert> cleanupWireframe = removeDuplicateWireframeLines(wireframeVerts_full);

		if (g_verbose)
			print_log("Optimize wireframe {} model: {} to {} lines.\n", modelIdx, wireframeVerts_full.size(), cleanupWireframe.size());


		cVert* resultWireFrame = new cVert[cleanupWireframe.size()];
		memcpy(resultWireFrame, cleanupWireframe.data(), cleanupWireframe.size() * sizeof(cVert));

		renderModel->wireframeBuffer = new VertexBuffer(g_app->colorShader, resultWireFrame, cleanupWireframe.size(), GL_LINES);
		renderModel->wireframeBuffer->ownData = true;
		renderModel->wireframeBuffer->frameId = 0;
	}

	for (int i = 0; i < model.nFaces; i++)
	{
		refreshFace(model.iFirstFace + i);
	}

	if (refreshClipnodes)
		generateClipnodeBuffer(modelIdx);

	return renderModel->groupCount;
}

bool BspRenderer::refreshModelClipnodes(int modelIdx)
{
	if (!clipnodesLoaded)
	{
		return false;
	}
	if (modelIdx < 0 || modelIdx >= numRenderClipnodes)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0280));
		return false;
	}
	for (int hullIdx = 0; hullIdx < MAX_MAP_HULLS; hullIdx++)
	{
		int nodeIdx = map->models[modelIdx].iHeadnodes[hullIdx];
		if (hullIdx == 0 && clipnodesBufferCache.find(nodeIdx) != clipnodesBufferCache.end())
		{
			clipnodesBufferCache.erase(nodeIdx);
		}
		else if (hullIdx > 0 && nodesBufferCache.find(nodeIdx) != nodesBufferCache.end())
		{
			nodesBufferCache.erase(nodeIdx);
		}
	}

	deleteRenderModelClipnodes(&renderClipnodes[modelIdx]);
	generateClipnodeBuffer(modelIdx);
	return true;
}

void BspRenderer::loadClipnodes()
{
	if (!map)
		return;

	clipnodesBufferCache.clear();
	nodesBufferCache.clear();

	numRenderClipnodes = map->modelCount;
	renderClipnodes.resize(numRenderClipnodes);

	std::vector<int> tmpRenderHulls(MAX_MAP_HULLS);
	std::iota(tmpRenderHulls.begin(), tmpRenderHulls.end(), 0);

	// Using 4x threads instead of very big count
	std::for_each(std::execution::par_unseq, tmpRenderHulls.begin(), tmpRenderHulls.end(),
		[&](int hull)
		{
			for (int i = 0; i < numRenderClipnodes; i++)
			{
				generateClipnodeBufferForHull(i, hull);
			}
		}
	);
}

void BspRenderer::generateClipnodeBufferForHull(int modelIdx, int hullIdx)
{
	if (hullIdx < 0 || hullIdx > 3)
		return;

	BSPMODEL& model = map->models[modelIdx];
	Clipper clipper;

	vec3 min = vec3(model.nMins.x, model.nMins.y, model.nMins.z);
	vec3 max = vec3(model.nMaxs.x, model.nMaxs.y, model.nMaxs.z);

	if (modelIdx >= numRenderClipnodes)
	{
		addClipnodeModel(modelIdx);
	}

	RenderClipnodes& renderClip = renderClipnodes[modelIdx];

	if (renderClip.clipnodeBuffer[hullIdx])
	{
		delete renderClip.clipnodeBuffer[hullIdx];
		renderClip.clipnodeBuffer[hullIdx] = NULL;
	}

	if (renderClip.wireframeClipnodeBuffer[hullIdx])
	{
		delete renderClip.wireframeClipnodeBuffer[hullIdx];
		renderClip.wireframeClipnodeBuffer[hullIdx] = NULL;
	}

	renderClip.faceMaths[hullIdx].clear();

	int nodeIdx = map->models[modelIdx].iHeadnodes[hullIdx];

	nodeBuffStr oldHullIdxStruct = nodeBuffStr();
	oldHullIdxStruct.hullIdx = oldHullIdxStruct.modelIdx = -1;

	g_mutex_list[2].lock();
	if (hullIdx == 0 && clipnodesBufferCache.find(nodeIdx) != clipnodesBufferCache.end())
	{
		oldHullIdxStruct = clipnodesBufferCache[nodeIdx];
	}
	else if (hullIdx > 0 && nodesBufferCache.find(nodeIdx) != nodesBufferCache.end())
	{
		oldHullIdxStruct = nodesBufferCache[nodeIdx];
	}
	g_mutex_list[2].unlock();

	if (oldHullIdxStruct.modelIdx >= 0 && oldHullIdxStruct.hullIdx >= 0)
	{
		return;/* // Instead of cache.... Just do nothing.
		RenderClipnodes* cachedRenderClip = &renderClipnodes[oldHullIdxStruct.modelIdx];


		std::vector<FaceMath>& tfaceMaths = cachedRenderClip->faceMaths[oldHullIdxStruct.hullIdx];

		cVert* output = new cVert[cachedRenderClip->clipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts];
		memcpy(output, cachedRenderClip->clipnodeBuffer[oldHullIdxStruct.hullIdx]->data,
			cachedRenderClip->clipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts * sizeof(cVert));

		cVert* wireOutput = new cVert[cachedRenderClip->wireframeClipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts];
		memcpy(wireOutput, cachedRenderClip->wireframeClipnodeBuffer[oldHullIdxStruct.hullIdx]->data,
			cachedRenderClip->wireframeClipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts * sizeof(cVert));

		renderClip->clipnodeBuffer[hullIdx] = new VertexBuffer(colorShader, output,
			(GLsizei)cachedRenderClip->clipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts, GL_TRIANGLES);
		renderClip->clipnodeBuffer[hullIdx]->ownData = true;

		renderClip->wireframeClipnodeBuffer[hullIdx] = new VertexBuffer(colorShader,  wireOutput,
			(GLsizei)cachedRenderClip->wireframeClipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts, GL_LINES);
		renderClip->wireframeClipnodeBuffer[hullIdx]->ownData = true;

		renderClip->faceMaths[hullIdx] = tfaceMaths;
		return;*/
	}


	std::vector<NodeVolumeCuts> solidNodes = map->get_model_leaf_volume_cuts(modelIdx, hullIdx);
	//print_log(get_localized_string(LANG_0281),solidNodes.size(),modelIdx,hullIdx);
	std::vector<CMesh> meshes;
	for (size_t k = 0; k < solidNodes.size(); k++)
	{
		meshes.emplace_back(clipper.clip(solidNodes[k].cuts));
		clipnodeLeafCount++;
	}

	static COLOR4 hullColors[] = {
		COLOR4(255, 255, 255, 128),
		COLOR4(96, 255, 255, 128),
		COLOR4(255, 96, 255, 128),
		COLOR4(255, 255, 96, 128),
	};
	COLOR4 color = hullColors[hullIdx];

	std::vector<cVert> allVerts;
	std::vector<cVert> wireframeVerts;
	std::vector<FaceMath>& tfaceMaths = renderClip.faceMaths[hullIdx];
	tfaceMaths.clear();

	for (size_t m = 0; m < meshes.size(); m++)
	{
		CMesh& mesh = meshes[m];

		for (size_t n = 0; n < mesh.faces.size(); n++)
		{
			if (!mesh.faces[n].visible)
			{
				continue;
			}
			std::set<int> uniqueFaceVerts;

			for (size_t k = 0; k < mesh.faces[n].edges.size(); k++)
			{
				for (int v = 0; v < 2; v++)
				{
					int vertIdx = mesh.edges[mesh.faces[n].edges[k]].verts[v];
					if (!mesh.verts[vertIdx].visible || uniqueFaceVerts.count(vertIdx))
					{
						continue;
					}
					uniqueFaceVerts.insert(vertIdx);
				}
			}

			std::vector<vec3> faceVerts;
			for (auto vertIdx : uniqueFaceVerts)
			{
				faceVerts.push_back(mesh.verts[vertIdx].pos);
			}

			if (faceVerts.size() < 1)
			{
				// print_log(get_localized_string(LANG_0282));
				continue;
			}

			faceVerts = getSortedPlanarVerts(faceVerts);

			if (faceVerts.size() < 3)
			{
				// print_log(get_localized_string(LANG_1046));
				continue;
			}

			vec3 normal = getNormalFromVerts(faceVerts);


			if (dotProduct(mesh.faces[n].normal, normal) > 0.0f)
			{
				reverse(faceVerts.begin(), faceVerts.end());
				normal = normal.invert();
			}

			// calculations for face picking
			FaceMath faceMath;
			faceMath.normal = mesh.faces[n].normal;
			faceMath.fdist = getDistAlongAxis(mesh.faces[n].normal, faceVerts[0]);

			vec3 v0 = faceVerts[0];
			vec3 v1;
			bool found = false;
			for (size_t c = 1; c < faceVerts.size(); c++)
			{
				if (faceVerts[c] != v0)
				{
					v1 = faceVerts[c];
					found = true;
					break;
				}
			}
			if (!found)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0283));
			}

			vec3 plane_z = mesh.faces[n].normal;
			vec3 plane_x = (v1 - v0).normalize();
			vec3 plane_y = crossProduct(plane_z, plane_x).normalize();

			faceMath.worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

			faceMath.localVerts = std::vector<vec2>(faceVerts.size());
			for (size_t k = 0; k < faceVerts.size(); k++)
			{
				faceMath.localVerts[k] = (faceMath.worldToLocal * vec4(faceVerts[k], 1)).xy();
			}

			tfaceMaths.push_back(faceMath);
			// create the verts for rendering
			for (size_t c = 0; c < faceVerts.size(); c++)
			{
				faceVerts[c] = faceVerts[c].flip();
			}

			COLOR4 wireframeColor = { 0, 0, 0, 255 };
			for (size_t k = 0; k < faceVerts.size(); k++)
			{
				wireframeVerts.emplace_back(cVert(faceVerts[k], wireframeColor));
				wireframeVerts.emplace_back(cVert(faceVerts[(k + 1) % faceVerts.size()], wireframeColor));
			}

			vec3 lightDir = vec3(1.0f, 1.0f, -1.0f).normalize();
			float dot = (dotProduct(normal, lightDir) + 1) / 2.0f;
			if (dot > 0.5f)
			{
				dot = dot * dot;
			}

			COLOR4 faceColor = color * (dot);

			// convert from TRIANGLE_FAN style verts to TRIANGLES
			for (size_t k = 2; k < faceVerts.size(); k++)
			{
				allVerts.emplace_back(cVert(faceVerts[0], faceColor));
				allVerts.emplace_back(cVert(faceVerts[k - 1], faceColor));
				allVerts.emplace_back(cVert(faceVerts[k], faceColor));
			}

		}
	}

	if (allVerts.empty() || wireframeVerts.empty())
	{
		return;
	}

	if (modelIdx > 0 && hullIdx == 0)
	{
		allVerts = stretch_model(allVerts, 0.1f);
		wireframeVerts = stretch_model(wireframeVerts, 0.1f);
	}

	cVert* output = new cVert[allVerts.size()];
	if (allVerts.size())
		std::copy(allVerts.begin(), allVerts.end(), output);

	cVert* wireOutput = new cVert[wireframeVerts.size()];
	if (wireframeVerts.size())
		std::copy(wireframeVerts.begin(), wireframeVerts.end(), wireOutput);

	renderClip.clipnodeBuffer[hullIdx] = new VertexBuffer(g_app->colorShader, output, allVerts.size(), GL_TRIANGLES);
	renderClip.clipnodeBuffer[hullIdx]->ownData = true;
	renderClip.clipnodeBuffer[hullIdx]->frameId = 0;

	renderClip.wireframeClipnodeBuffer[hullIdx] = new VertexBuffer(g_app->colorShader, wireOutput, wireframeVerts.size(), GL_LINES);
	renderClip.wireframeClipnodeBuffer[hullIdx]->ownData = true;
	renderClip.wireframeClipnodeBuffer[hullIdx]->frameId = 0;

	nodeBuffStr curHullIdxStruct = nodeBuffStr();
	curHullIdxStruct.hullIdx = hullIdx;
	curHullIdxStruct.modelIdx = modelIdx;

	if (hullIdx == 0)
	{
		clipnodesBufferCache[nodeIdx] = curHullIdxStruct;
	}
	else
	{
		nodesBufferCache[nodeIdx] = curHullIdxStruct;
	}
}

void BspRenderer::generateClipnodeBuffer(int modelIdx)
{
	if (!map || modelIdx < 0)
		return;

	for (int hullIdx = 0; hullIdx < MAX_MAP_HULLS; hullIdx++)
	{
		int nodeIdx = map->models[modelIdx].iHeadnodes[hullIdx];
		if (hullIdx == 0 && clipnodesBufferCache.find(nodeIdx) != clipnodesBufferCache.end())
		{
			clipnodesBufferCache.erase(nodeIdx);
		}
		else if (hullIdx > 0 && nodesBufferCache.find(nodeIdx) != nodesBufferCache.end())
		{
			nodesBufferCache.erase(nodeIdx);
		}
	}

	for (int i = 0; i < MAX_MAP_HULLS; i++)
	{
		generateClipnodeBufferForHull(modelIdx, i);
	}
}

void BspRenderer::updateClipnodeOpacity(unsigned char newValue)
{
	if (!renderClipnodes.size())
		return;
	for (int i = 0; i < numRenderClipnodes; i++)
	{
		for (int k = 0; k < MAX_MAP_HULLS; k++)
		{
			VertexBuffer* clipBuf = renderClipnodes[i].clipnodeBuffer[k];
			if (clipBuf && clipBuf->get_data() && clipBuf->numVerts > 0)
			{
				cVert* vertData = (cVert*)clipBuf->get_data();
				for (int v = 0; v < clipBuf->numVerts; v++)
				{
					vertData[v].c.a = newValue;
				}
				renderClipnodes[i].clipnodeBuffer[k]->uploaded = false;
			}
		}
	}
}

void BspRenderer::preRenderEnts()
{
	renderEnts.resize(map->ents.size());

	numPointEnts = 0;

	for (size_t i = 1; i < map->ents.size(); i++)
	{
		numPointEnts += !map->ents[i]->isBspModel();
	}

	for (size_t i = 0; i < map->ents.size(); i++)
	{
		refreshEnt((int)i);
	}
}

void BspRenderer::refreshPointEnt(size_t entIdx)
{
	//int skipIdx = 0;

	//if (entIdx == 0)
	//	return;

	//// skip worldspawn
	//for (size_t i = 1, sz = map->ents.size(); i < sz; i++)
	//{
	//	if (renderEnts[i].modelIdx >= 0)
	//		continue;

	//	if ((int)i == entIdx)
	//	{
	//		break;
	//	}

	//	skipIdx++;
	//}

	//if (skipIdx >= numPointEnts)
	//{
	//	print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0284));
	//	return;
	//}
}

void BspRenderer::setRenderAngles(size_t entIdx, vec3 angles)
{
	if (!map->ents[entIdx]->hasKey("classname"))
	{
		renderEnts[entIdx].modelMat4x4_angles.rotateY((angles.y * (HL_PI / 180.0f)));
		renderEnts[entIdx].modelMat4x4_angles.rotateZ(-(angles.x * (HL_PI / 180.0f)));
		renderEnts[entIdx].modelMat4x4_angles.rotateX((angles.z * (HL_PI / 180.0f)));
		renderEnts[entIdx].needAngles = false;
	}
	else
	{
		std::string entClassName = map->ents[entIdx]->keyvalues["classname"];
		// based at cs 1.6 gamedll
		if (entClassName == "func_breakable")
		{
			renderEnts[entIdx].angles.y = 0.0f;
			renderEnts[entIdx].modelMat4x4_angles.rotateY(0.0f);
			renderEnts[entIdx].modelMat4x4_angles.rotateZ(-(angles.x * (HL_PI / 180.0f)));
			renderEnts[entIdx].modelMat4x4_angles.rotateX((angles.z * (HL_PI / 180.0f)));
		}
		else if (IsEntNotSupportAngles(entClassName))
		{
			renderEnts[entIdx].angles = vec3();
		}
		else if (entClassName == "env_sprite")
		{
			if (abs(angles.y) >= EPSILON && abs(angles.z) < EPSILON)
			{
				renderEnts[entIdx].angles.z = 0.0f;
				renderEnts[entIdx].modelMat4x4_angles.rotateY(0.0);
				renderEnts[entIdx].modelMat4x4_angles.rotateZ(-(angles.x * (HL_PI / 180.0f)));
				renderEnts[entIdx].modelMat4x4_angles.rotateX((angles.y * (HL_PI / 180.0f)));
			}
			else
			{
				renderEnts[entIdx].modelMat4x4_angles.rotateY((angles.y * (HL_PI / 180.0f)));
				renderEnts[entIdx].modelMat4x4_angles.rotateZ(-(angles.x * (HL_PI / 180.0f)));
				renderEnts[entIdx].modelMat4x4_angles.rotateX((angles.z * (HL_PI / 180.0f)));
			}
		}
		else
		{
			bool foundAngles = false;
			for (const auto& prefix : g_settings.entsNegativePitchPrefix)
			{
				if (entClassName.starts_with(prefix))
				{
					renderEnts[entIdx].modelMat4x4_angles.rotateY((angles.y * (HL_PI / 180.0f)));
					renderEnts[entIdx].modelMat4x4_angles.rotateZ((angles.x * (HL_PI / 180.0f)));
					renderEnts[entIdx].modelMat4x4_angles.rotateX((angles.z * (HL_PI / 180.0f)));
					foundAngles = true;
					break;
				}
			}
			if (!foundAngles)
			{
				renderEnts[entIdx].modelMat4x4_angles.rotateY((angles.y * (HL_PI / 180.0f)));
				renderEnts[entIdx].modelMat4x4_angles.rotateZ(-(angles.x * (HL_PI / 180.0f)));
				renderEnts[entIdx].modelMat4x4_angles.rotateX((angles.z * (HL_PI / 180.0f)));
			}
		}
	}

	if (renderEnts[entIdx].angles != vec3())
	{
		renderEnts[entIdx].needAngles = true;
	}
}

void BspRenderer::refreshEnt(size_t entIdx)
{
	if (entIdx < 0 || !g_app->pointEntRenderer)
		return;
	int skin = -1;
	int sequence = -1;
	int body = -1;

	Entity* ent = map->ents[entIdx];

	BSPMODEL mdl = map->models[ent->getBspModelIdx() > 0 ? ent->getBspModelIdx() : 0];
	renderEnts[entIdx].modelIdx = ent->getBspModelIdx();
	renderEnts[entIdx].isDuplicateModel = false;

	if (renderEnts[entIdx].modelIdx >= 0)
	{
		for (size_t i = 0; i < map->ents.size(); i++)
		{
			if (i != entIdx)
			{
				if (map->ents[i]->getBspModelIdx() == renderEnts[entIdx].modelIdx)
				{
					renderEnts[entIdx].isDuplicateModel = true;
					break;
				}
			}
		}
	}

	renderEnts[entIdx].offset = vec3();
	renderEnts[entIdx].angles = vec3();
	renderEnts[entIdx].needAngles = false;
	renderEnts[entIdx].pointEntCube = g_app->pointEntRenderer->getEntCube(ent);
	bool setAngles = false;

	vec3 origin = ent->origin;
	renderEnts[entIdx].modelMat4x4.loadIdentity();
	renderEnts[entIdx].modelMat4x4.translate(origin.x, origin.z, -origin.y);
	renderEnts[entIdx].modelMat4x4_angles.loadIdentity();
	renderEnts[entIdx].modelMat4x4_angles.translate(origin.x, origin.z, -origin.y);
	renderEnts[entIdx].offset = origin;

	for (unsigned int i = 0; i < ent->keyOrder.size(); i++)
	{
		if (ent->keyOrder[i] == "angles")
		{
			setAngles = true;
			renderEnts[entIdx].angles = parseVector(ent->keyvalues["angles"]);
		}
		if (ent->keyOrder[i] == "angle")
		{
			setAngles = true;
			float y = (float)atof(ent->keyvalues["angle"].c_str());

			if (y >= 0.0f)
			{
				renderEnts[entIdx].angles.y = y;
			}
			else if (y == -1.0f)
			{
				renderEnts[entIdx].angles.x = -90.0f;
				renderEnts[entIdx].angles.y = 0.0f;
				renderEnts[entIdx].angles.z = 0.0f;
			}
			else if (y <= -2.0f)
			{
				renderEnts[entIdx].angles.x = 90.0f;
				renderEnts[entIdx].angles.y = 0.0f;
				renderEnts[entIdx].angles.z = 0.0f;
			}
		}
		if (ent->classname.size() && ent->classname.find("light") != std::string::npos && ent->keyOrder[i] == "pitch")
		{
			setAngles = true;
			float x = (float)atof(ent->keyvalues["pitch"].c_str());
			renderEnts[entIdx].angles.x = -x;
		}
	}

	if (ent->hasKey("sequence") || g_app->fgd)
	{
		if (ent->hasKey("sequence") && isNumeric(ent->keyvalues["sequence"]))
		{
			sequence = atoi(ent->keyvalues["sequence"].c_str());
		}
		if (sequence <= 0 && g_app->fgd)
		{
			FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->keyvalues["classname"]);
			if (fgdClass)
			{
				sequence = fgdClass->modelSequence;
			}
		}
	}

	if (ent->hasKey("skin") || g_app->fgd)
	{
		if (ent->hasKey("skin") && isNumeric(ent->keyvalues["skin"]))
		{
			skin = atoi(ent->keyvalues["skin"].c_str());
		}
		if (skin <= 0 && g_app->fgd)
		{
			FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->keyvalues["classname"]);
			if (fgdClass)
			{
				skin = fgdClass->modelSkin;
			}
		}
	}

	if (ent->hasKey("body") || g_app->fgd)
	{
		if (ent->hasKey("body") && isNumeric(ent->keyvalues["body"]))
		{
			body = atoi(ent->keyvalues["body"].c_str());
		}
		if (body == 0 && g_app->fgd)
		{
			FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->keyvalues["classname"]);
			if (fgdClass)
			{
				body = fgdClass->modelBody;
			}
		}
	}



	if (!ent->isBspModel())
	{
		if (ent->hasKey("model"))
		{
			std::string modelpath = std::string();

			if (ent->hasKey("model") && ent->keyvalues["model"].size())
			{
				modelpath = ent->keyvalues["model"];
			}

			if (g_app->fgd && modelpath.empty())
			{
				FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->keyvalues["classname"]);
				if (fgdClass && !fgdClass->model.empty())
				{
					modelpath = fgdClass->model;
				}
			}

			if (renderEnts[entIdx].mdlFileName.size() && !modelpath.size() || renderEnts[entIdx].mdlFileName != modelpath)
			{
				renderEnts[entIdx].mdlFileName = modelpath;
				std::string lowerpath = toLowerCase(modelpath);
				std::string newModelPath;
				if (lowerpath.ends_with(".mdl"))
				{
					if (FindPathInAssets(map, modelpath, newModelPath))
					{
						renderEnts[entIdx].mdl = AddNewModelToRender(newModelPath, body + sequence * 100 + skin * 1000);
						renderEnts[entIdx].mdl->UpdateModelMeshList();
					}
					else
					{
						FindPathInAssets(map, modelpath, newModelPath, true);
						renderEnts[entIdx].mdl = NULL;
					}
				}
				else
				{
					renderEnts[entIdx].mdl = NULL;
					if (lowerpath.ends_with(".spr"))
					{
						if (FindPathInAssets(map, modelpath, newModelPath))
						{
							renderEnts[entIdx].spr = AddNewSpriteToRender(newModelPath);
						}
						else
						{
							FindPathInAssets(map, modelpath, newModelPath, true);
							renderEnts[entIdx].spr = NULL;
						}
					}
					else
					{
						renderEnts[entIdx].spr = NULL;
					}
				}
			}
		}
		else if (g_app->fgd)
		{
			FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->keyvalues["classname"]);
			if (fgdClass && fgdClass->isSprite && fgdClass->sprite.size())
			{
				renderEnts[entIdx].spr = NULL;

				std::string lowerpath = toLowerCase(fgdClass->sprite);
				std::string newModelPath;
				if (lowerpath.ends_with(".mdl"))
				{
					if (FindPathInAssets(map, fgdClass->sprite, newModelPath))
					{
						renderEnts[entIdx].mdl = AddNewModelToRender(newModelPath, body + sequence * 100 + skin * 1000);
						renderEnts[entIdx].mdl->UpdateModelMeshList();
					}
					else
					{
						FindPathInAssets(map, fgdClass->sprite, newModelPath, true);
						renderEnts[entIdx].mdl = NULL;
					}
				}
				else
				{
					renderEnts[entIdx].mdl = NULL;
					if (lowerpath.ends_with(".spr"))
					{
						if (FindPathInAssets(map, fgdClass->sprite, newModelPath))
						{
							renderEnts[entIdx].spr = AddNewSpriteToRender(newModelPath);
						}
						else
						{
							FindPathInAssets(map, fgdClass->sprite, newModelPath, true);
							renderEnts[entIdx].spr = NULL;
						}
					}
					else
					{
						renderEnts[entIdx].spr = NULL;
					}
				}
			}
			else
			{
				fgdClass = g_app->fgd->getFgdClass(ent->keyvalues["classname"]);
				if (fgdClass && !fgdClass->model.empty())
				{
					std::string lowerpath = toLowerCase(fgdClass->model);
					std::string newModelPath;
					if (lowerpath.ends_with(".mdl"))
					{
						if (FindPathInAssets(map, fgdClass->model, newModelPath))
						{
							renderEnts[entIdx].mdl = AddNewModelToRender(newModelPath, body + sequence * 100 + skin * 1000);
							renderEnts[entIdx].mdl->UpdateModelMeshList();
						}
						else
						{
							FindPathInAssets(map, fgdClass->model, newModelPath, true);
							renderEnts[entIdx].mdl = NULL;
						}
					}
					else
					{
						renderEnts[entIdx].mdl = NULL;
						if (lowerpath.ends_with(".spr"))
						{
							if (FindPathInAssets(map, fgdClass->model, newModelPath))
							{
								renderEnts[entIdx].spr = AddNewSpriteToRender(newModelPath);
							}
							else
							{
								FindPathInAssets(map, fgdClass->model, newModelPath, true);
								renderEnts[entIdx].spr = NULL;
							}
						}
						else
						{
							renderEnts[entIdx].spr = NULL;
						}
					}
				}
			}
		}

	}

	if (skin != -1)
	{
		if (renderEnts[entIdx].mdl)
		{
			renderEnts[entIdx].mdl->SetSkin(skin);
		}
	}
	if (body != -1)
	{
		if (renderEnts[entIdx].mdl && renderEnts[entIdx].mdl->m_pstudiohdr)
		{
			auto* pbodypart = (mstudiobodyparts_t*)((unsigned char*)renderEnts[entIdx].mdl->m_pstudiohdr + renderEnts[entIdx].mdl->m_pstudiohdr->bodypartindex);
			for (int bg = 0; bg < renderEnts[entIdx].mdl->m_pstudiohdr->numbodyparts; bg++)
			{
				renderEnts[entIdx].mdl->SetBodygroup(bg, body % pbodypart->nummodels);
				body /= pbodypart->nummodels;
				pbodypart++;
			}
		}
	}
	if (sequence != -1)
	{
		if (renderEnts[entIdx].mdl)
		{
			renderEnts[entIdx].mdl->SetSequence(sequence);
		}
	}
	if (setAngles)
	{
		setRenderAngles(entIdx, renderEnts[entIdx].angles);
	}
}

void BspRenderer::calcFaceMaths()
{
	deleteFaceMaths();

	numFaceMaths = map->faceCount;
	faceMaths = new FaceMath[map->faceCount];

	//vec3 world_x = vec3(1.0f, 0.0f, 0.0f);
	//vec3 world_y = vec3(0.0f, 1.0f, 0.0f);
	//vec3 world_z = vec3(0.0f, 0.0f, 1.0f);

	for (int i = 0; i < map->faceCount; i++)
	{
		refreshFace(i);
	}
}

void BspRenderer::refreshFace(int faceIdx)
{
	if (faceIdx >= numFaceMaths)
	{
		return;
		/*FaceMath* tmpfaceMaths = new FaceMath[faceIdx + 1]{};
		memcpy(tmpfaceMaths, faceMaths, faceIdx * sizeof(FaceMath));
		delete[] faceMaths;
		faceMaths = tmpfaceMaths;
		numFaceMaths = faceIdx;*/
	}

	const vec3 world_x = vec3(1.0f, 0.0f, 0.0f);
	const vec3 world_y = vec3(0.0f, 1.0f, 0.0f);
	const vec3 world_z = vec3(0.0f, 0.0f, 1.0f);

	FaceMath& faceMath = faceMaths[faceIdx];
	BSPFACE32& face = map->faces[faceIdx];
	BSPPLANE& plane = map->planes[face.iPlane];
	vec3 planeNormal = face.nPlaneSide ? plane.vNormal * -1 : plane.vNormal;
	float fDist = face.nPlaneSide ? -plane.fDist : plane.fDist;

	faceMath.normal = planeNormal;
	faceMath.fdist = fDist;

	std::vector<vec3> allVerts(face.nEdges);
	vec3 v1;
	for (int e = 0; e < face.nEdges; e++)
	{
		int edgeIdx = map->surfedges[face.iFirstEdge + e];
		BSPEDGE32& edge = map->edges[abs(edgeIdx)];
		int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];
		allVerts[e] = map->verts[vertIdx];

		// 2 verts can share the same position on a face, so need to find one that isn't shared (aomdc_1intro)
		if (e > 0 && allVerts[e] != allVerts[0])
		{
			v1 = allVerts[e];
		}
	}
	if (allVerts.size() == 0)
	{
		allVerts.emplace_back(vec3());
	}
	vec3 plane_x = (v1 - allVerts[0]).normalize(1.0f);
	vec3 plane_y = crossProduct(planeNormal, plane_x).normalize(1.0f);
	vec3 plane_z = planeNormal;

	faceMath.worldToLocal = worldToLocalTransform(plane_x, plane_y, plane_z);

	faceMath.localVerts = std::vector<vec2>(allVerts.size());
	for (size_t i = 0; i < allVerts.size(); i++)
	{
		faceMath.localVerts[i] = (faceMath.worldToLocal * vec4(allVerts[i], 1.0f)).xy();
	}
}

BspRenderer::~BspRenderer()
{
	clearUndoCommands();
	clearRedoCommands();

	if (lightmapFuture.valid() && lightmapFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready ||
		texturesFuture.valid() && texturesFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready ||
		clipnodesFuture.valid() && clipnodesFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
	{
		print_log(get_localized_string(LANG_0285));
	}

	for (size_t i = 0; i < wads.size(); i++)
	{
		delete wads[i];
	}
	wads.clear();

	if (lightmaps)
	{
		delete[] lightmaps;
	}

	if (renderEnts.size())
	{
		renderEnts.clear();
	}

	delete leafCube;
	leafCube = NULL;
	delete nodeCube;
	nodeCube = NULL;/*
	delete nodePlaneCube;
	nodePlaneCube = NULL;*/

	deleteTextures();
	deleteLightmapTextures();
	deleteRenderFaces();
	deleteRenderClipnodes();
	deleteFaceMaths();

	clipnodesBufferCache.clear();
	nodesBufferCache.clear();

	if (g_app->SelectedMap == map)
		g_app->selectMap(NULL);
	map->setBspRender(NULL);

	delete map;
	map = NULL;
}

void BspRenderer::reuploadTextures()
{
	if (!glTexturesSwap)
		return;

	deleteTextures();

	//loadTextures();

	glTextures = glTexturesSwap;

	for (int i = 0; i < map->textureCount; i++)
	{
		glTextures[i]->upload();
	}

	numLoadedTextures = map->textureCount;

	texturesLoaded = true;

	preRenderFaces();

	needReloadDebugTextures = true;
}

void BspRenderer::delayLoadData()
{
	if (!lightmapsUploaded && lightmapFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		for (size_t i = 0; i < numLightmapAtlases; i++)
		{
			if (glLightmapTextures[i])
				glLightmapTextures[i]->upload();
		}

		preRenderFaces();

		lightmapsUploaded = true;
	}

	if (!texturesLoaded && texturesFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		reuploadTextures();
	}

	if (!clipnodesLoaded && clipnodesFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		if (renderClipnodes.size())
		{
			for (int i = 0; i < numRenderClipnodes; i++)
			{
				RenderClipnodes& clip = renderClipnodes[i];
				for (int k = 0; k < MAX_MAP_HULLS; k++)
				{
					if (clip.clipnodeBuffer[k])
					{
						clip.clipnodeBuffer[k]->uploaded = false;
					}
				}
			}
		}

		clipnodesLoaded = true;
		print_log(get_localized_string(LANG_0286), clipnodeLeafCount);
		updateClipnodeOpacity((g_render_flags & RENDER_TRANSPARENT) ? 128 : 255);
	}
}

bool BspRenderer::isFinishedLoading()
{
	return lightmapsUploaded && texturesLoaded && clipnodesLoaded;
}

void BspRenderer::highlightFace(size_t faceIdx, int highlight, bool reupload)
{
	RenderFace* rface;
	RenderGroup* rgroup;
	if (!getRenderPointers((int)faceIdx, &rface, &rgroup))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1047));
		return;
	}
	float r, g, b;
	r = g = b = 0.0f;

	if (highlight == 1)
	{
		r = rgroup->special ? 2.0f : 0.86f;
		g = 0.0f;
		b = 0.0f;
	}

	if (highlight == 2)
	{
		r = rgroup->special ? 3.0f : 0.0f;
		g = 0.0f;
		b = 0.86f;
	}

	if (highlight == 3)
	{
		r = rgroup->special ? 4.0f : 0.0f;
		g = 0.2f;
		b = 0.2f;
	}

	auto verts = ((lightmapVert*)rgroup->buffer->get_data());

	for (int i = 0; i < rface->vertCount; i++)
	{
		verts[rface->vertOffset + i].r = r;
		verts[rface->vertOffset + i].g = g;
		verts[rface->vertOffset + i].b = b;
	}
	if (reupload)
		rgroup->buffer->uploaded = false;
}

void BspRenderer::updateFaceUVs(int faceIdx)
{
	RenderFace* rface;
	RenderGroup* rgroup;
	if (!getRenderPointers(faceIdx, &rface, &rgroup))
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1148));
		return;
	}

	BSPFACE32& face = map->faces[faceIdx];
	BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
	if (texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount)
	{
		int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
		if (texOffset >= 0)
		{
			BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

			auto verts = ((lightmapVert*)rgroup->buffer->get_data());

			for (int i = 0; i < rface->vertCount; i++)
			{
				lightmapVert& vert = verts[rface->vertOffset + i];
				vec3 pos = vert.pos.flipUV();

				float tw = 1.0f / (float)tex.nWidth;
				float th = 1.0f / (float)tex.nHeight;
				float fU = dotProduct(texinfo.vS, pos) + texinfo.shiftS;
				float fV = dotProduct(texinfo.vT, pos) + texinfo.shiftT;
				vert.u = fU * tw;
				vert.v = fV * th;
			}
			rgroup->buffer->uploaded = false;
		}
	}

	rgroup->buffer->uploaded = false;
}

bool BspRenderer::getRenderPointers(int faceIdx, RenderFace** renderFace, RenderGroup** renderGroup)
{
	int modelIdx = map->get_model_from_face(faceIdx);

	if (modelIdx == -1)
	{
		return false;
	}

	int relativeFaceIdx = faceIdx - map->models[modelIdx].iFirstFace;
	*renderFace = &renderModels[modelIdx].renderFaces[relativeFaceIdx];
	*renderGroup = &renderModels[modelIdx].renderGroups[(*renderFace)->group];

	return true;
}

unsigned int BspRenderer::getFaceTextureId(int faceIdx)
{
	BSPFACE32& face = map->faces[faceIdx];
	BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
	if (texinfo.iMiptex < 0 || texinfo.iMiptex >= map->textureCount)
		return missingTex->id;
	return glTextures[texinfo.iMiptex]->id;
}

void BspRenderer::render(bool modelVertsDraw, int clipnodeHull)
{
	mapOffset = map->ents.size() ? map->ents[0]->origin : vec3();
	renderOffset = mapOffset.flip();
	localCameraOrigin = cameraOrigin - mapOffset;

	if (delayEntUndoList.size())
	{
		for (const auto& undoEnt : delayEntUndoList)
		{
			if (undoEnt.entIdx >= 0 && undoEnt.entIdx < (int)map->ents.size() && undoEnt.ent == map->ents[undoEnt.entIdx])
			{
				pushEntityUndoState(undoEnt.description, undoEnt.entIdx);
			}
		}
		delayEntUndoList.clear();
	}

	g_app->matmodel.loadIdentity();
	g_app->matmodel.translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_app->colorShader->updateMatrixes();

	g_app->matmodel.loadIdentity();
	g_app->matmodel.translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_app->bspShader->updateMatrixes();

	static double leafUpdTime = 0.0;

	if (fabs(g_app->curTime - leafUpdTime) > 0.25)
	{
		leafUpdTime = g_app->curTime;
		std::vector<int> nodeBranch;
		int childIdx = -1;
		int headNode = map->models[0].iHeadnodes[0];
		map->pointContents(headNode, localCameraOrigin, 0, nodeBranch, curLeafIdx, childIdx);
		if (curLeafIdx < 0)
			curLeafIdx = 0;

		if (g_app->pickMode == PICK_FACE_LEAF)
		{
			if (!g_app->gui->showFaceEditWidget)
			{
				BSPLEAF32& tmpLeaf = map->leaves[curLeafIdx];

				leafCube->mins = tmpLeaf.nMins;
				leafCube->maxs = tmpLeaf.nMaxs;

				g_app->pointEntRenderer->genCubeBuffers(leafCube);
				std::vector<int> leafNodes;
				map->get_leaf_nodes(curLeafIdx, leafNodes);

				if (leafNodes.size())
				{
					BSPNODE32 node = map->nodes[leafNodes[0]];

					nodeCube->mins = node.nMins;
					nodeCube->maxs = node.nMaxs;

					g_app->pointEntRenderer->genCubeBuffers(nodeCube);/*

					BSPPLANE plane = map->planes[node.iPlane];

					nodePlaneCube->mins = { -32,-32,-32 };
					nodePlaneCube->maxs = { 32,32,32 };
					nodePlaneCube->mins += plane.vNormal;
					g_app->pointEntRenderer->genCubeBuffers(nodePlaneCube);*/
				}
			}
		}
	}

	bool need_refresh_mat = true;

	/*if ((old_rend_offs - renderOffset).length() > 0.01)
	{
		need_refresh_mat = true;
		old_rend_offs = renderOffset;
	}*/

	if (need_refresh_mat)
	{
		for (size_t i = 0, sz = map->ents.size(); i < sz; i++)
		{
			RenderEnt& ent = renderEnts[i];
			ent.modelMat4x4_calc = ent.modelMat4x4;
			ent.modelMat4x4_calc.translate(renderOffset.x, renderOffset.y, renderOffset.z);
			ent.modelMat4x4_calc_angles = ent.modelMat4x4_angles;
			ent.modelMat4x4_calc_angles.translate(renderOffset.x, renderOffset.y, renderOffset.z);
		}
	}

	std::vector<size_t> highlightEnts = g_app->pickInfo.selectedEnts;


	if (g_render_flags & RENDER_POINT_ENTS)
	{
		drawPointEntities(highlightEnts, REND_PASS_COLORSHADER);
		drawPointEntities(highlightEnts, REND_PASS_MODELSHADER);
	}

	for (int pass = 0; pass <= 2; pass++)
	{
		if (pass != REND_PASS_MODELSHADER)
		{
			g_app->bspShader->bind();
			g_app->bspShader->updateMatrixes();

			if (!map->ents[0]->hide)
				drawModel(0, pass, false, false);

			for (size_t i = 0, sz = map->ents.size(); i < sz; i++)
			{
				if (map->ents[i]->hide)
					continue;
				if (g_app->pickInfo.IsSelectedEnt(i))
				{
					/*if (g_render_flags & RENDER_SELECTED_AT_TOP)
						glDepthFunc(GL_ALWAYS);
					if (renderEnts[i].modelIdx >= 0 && renderEnts[i].modelIdx < map->modelCount)
					{
						g_app->bspShader->pushMatrix();
						g_app->matmodel = renderEnts[i].modelMat4x4_calc;
						g_app->bspShader->updateMatrixes();

						drawModel(&renderEnts[i], pass, true, false);
						g_app->bspShader->popMatrix();
					}
					if (g_render_flags & RENDER_SELECTED_AT_TOP)
						glDepthFunc(GL_LESS);*/
				}
				else
				{
					if (renderEnts[i].modelIdx >= 0 && renderEnts[i].modelIdx < map->modelCount)
					{
						g_app->bspShader->pushMatrix();
						drawModel(&renderEnts[i], pass, false, false);
						g_app->bspShader->popMatrix();
					}
				}
			}
		}
	}

	if (clipnodesLoaded)
	{
		if (g_render_flags & RENDER_WORLD_CLIPNODES && clipnodeHull != -1)
		{
			if (!map->ents[0]->hide)
			{
				g_app->colorShader->bind();
				g_app->colorShader->pushMatrix();
				g_app->matmodel.loadIdentity();
				g_app->matmodel.translate(renderOffset.x, renderOffset.y, renderOffset.z);
				g_app->colorShader->updateMatrixes();
				drawModelClipnodes(0, false, clipnodeHull);
				g_app->colorShader->popMatrix();
			}
		}

		if (g_render_flags & RENDER_ENT_CLIPNODES)
		{
			for (int i = 0, sz = (int)map->ents.size(); i < sz; i++)
			{
				if (map->ents[i]->hide)
					continue;
				if (renderEnts[i].modelIdx > 0 && renderEnts[i].modelIdx < map->modelCount)
				{
					if (clipnodeHull == -1 && renderModels[renderEnts[i].modelIdx].groupCount > 0)
					{
						continue; // skip rendering for models that have faces, if in auto mode
					}
					g_app->colorShader->bind();
					g_app->colorShader->pushMatrix();
					g_app->matmodel = renderEnts[i].modelMat4x4_calc_angles;
					g_app->colorShader->updateMatrixes();

					bool hightlighted = g_app->pickInfo.IsSelectedEnt(i);

					if (hightlighted)
					{
						glUniform4f(g_app->colorShaderMultId, 0.5f, 0.2f, 0.2f, 0.0f);
					}

					drawModelClipnodes(renderEnts[i].modelIdx, false, clipnodeHull);

					if (hightlighted)
					{
						glUniform4f(g_app->colorShaderMultId, 0.0f, 0.0f, 0.0f, 0.0f);
					}

					g_app->colorShader->popMatrix();
				}
			}
		}
	}

	if (highlightEnts.size() && map == g_app->SelectedMap)
	{
		if (g_render_flags & RENDER_SELECTED_AT_TOP && !modelVertsDraw)
		{
			glDepthMask(GL_FALSE);
			glDepthFunc(GL_ALWAYS);
		}
		if (modelVertsDraw)
		{
			glDisable(GL_CULL_FACE);
		}
		g_app->bspShader->pushMatrix();
		for (int pass = 0; pass <= 2; pass++)
		{
			if (pass == REND_PASS_MODELSHADER)
				continue;

			g_app->bspShader->bind();
			g_app->bspShader->updateMatrixes();
			for (size_t highlightEnt : highlightEnts)
			{
				if (map->ents[highlightEnt]->hide)
					continue;
				if (renderEnts[highlightEnt].modelIdx >= 0 && renderEnts[highlightEnt].modelIdx < map->modelCount)
				{
					drawModel(&renderEnts[highlightEnt], pass, true, false);
				}
			}
		}
		g_app->bspShader->popMatrix();
		if (modelVertsDraw)
		{
			glEnable(GL_CULL_FACE);
		}
		if (g_render_flags & RENDER_SELECTED_AT_TOP && !modelVertsDraw)
		{
			glDepthMask(GL_TRUE);
			glDepthFunc(GL_LESS);
		}
	}



	if (g_app->pickMode == PICK_FACE_LEAF)
	{
		glDepthMask(GL_FALSE);
		glDepthFunc(GL_ALWAYS);
		glDisable(GL_CULL_FACE);
		glLineWidth(std::min(g_app->lineWidthRange[1], 2.0f));
		g_app->colorShader->bind();
		g_app->matmodel.loadIdentity();
		g_app->matmodel.translate(renderOffset.x, renderOffset.y, renderOffset.z);
		g_app->colorShader->updateMatrixes();
		leafCube->wireframeBuffer->drawFull();
		glLineWidth(std::min(g_app->lineWidthRange[1], 3.0f));
		nodeCube->wireframeBuffer->drawFull();/*
		glLineWidth(std::min(g_app->lineWidthRange[1], 4.0f));
		nodePlaneCube->wireframeBuffer->drawFull();*/
		glLineWidth(1.3f);
		glEnable(GL_CULL_FACE);
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_LESS);
	}

	delayLoadData();
}


void BspRenderer::drawModelClipnodes(int modelIdx, bool highlight, int hullIdx)
{
	if (hullIdx == -1)
	{
		hullIdx = getBestClipnodeHull(modelIdx);
	}

	if (hullIdx == -1 || hullIdx > 3)
	{
		return; // nothing can be drawn
	}

	int nodeIdx = map->models[modelIdx].iHeadnodes[hullIdx];

	if (hullIdx == 0)
	{
		if (drawedClipnodes.count(nodeIdx) > 0)
		{
			return;
		}

		drawedClipnodes.insert(nodeIdx);
	}
	else if (hullIdx > 0)
	{
		if (drawedNodes.count(nodeIdx) > 0)
		{
			return;
		}

		drawedNodes.insert(nodeIdx);
	}

	nodeBuffStr oldHullIdxStruct = nodeBuffStr();
	oldHullIdxStruct.hullIdx = oldHullIdxStruct.modelIdx = -1;


	if (hullIdx == 0 && clipnodesBufferCache.find(nodeIdx) != clipnodesBufferCache.end())
	{
		oldHullIdxStruct = clipnodesBufferCache[nodeIdx];
	}
	else if (hullIdx > 0 && nodesBufferCache.find(nodeIdx) != nodesBufferCache.end())
	{
		oldHullIdxStruct = nodesBufferCache[nodeIdx];
	}

	if (oldHullIdxStruct.hullIdx > 0 && oldHullIdxStruct.modelIdx > 0)
	{
		RenderClipnodes& clip = renderClipnodes[oldHullIdxStruct.modelIdx];

		if (clip.clipnodeBuffer[oldHullIdxStruct.hullIdx])
		{
			clip.clipnodeBuffer[oldHullIdxStruct.hullIdx]->drawFull();
			clip.wireframeClipnodeBuffer[oldHullIdxStruct.hullIdx]->drawFull();
		}
	}
	else
	{
		if (modelIdx < renderClipnodes.size())
		{
			RenderClipnodes& clip = renderClipnodes[modelIdx];

			if (clip.clipnodeBuffer[hullIdx])
			{
				clip.clipnodeBuffer[hullIdx]->drawFull();
				clip.wireframeClipnodeBuffer[hullIdx]->drawFull();
			}
		}
	}
}

void BspRenderer::drawModel(RenderEnt* ent, int pass, bool highlight, bool edgesOnly)
{
	int modelIdx = ent ? ent->modelIdx : 0;

	if (modelIdx < 0 || modelIdx >= numRenderModels)
	{
		return;
	}


	if (pass == REND_PASS_COLORSHADER)
	{
		RenderModel& rend_mdl = renderModels[modelIdx];
		if (rend_mdl.wireframeBuffer)
		{
			if (ent && ent->isDuplicateModel)
				rend_mdl.wireframeBuffer->frameId--;

			if (highlight || (g_render_flags & RENDER_WIREFRAME))
			{
				if (highlight && !rend_mdl.highlighted)
				{
					rend_mdl.highlighted = true;
					auto wireframeVerts = (cVert*)rend_mdl.wireframeBuffer->get_data();
					for (int n = 0; n < rend_mdl.wireframeBuffer->numVerts; n++)
					{
						wireframeVerts[n].c = COLOR4(245, 212, 66, 255);
					}
				}
				else if (!highlight && rend_mdl.highlighted)
				{
					rend_mdl.highlighted = false;
					auto wireframeVerts = (cVert*)rend_mdl.wireframeBuffer->get_data();
					if (modelIdx > 0)
					{
						for (int n = 0; n < rend_mdl.wireframeBuffer->numVerts; n++)
						{
							wireframeVerts[n].c = COLOR4(0, 100, 255, 255);
						}
					}
					else
					{
						for (int n = 0; n < rend_mdl.wireframeBuffer->numVerts; n++)
						{
							wireframeVerts[n].c = COLOR4(100, 100, 100, 255);
						}
					}
				}

				g_app->colorShader->pushMatrix();

				if (g_app->pickMode != PICK_OBJECT && highlight)
				{
					if (ent)
					{
						g_app->matmodel = ent->modelMat4x4_calc;
						g_app->colorShader->updateMatrixes();
					}
					rend_mdl.wireframeBuffer->drawFull();
				}
				else if (ent && ent->needAngles)
				{
					if (!highlight)
					{
						glLineWidth(std::min(g_app->lineWidthRange[1], 2.5f));
						g_app->matmodel = ent->modelMat4x4_calc;
						g_app->colorShader->updateMatrixes();
						rend_mdl.wireframeBuffer->drawFull();
						rend_mdl.wireframeBuffer->frameId--;
						glLineWidth(1.3f);
					}
					else
					{
						g_app->matmodel = ent->modelMat4x4_calc;
						g_app->colorShader->updateMatrixes();
						rend_mdl.wireframeBuffer->drawFull();
					}
				}
				else
				{
					if (ent)
					{
						g_app->matmodel = ent->modelMat4x4_calc;
						g_app->colorShader->updateMatrixes();
					}
					rend_mdl.wireframeBuffer->drawFull();
				}

				g_app->colorShader->popMatrix();
			}
		}

	}

	for (int i = 0; i < renderModels[modelIdx].groupCount; i++)
	{
		RenderGroup& rgroup = renderModels[modelIdx].renderGroups[i];

		if (rgroup.special)
		{
			if (modelIdx == 0 && !(g_render_flags & RENDER_SPECIAL))
			{
				continue;
			}
			else if (modelIdx != 0 && !(g_render_flags & RENDER_SPECIAL_ENTS))
			{
				continue;
			}
		}
		else if (modelIdx != 0 && !(g_render_flags & RENDER_ENTS))
		{
			continue;
		}

		if (pass == REND_PASS_BSPSHADER_TRANSPARENT)
		{
			if (!edgesOnly && rgroup.buffer)
			{
				if (ent && ent->isDuplicateModel)
					rgroup.buffer->frameId--;

				g_app->bspShader->bind();
				g_app->bspShader->pushMatrix();
				if (texturesLoaded && g_render_flags & RENDER_TEXTURES)
				{
					rgroup.texture->bind(0);
				}
				else
				{
					whiteTex->bind(0);
				}

				for (int s = 0; s < MAX_LIGHTMAPS; s++)
				{
					if (highlight && g_app->pickMode == PICK_OBJECT)
					{
						redTex->bind(s + 1);
					}
					else if (lightmapsUploaded && lightmapsGenerated && (g_render_flags & RENDER_LIGHTMAPS))
					{
						if (rgroup.lightmapAtlas[s] && lightEnableFlags[s])
						{
							rgroup.lightmapAtlas[s]->bind(s + 1);
							if (s > 0 && !map->lightdata)
							{
								blackTex->bind(s + 1);
							}
						}
						else
						{
							blackTex->bind(s + 1);
						}

					}
					else
					{
						if (s == 0)
						{
							whiteTex->bind(s + 1);
						}
						else
						{
							blackTex->bind(s + 1);
						}
					}
				}


				if (g_app->pickMode != PICK_OBJECT && highlight)
				{
					if (ent)
					{
						g_app->matmodel = ent->modelMat4x4_calc;
						g_app->bspShader->updateMatrixes();
					}

					rgroup.buffer->drawFull();
				}
				else
				{
					if (highlight)
					{
						if (ent)
						{
							g_app->matmodel = ent->modelMat4x4_calc;
							g_app->bspShader->updateMatrixes();
						}
						rgroup.buffer->drawFull();
					}
					else
					{
						if (ent)
						{
							g_app->matmodel = ent->modelMat4x4_calc_angles;
							g_app->bspShader->updateMatrixes();
						}
						rgroup.buffer->drawFull();
					}
				}
				g_app->bspShader->popMatrix();
			}
		}
	}
}

void BspRenderer::drawPointEntities(std::vector<size_t> highlightEnts, int pass)
{
	// skip worldspawn

	g_app->modelShader->pushMatrix();
	g_app->colorShader->pushMatrix();

	for (size_t i = 1, sz = map->ents.size(); i < sz; i++)
	{
		if (renderEnts[i].modelIdx >= 0)
			continue;
		if (map->ents[i]->hide)
			continue;

		if (g_app->pickInfo.IsSelectedEnt(i))
		{
			if (g_render_flags & RENDER_SELECTED_AT_TOP)
				glDepthFunc(GL_ALWAYS);
			if ((g_render_flags & RENDER_MODELS) && (renderEnts[i].spr
				|| (renderEnts[i].mdl && renderEnts[i].mdl->mdl_mesh_groups.size())))
			{
				if (pass == REND_PASS_MODELSHADER)
				{
					g_app->matmodel = renderEnts[i].modelMat4x4_calc_angles;
					g_app->modelShader->updateMatrixes();

					if (renderEnts[i].mdl)
					{
						renderEnts[i].mdl->DrawMDL();
					}
					else if (renderEnts[i].spr)
					{
						renderEnts[i].spr->DrawSprite();
					}
				}
				else if (pass == REND_PASS_COLORSHADER)
				{
					g_app->matmodel = renderEnts[i].modelMat4x4_calc_angles;
					g_app->colorShader->updateMatrixes();

					if (renderEnts[i].mdl && renderEnts[i].mdl->mdl_cube)
					{
						if (g_render_flags & RENDER_WIREFRAME)
							renderEnts[i].mdl->mdl_cube->wireframeBuffer->drawFull();
					}
					else if (renderEnts[i].spr)
					{
						renderEnts[i].spr->DrawAxes();
						renderEnts[i].spr->DrawBBox();
					}
					if (g_render_flags & RENDER_WIREFRAME)
						renderEnts[i].pointEntCube->wireframeBuffer->drawFull();
				}
			}
			else
			{
				if (pass == REND_PASS_COLORSHADER)
				{
					g_app->matmodel = renderEnts[i].modelMat4x4_calc_angles;
					g_app->colorShader->updateMatrixes();

					renderEnts[i].pointEntCube->axesBuffer->drawFull();

					/*if (renderEnts[i].mdl && renderEnts[i].mdl->mdl_cube)
					{
						if (g_render_flags & RENDER_WIREFRAME)
							renderEnts[i].mdl->mdl_cube->wireframeBuffer->drawFull();
					}*/

					renderEnts[i].pointEntCube->selectBuffer->drawFull();
					renderEnts[i].pointEntCube->wireframeBuffer->drawFull();
				}
			}
			if (g_render_flags & RENDER_SELECTED_AT_TOP)
				glDepthFunc(GL_LESS);
		}
		else
		{
			if ((g_render_flags & RENDER_MODELS) && (renderEnts[i].spr
				|| (renderEnts[i].mdl && renderEnts[i].mdl->mdl_mesh_groups.size())))
			{
				if (pass == REND_PASS_MODELSHADER)
				{
					g_app->matmodel = renderEnts[i].modelMat4x4_calc_angles;
					g_app->modelShader->updateMatrixes();


					if (renderEnts[i].mdl)
					{
						renderEnts[i].mdl->DrawMDL();
					}
					else if (renderEnts[i].spr)
					{
						renderEnts[i].spr->DrawSprite();
					}
				}
				else if (pass == REND_PASS_COLORSHADER)
				{
					//g_app->matmodel = renderEnts[i].modelMat4x4_calc_angles;
					//g_app->colorShader->updateMatrixes();

					///*if (renderEnts[i].mdl && renderEnts[i].mdl->mdl_cube)
					//{
					//	renderEnts[i].mdl->mdl_cube->wireframeBuffer->drawFull();
					//}*/
					//renderEnts[i].pointEntCube->wireframeBuffer->drawFull();
				}
			}
			else
			{
				if (pass == REND_PASS_COLORSHADER)
				{
					g_app->matmodel = renderEnts[i].modelMat4x4_calc_angles;
					g_app->colorShader->updateMatrixes();

					renderEnts[i].pointEntCube->axesBuffer->drawFull();

					/*if (renderEnts[i].mdl && renderEnts[i].mdl->mdl_cube)
					{
						renderEnts[i].mdl->mdl_cube->wireframeBuffer->drawFull();
					}*/
					renderEnts[i].pointEntCube->cubeBuffer->drawFull();
				}
			}
		}
	}

	g_app->modelShader->popMatrix();
	g_app->colorShader->popMatrix();
}

bool BspRenderer::pickPoly(vec3 start, const vec3& dir, int hullIdx, PickInfo& tempPickInfo, Bsp** tmpMap)
{
	bool foundBetterPick = false;

	if (!map || map->ents.empty())
	{
		return foundBetterPick;
	}

	start -= mapOffset;

	if (pickModelPoly(start, dir, vec3(), 0, hullIdx, tempPickInfo))
	{
		if (*tmpMap || *tmpMap == map)
		{
			tempPickInfo.SetSelectedEnt(0);
			*tmpMap = map;
			foundBetterPick = true;
		}
	}

	for (int i = 0; i < (int)map->ents.size(); i++)
	{
		if (map->ents[i]->hide)
			continue;
		if (renderEnts[i].modelIdx >= 0 && renderEnts[i].modelIdx < map->modelCount)
		{
			bool isSpecial = false;
			for (int k = 0; k < renderModels[renderEnts[i].modelIdx].groupCount; k++)
			{
				if (renderModels[renderEnts[i].modelIdx].renderGroups[k].special)
				{
					isSpecial = true;
					break;
				}
			}

			if (isSpecial && !(g_render_flags & RENDER_SPECIAL_ENTS))
			{
				continue;
			}
			else if (!isSpecial && !(g_render_flags & RENDER_ENTS))
			{
				continue;
			}

			if (pickModelPoly(start, dir, renderEnts[i].offset, renderEnts[i].modelIdx, hullIdx, tempPickInfo))
			{
				if (!*tmpMap || *tmpMap == map)
				{
					tempPickInfo.SetSelectedEnt(i);
					*tmpMap = map;
					foundBetterPick = true;
				}
			}
		}
		else if (i > 0 && g_render_flags & RENDER_POINT_ENTS)
		{
			vec3 mins;
			vec3 maxs;
			if (g_render_flags & RENDER_MODELS && renderEnts[i].mdl)
			{
				//renderEnts[i].mdl->ExtractBBox(mins, maxs);
				mins = renderEnts[i].offset + renderEnts[i].mdl->mins;
				maxs = renderEnts[i].offset + renderEnts[i].mdl->maxs;

				if (pickAABB(start, dir, mins, maxs, tempPickInfo.bestDist))
				{
					if (!*tmpMap || *tmpMap == map)
					{
						tempPickInfo.SetSelectedEnt(i);
						*tmpMap = map;
						foundBetterPick = true;
					}
				}

			}
			if (g_render_flags & RENDER_MODELS && renderEnts[i].spr)
			{
				mins = renderEnts[i].offset + renderEnts[i].spr->sprite_groups[renderEnts[i].spr->current_group].
					sprites[renderEnts[i].spr->sprite_groups[renderEnts[i].spr->current_group].current_spr].spriteCube->mins;
				maxs = renderEnts[i].offset + renderEnts[i].spr->sprite_groups[renderEnts[i].spr->current_group].
					sprites[renderEnts[i].spr->sprite_groups[renderEnts[i].spr->current_group].current_spr].spriteCube->maxs;
				if (pickAABB(start, dir, mins, maxs, tempPickInfo.bestDist))
				{
					if (!*tmpMap || *tmpMap == map)
					{
						tempPickInfo.SetSelectedEnt(i);
						*tmpMap = map;
						foundBetterPick = true;
					}
				}
			}
			mins = renderEnts[i].offset + renderEnts[i].pointEntCube->mins;
			maxs = renderEnts[i].offset + renderEnts[i].pointEntCube->maxs;
			if (pickAABB(start, dir, mins, maxs, tempPickInfo.bestDist))
			{
				if (!*tmpMap || *tmpMap == map)
				{
					tempPickInfo.SetSelectedEnt(i);
					*tmpMap = map;
					foundBetterPick = true;
				}
			}
		}
	}

	return foundBetterPick;
}

bool BspRenderer::pickModelPoly(vec3 start, const vec3& dir, vec3 offset, int modelIdx, int hullIdx, PickInfo& tempPickInfo)
{
	if (map->modelCount <= 0 || modelIdx < 0)
		return false;


	int entIdx = map->get_ent_from_model(modelIdx);

	if (entIdx >= 0)
	{
		if (map->ents[entIdx]->hide)
			return false;
	}

	BSPMODEL& model = map->models[modelIdx];

	start -= offset;

	bool foundBetterPick = false;
	bool skipSpecial = !(g_render_flags & RENDER_SPECIAL);

	for (int k = 0; k < model.nFaces; k++)
	{
		FaceMath& faceMath = faceMaths[model.iFirstFace + k];
		BSPFACE32& face = map->faces[model.iFirstFace + k];
		BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];

		if (skipSpecial && modelIdx == 0)
		{
			if (info.nFlags & TEX_SPECIAL)
			{
				continue;
			}
		}

		float t = tempPickInfo.bestDist;
		if (pickFaceMath(start, dir, faceMath, t))
		{
			vec3 vectest = vec3();
			bool badface = false;
			for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
			{
				int edgeIdx = map->surfedges[e];
				BSPEDGE32 edge = map->edges[abs(edgeIdx)];
				vec3& v = edgeIdx < 0 ? map->verts[edge.iVertex[1]] : map->verts[edge.iVertex[0]];
				if (vectest != vec3() && vectest == v)
				{
					badface = true;
					break;
				}
				vectest = v;
			}
			if (!badface)
			{
				foundBetterPick = true;
				tempPickInfo.bestDist = t;
				tempPickInfo.selectedFaces.resize(1);
				tempPickInfo.selectedFaces[0] = model.iFirstFace + k;
			}
		}
	}

	bool selectWorldClips = modelIdx == 0 && (g_render_flags & RENDER_WORLD_CLIPNODES) && hullIdx != -1;
	bool selectEntClips = modelIdx > 0 && (g_render_flags & RENDER_ENT_CLIPNODES);

	if (hullIdx == -1 && renderModels[modelIdx].groupCount == 0)
	{
		// clipnodes are visible for this model because it has no faces
		hullIdx = getBestClipnodeHull(modelIdx);
	}

	if (clipnodesLoaded && (selectWorldClips || selectEntClips) && hullIdx != -1)
	{
		int nodeIdx = map->models[modelIdx].iHeadnodes[hullIdx];
		nodeBuffStr oldHullIdxStruct = nodeBuffStr();
		oldHullIdxStruct.hullIdx = oldHullIdxStruct.modelIdx = -1;

		g_mutex_list[2].lock();
		if (hullIdx == 0 && clipnodesBufferCache.find(nodeIdx) != clipnodesBufferCache.end())
		{
			oldHullIdxStruct = clipnodesBufferCache[nodeIdx];
		}
		else if (hullIdx > 0 && nodesBufferCache.find(nodeIdx) != nodesBufferCache.end())
		{
			oldHullIdxStruct = nodesBufferCache[nodeIdx];
		}
		g_mutex_list[2].unlock();

		if (oldHullIdxStruct.modelIdx < 0 || oldHullIdxStruct.hullIdx < 0)
		{
			oldHullIdxStruct.modelIdx = modelIdx;
			oldHullIdxStruct.hullIdx = hullIdx;
			generateClipnodeBufferForHull(modelIdx, hullIdx);
		}
		for (size_t i = 0; i < renderClipnodes[oldHullIdxStruct.modelIdx].faceMaths[oldHullIdxStruct.hullIdx].size(); i++)
		{
			FaceMath& faceMath = renderClipnodes[oldHullIdxStruct.modelIdx].faceMaths[oldHullIdxStruct.hullIdx][i];

			float t = tempPickInfo.bestDist;
			if (pickFaceMath(start, dir, faceMath, t))
			{
				foundBetterPick = true;
				tempPickInfo.bestDist = t;
				tempPickInfo.selectedFaces.clear();
			}
		}
	}

	return foundBetterPick;
}

bool BspRenderer::pickFaceMath(const vec3& start, const vec3& dir, FaceMath& faceMath, float& bestDist)
{
	float dot = dotProduct(dir, faceMath.normal);
	if (dot >= 0.0f)
	{
		return false; // don't select backfaces or parallel faces
	}

	float t = dotProduct((faceMath.normal * faceMath.fdist) - start, faceMath.normal) / dot;
	if (t < EPSILON || t > bestDist)
	{
		return false; // intersection behind camera, or not a better pick
	}

	// transform intersection point to the plane's coordinate system
	vec3 intersection = start + dir * t;
	vec2 localRayPoint = (faceMath.worldToLocal * vec4(intersection, 1.0f)).xy();

	// check if point is inside the polygon using the plane's 2D coordinate system
	if (!pointInsidePolygon(faceMath.localVerts, localRayPoint))
	{
		return false;
	}

	bestDist = t;
	intersectVec = intersection;

	return true;
}

int BspRenderer::getBestClipnodeHull(int modelIdx)
{
	if (!clipnodesLoaded)
	{
		return -1;
	}

	RenderClipnodes& clip = renderClipnodes[modelIdx];

	// prefer hull that most closely matches the object size from a player's perspective
	if (clip.clipnodeBuffer[0])
	{
		return 0;
	}
	else if (clip.clipnodeBuffer[3])
	{
		return 3;
	}
	else if (clip.clipnodeBuffer[1])
	{
		return 1;
	}
	else if (clip.clipnodeBuffer[2])
	{
		return 2;
	}

	return -1;
}


void BspRenderer::saveEntityState(size_t entIdx)
{
	undoEntityStateMap[entIdx] = *map->ents[entIdx];
}

void BspRenderer::saveLumpState()
{
	if (g_verbose)
		print_log("SAVE LUMP STATES TO BACKUP\n");
	map->update_ent_lump();
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		undoLumpState.lumps[i] = std::vector<unsigned char>(map->lumps[i], map->lumps[i] + map->bsp_header.lump[i].nLength);
	}
}

void BspRenderer::pushEntityUndoStateDelay(const std::string& actionDesc, size_t entIdx, Entity* ent)
{
	delayEntUndoList.push_back({ actionDesc,entIdx,ent });
}


void BspRenderer::pushEntityUndoState(const std::string& actionDesc, size_t entIdx)
{
	if (g_verbose)
		print_log("SAVE ENT STATES TO BACKUP\n");
	if (entIdx >= map->ents.size())
	{
		print_log(get_localized_string(LANG_0287));
		return;
	}

	Entity* ent = map->ents[entIdx];

	if (!ent)
	{
		print_log(get_localized_string(LANG_0288));
		return;
	}

	bool anythingToUndo = false;
	if (undoEntityStateMap[entIdx].keyOrder.size() == ent->keyOrder.size())
	{
		for (size_t i = 0; i < undoEntityStateMap[entIdx].keyOrder.size(); i++)
		{
			std::string oldKey = undoEntityStateMap[entIdx].keyOrder[i];
			std::string newKey = ent->keyOrder[i];
			if (oldKey != newKey)
			{
				anythingToUndo = true;
				break;
			}
			std::string oldVal = undoEntityStateMap[entIdx].keyvalues[oldKey];
			std::string newVal = ent->keyvalues[oldKey];
			if (oldVal != newVal)
			{
				anythingToUndo = true;
				break;
			}
		}
	}
	else
	{
		anythingToUndo = true;
	}

	if (!anythingToUndo)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0289));
		return; // nothing to undo
	}
	pushUndoCommand(new EditEntityCommand(actionDesc, entIdx, undoEntityStateMap[entIdx], *ent));
	saveEntityState(entIdx);
}

void BspRenderer::pushModelUndoState(const std::string& actionDesc, unsigned int targets)
{
	if (g_verbose)
		print_log("SAVE MODEL STATES TO BACKUP\n");
	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0290));
		return;
	}

	auto entIdx = g_app->pickInfo.selectedEnts;
	if (!entIdx.size() && g_app->pickInfo.selectedFaces.size())
	{
		int modelIdx = map->get_model_from_face((int)g_app->pickInfo.selectedFaces[0]);
		if (modelIdx >= 0)
		{
			int entid = map->get_ent_from_model(modelIdx);
			entIdx.push_back(entid);
		}
	}
	if (!entIdx.size())
	{
		entIdx.push_back(0);
	}

	LumpState newLumps = map->duplicate_lumps(targets);

	bool differences[HEADER_LUMPS] = { false };

	int targetLumps = 0;

	bool anyDifference = false;
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (targets & (1 << i))
		{
			if (newLumps.lumps[i].size() != undoLumpState.lumps[i].size() ||
				!std::equal(newLumps.lumps[i].begin(), newLumps.lumps[i].end(),
					undoLumpState.lumps[i].begin()))
			{
				anyDifference = true;
				differences[i] = true;
				if (g_verbose)
				{
					print_log(get_localized_string(LANG_0291), g_lump_names[i], undoLumpState.lumps[i].size(), newLumps.lumps[i].size());
				}
				targetLumps = targetLumps | (1 << i);
			}
		}
	}

	if (!anyDifference)
	{
		print_log(get_localized_string(LANG_0292));
		return;
	}

	// delete lumps that have no differences to save space
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (!differences[i])
		{
			undoLumpState.lumps[i].clear();
			newLumps.lumps[i].clear();
		}
	}

	EditBspModelCommand* editCommand = new EditBspModelCommand(actionDesc, (int)entIdx[0], undoLumpState, newLumps, undoEntityStateMap[entIdx[0]].origin, targetLumps);
	pushUndoCommand(editCommand);

	// entity origin edits also update the ent origin (TODO: this breaks when moving + scaling something)
	saveEntityState((int)entIdx[0]);
}

void BspRenderer::pushUndoCommand(Command* cmd)
{
	cmd->execute();
	undoHistory.push_back(cmd);
	clearRedoCommands();

	while (!undoHistory.empty() && undoHistory.size() > (size_t)g_settings.undoLevels)
	{
		delete undoHistory[0];
		undoHistory.erase(undoHistory.begin());
	}

	calcUndoMemoryUsage();

	saveLumpState();
}

void BspRenderer::undo()
{
	if (undoHistory.empty())
	{
		return;
	}

	Command* undoCommand = undoHistory[undoHistory.size() - 1];
	if (!undoCommand->allowedDuringLoad && g_app->isLoading)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0293), undoCommand->desc);
		return;
	}

	undoCommand->undo();
	undoHistory.pop_back();
	redoHistory.push_back(undoCommand);
	g_app->updateEnts();
}

void BspRenderer::redo()
{
	if (redoHistory.empty())
	{
		return;
	}

	Command* redoCommand = redoHistory[redoHistory.size() - 1];
	if (!redoCommand->allowedDuringLoad && g_app->isLoading)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0294), redoCommand->desc);
		return;
	}

	redoCommand->execute();
	redoHistory.pop_back();
	undoHistory.push_back(redoCommand);
	g_app->updateEnts();
}

void BspRenderer::clearUndoCommands()
{
	for (size_t i = 0; i < undoHistory.size(); i++)
	{
		delete undoHistory[i];
	}

	undoHistory.clear();
	calcUndoMemoryUsage();
}

void BspRenderer::clearRedoCommands()
{
	for (size_t i = 0; i < redoHistory.size(); i++)
	{
		delete redoHistory[i];
	}

	redoHistory.clear();
	calcUndoMemoryUsage();
}

void BspRenderer::calcUndoMemoryUsage()
{
	undoMemoryUsage = (undoHistory.size() + redoHistory.size()) * sizeof(Command*);

	for (size_t i = 0; i < undoHistory.size(); i++)
	{
		undoMemoryUsage += undoHistory[i]->memoryUsage();
	}
	for (size_t i = 0; i < redoHistory.size(); i++)
	{
		undoMemoryUsage += redoHistory[i]->memoryUsage();
	}
}

void BspRenderer::clearDrawCache()
{
	drawedClipnodes.clear();
	drawedNodes.clear();
}

PickInfo::PickInfo()
{
	selectedEnts.clear();
	selectedFaces.clear();
	bestDist = 0.0f;
}

void PickInfo::AddSelectedEnt(size_t entIdx)
{
	if (entIdx < 0xFFFFFFu && !IsSelectedEnt(entIdx))
	{
		selectedEnts.push_back(entIdx);
	}
	pickCount++;
}

void PickInfo::SetSelectedEnt(size_t entIdx)
{
	selectedEnts.clear();
	AddSelectedEnt(entIdx);
}

void PickInfo::DelSelectedEnt(size_t entIdx)
{
	if (IsSelectedEnt(entIdx))
	{
		pickCount++;
		selectedEnts.erase(std::find(selectedEnts.begin(), selectedEnts.end(), entIdx));
	}
}

bool PickInfo::IsSelectedEnt(size_t entIdx)
{
	return selectedEnts.size() && std::find(selectedEnts.begin(), selectedEnts.end(), entIdx) != selectedEnts.end();
}

