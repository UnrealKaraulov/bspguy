#include "lang.h"
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
#include "Polygon3D.h"
#include "util.h"
#include "log.h"

#include <execution>

BspRenderer::BspRenderer(Bsp* _map) : undoLumpState(LumpState(_map))
{
	map = _map;
	map->setBspRender(this);

	curLeafIdx = 0;
	lightmaps = NULL;
	glTexturesSwap.clear();
	glTextures.clear();

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
	intersectDist = 0.0f;

	renderClipnodes = {};

	renderCameraOrigin = renderCameraAngles = vec3();
	renderCameraAngles.z = 90.0f;
	// Setup Deafult Camera

	if (g_settings.start_at_entity)
	{
		print_log(get_localized_string(LANG_0267));
		Entity* foundEnt = NULL;
		bool foundCam = false;

		for (auto ent : map->ents)
		{
			if (ent->hasKey("classname") && ent->keyvalues["classname"] == "trigger_camera")
			{
				foundCam = true;
				foundEnt = ent;
				break;
			}
		}

		if (!foundEnt)
		{
			for (auto ent : map->ents)
			{
				if (ent->hasKey("classname") && ent->keyvalues["classname"] == "info_player_start")
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
				foundEnt = ent;
				if (foundEnt)
					break;
			}
		}

		if (foundEnt)
		{
			renderCameraOrigin = foundEnt->origin;
			if (!foundCam)
			{
				renderCameraOrigin.z += 32;
				for (unsigned int i = 0; i < foundEnt->keyOrder.size(); i++)
				{
					if (foundEnt->keyOrder[i] == "angles")
					{
						renderCameraAngles = parseVector(foundEnt->keyvalues["angles"]);
					}
					if (foundEnt->keyOrder[i] == "angle")
					{
						float y = str_to_float(foundEnt->keyvalues["angle"]);

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
			}
			else
			{
				auto targets = foundEnt->getTargets();
				Entity* targetEnt = NULL;
				for (auto ent2 : map->ents)
				{
					if (targetEnt)
						break;
					if (ent2->hasKey("targetname"))
					{
						for (auto target : targets)
						{
							if (ent2->keyvalues["targetname"] == target)
							{
								targetEnt = ent2;
								break;
							}
						}
					}
				}

				if (targetEnt)
				{
					vec3 newAngle = targetEnt->origin - foundEnt->origin;
					VectorAngles(newAngle, newAngle);
					newAngle[0] = -newAngle[0];
					renderCameraAngles = newAngle;
				}
			}

			renderCameraAngles = renderCameraAngles.flip();
			renderCameraAngles.z = renderCameraAngles.z + 90.0f;
			renderCameraAngles = renderCameraAngles.normalize_angles();
			renderCameraAngles.y = 0.0f;
		}
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
	for (auto& r : renderModels)
		delete r;
	renderModels.clear();
	renderClipnodes.clear();

	faceMaths.clear();
	g_mutex_list[2].lock();
	nodesBufferCache.clear();
	clipnodesBufferCache.clear();
	g_mutex_list[2].unlock();
	clearDrawCache();
	//loadTextures();
	//loadLightmaps();
	preRenderEnts();
	preRenderFaces();
	calcFaceMaths();

	lightmapFuture = std::async(std::launch::async, &BspRenderer::loadLightmaps, this);
	texturesFuture = std::async(std::launch::async, &BspRenderer::loadTextures, this);
	clipnodesFuture = std::async(std::launch::async, &BspRenderer::loadClipnodes, this);

	// cache ent targets so first selection doesn't lag
	for (size_t i = 0; i < map->ents.size(); i++)
	{
		map->ents[i]->getTargets();
	}

	undoLumpState = map->duplicate_lumps();
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

			if (g_settings.strip_wad_path)
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

	if (foundInfoDecals && !foundDecalWad)
	{
		wadNames.push_back("decals.wad");
	}

	for (size_t i = 0; i < wadNames.size(); i++)
	{
		std::string path = std::string();

		if (map->is_bsp_pathos && wadNames[i].size() > 4)
		{
			std::string wadDirName = wadNames[i];

			wadDirName.pop_back();
			wadDirName.pop_back();
			wadDirName.pop_back();
			wadDirName.pop_back();


			if (FindPathInAssets(map, "textures/world/" + wadDirName + "/" + wadNames[i], path))
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
		}

		if (path.empty() && FindPathInAssets(map, wadNames[i], path))
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

	map->update_lump_pointers();

	glTexturesSwap.resize(map->textureCount);
	for (int i = 0; i < map->textureCount; i++)
	{
		int texOffset = ((int*)map->textures)[i + 1];
		if (texOffset < 0)
		{
			glTexturesSwap[i].push_back(missingTex);
			missingCount++;
			continue;
		}

		BSPMIPTEX* tex = ((BSPMIPTEX*)(map->textures + texOffset));
		if (tex->szName[0] == '\0' || tex->nWidth == 0 || tex->nHeight == 0 || strlen(tex->szName) >= MAXTEXTURENAME)
		{
			glTexturesSwap[i].push_back(missingTex);
			missingCount++;
			continue;
		}

		if (strcasecmp(tex->szName, "aaatrigger") == 0)
		{
			glTexturesSwap[i].push_back(aaatriggerTex_rgba);
			continue;
		}

		if (memcmp(tex->szName, "sky", 3) == 0 || memcmp(tex->szName, "SKY", 3) == 0)
		{
			glTexturesSwap[i].push_back(skyTex_rgba);
			continue;
		}

		std::vector<std::string> texNames;

		if (/*tex->szName[0] == '-' || */tex->szName[0] == '+')
		{
			char* newname = &tex->szName[2]; // +0BTN1 +1BTN1 +ABTN1 +BBTN1

			bool is_int = (tex->szName[1] >= '0' && tex->szName[1] <= '9');

			for (int n = 0; n < map->textureCount; n++)
			{
				int offset2 = ((int*)map->textures)[n + 1];
				if (offset2 >= 0)
				{
					BSPMIPTEX* tex2 = (BSPMIPTEX*)(map->textures + offset2);
					if (strlen(tex2->szName) > 2 && strcasecmp(newname, &tex2->szName[2]) == 0)
					{
						if (is_int == (tex2->szName[1] >= '0' && tex2->szName[1] <= '9'))
						{
							/*if (tex->szName[0] == '-')
							{
								if (rand() % 50 > 30)
								{
									texNames.push_back(tex2->szName);
									break;
								}
							}
							else
							{*/
							texNames.push_back(tex2->szName);
							/*}*/
						}
					}
				}
			}

			if (texNames.size() > 1)
			{
				std::sort(texNames.begin(), texNames.end());
				texNames.erase(std::unique(texNames.begin(), texNames.end()), texNames.end());
			}
			else if (texNames.empty())
			{
				texNames.push_back(tex->szName);
			}
		}
		else
		{
			texNames.push_back(tex->szName);
		}


		for (auto& tex_name : texNames)
		{
			COLOR3* imageData = NULL;
			WADTEX* wadTex = NULL;
			std::string wadName = "unknown.wad";
			if (tex->nOffsets[0] <= 0)
			{
				bool foundInWad = false;
				for (size_t k = 0; k < wads.size(); k++)
				{
					if (wads[k]->hasTexture(tex_name))
					{
						foundInWad = true;
						wadName = wads[k]->wadname;
						wadTex = wads[k]->readTexture(tex_name);
						imageData = ConvertWadTexToRGB(wadTex);
						wadTexCount++;
						break;
					}
				}

				if (!foundInWad)
				{
					if (texNames.size() == 1)
					{
						glTexturesSwap[i].push_back(missingTex);
						missingCount++;
					}
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

				if (texNames.size() > 1)
				{
					for (int n = 0; n < map->textureCount; n++)
					{
						int offset2 = ((int*)map->textures)[n + 1];
						if (offset2 >= 0)
						{
							tex = (BSPMIPTEX*)(map->textures + offset2);
							if (strcasecmp(tex_name.c_str(), tex->szName) == 0)
							{
								break;
							}
						}
					}
				}

				imageData = ConvertMipTexToRGB(tex, map->is_texture_with_pal(i) ? NULL : (COLOR3*)palette);
				embedCount++;
			}

			if (imageData)
			{
				if (wadTex)
				{
					Texture* tmpTex = new Texture(wadTex->nWidth, wadTex->nHeight, (unsigned char*)imageData, tex_name);
					tmpTex->setWadName(wadName);
					glTexturesSwap[i].push_back(tmpTex);
				}
				else
				{
					Texture* tmpTex = new Texture(tex->nWidth, tex->nHeight, (unsigned char*)imageData, tex_name);
					tmpTex->setWadName("internal");
					glTexturesSwap[i].push_back(tmpTex);
				}
			}
			else
			{
				if (texNames.size() == 1)
				{
					glTexturesSwap[i].push_back(missingTex);
					missingCount++;
				}
			}

			delete wadTex;
		}

		if (glTexturesSwap[i].empty())
		{
			glTexturesSwap[i].push_back(missingTex);
			missingCount++;
		}
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
	reloadTextures();
	loadLightmaps();
	calcFaceMaths();
	preRenderEnts();
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

		g_mutex_list[2].lock();
		clipnodesBufferCache.clear();
		nodesBufferCache.clear();
		g_mutex_list[2].unlock();

		clipnodesFuture = std::async(std::launch::async, &BspRenderer::loadClipnodes, this);
	}
}

RenderClipnodes* BspRenderer::addClipnodeModel(int modelIdx)
{
	if (modelIdx < 0)
	{
		return NULL;
	}


	if (renderClipnodes.empty())
	{
		reloadClipnodes();
		return NULL;
	}

	auto target_size = (size_t)std::max(modelIdx, (int)renderClipnodes.size()) + 1;

	while (renderClipnodes.size() < target_size)
	{
		renderClipnodes.emplace_back(RenderClipnodes());
	}

	while (renderClipnodes.size() > target_size)
	{
		renderClipnodes.pop_back();
	}

	generateClipnodeBuffer(modelIdx);

	return &renderClipnodes[modelIdx];
}

void BspRenderer::loadLightmaps()
{
	std::vector<LightmapNode> atlases{};
	std::vector<Texture*> atlasTextures{};
	atlases.emplace_back(LightmapNode(0, 0, MAX_LIGHTMAP_ATLAS_SIZE, MAX_LIGHTMAP_ATLAS_SIZE));
	atlasTextures.push_back(new Texture(MAX_LIGHTMAP_ATLAS_SIZE, MAX_LIGHTMAP_ATLAS_SIZE,
		new unsigned char[MAX_LIGHTMAP_ATLAS_SIZE * MAX_LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3)], "LIGHTMAP"));

	memset(atlasTextures[atlasTextures.size() - 1]->get_data(), 255, MAX_LIGHTMAP_ATLAS_SIZE * MAX_LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

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
			map->GetFaceLightmapSize(i, size);
			map->GetFaceExtents(i, imins, imaxs);

			float textureStep = map->CalcFaceTextureStep(i) * 1.0f;

			LightmapInfo& info = lightmaps[i];
			info.w = size[0];
			info.h = size[1];
			info.midTexU = (float)(size[0]) / 2.0f;
			info.midTexV = (float)(size[1]) / 2.0f;

			// TODO: float mins/maxs not needed?
			info.midPolyU = (imins[0] + imaxs[0]) * textureStep / 2.0f;
			info.midPolyV = (imins[1] + imaxs[1]) * textureStep / 2.0f;

			for (int s = 0; s < MAX_LIGHTMAPS; s++)
			{
				if (face.nStyles[s] == 255)
					continue;

				if (map->is_bsp_pathos)
				{
					if (face.nStyles[s] == LM_AMBIENT_STYLE ||
						face.nStyles[s] == LM_DIFFUSE_STYLE ||
						face.nStyles[s] == LM_LIGHTVECS_STYLE)
					{
						info.atlasId[s] = 0;
						continue;
					}
				}
				int atlasId = 0;

				if (atlases.size() > 0)
					atlasId = (int)(atlases.size()) - 1;

				// TODO: Try fitting in earlier atlases before using the latest one
				if (!atlases[atlasId].insert(info.w, info.h, info.x[s], info.y[s]))
				{
					atlases.emplace_back(LightmapNode(0, 0, MAX_LIGHTMAP_ATLAS_SIZE, MAX_LIGHTMAP_ATLAS_SIZE));
					atlasTextures.push_back(new Texture(MAX_LIGHTMAP_ATLAS_SIZE, MAX_LIGHTMAP_ATLAS_SIZE, new unsigned char[MAX_LIGHTMAP_ATLAS_SIZE * MAX_LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3)], "LIGHTMAP"));

					atlasId++;
					memset(atlasTextures[atlasId]->get_data(), 255, MAX_LIGHTMAP_ATLAS_SIZE * MAX_LIGHTMAP_ATLAS_SIZE * sizeof(COLOR3));

					if (!atlases[atlasId].insert(info.w, info.h, info.x[s], info.y[s]))
					{
						print_log(get_localized_string(LANG_0275), info.w, info.h, MAX_LIGHTMAP_ATLAS_SIZE, MAX_LIGHTMAP_ATLAS_SIZE);
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
						int dst = (info.y[s] + y) * MAX_LIGHTMAP_ATLAS_SIZE + info.x[s] + x;
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

	glLightmapTextures.assign(atlasTextures.begin(), atlasTextures.end());

	//lodepng_encode24_file("atlas.png", atlasTextures[0]->data, MAX_LIGHTMAP_ATLAS_SIZE, MAX_LIGHTMAP_ATLAS_SIZE);
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
	genRenderFaces();

	for (auto model : renderModels)
	{
		for (auto& g : model->renderGroups)
		{
			if (g.buffer)
				g.buffer->uploaded = false;
		}
	}
	for (auto f : g_app->pickInfo.selectedFaces)
	{
		highlightFace(f, 1);
	}
}

void BspRenderer::genRenderFaces()
{
	deleteRenderFaces();

	while ((int)renderModels.size() < map->modelCount)
		renderModels.push_back(new RenderModel());
	while (renderModels.size() > (size_t)map->modelCount)
	{
		delete renderModels.back();
		renderModels.pop_back();
	}

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
	while ((int)renderModels.size() < map->modelCount + 1)
		renderModels.push_back(new RenderModel());
	while (renderModels.size() > (size_t)map->modelCount + 1)
	{
		delete renderModels.back();
		renderModels.pop_back();
	}


	print_log(get_localized_string(LANG_0279));
}

void BspRenderer::deleteRenderClipnodes()
{
	if (renderClipnodes.size())
	{
		for (auto& clip : renderClipnodes)
		{
			deleteRenderModelClipnodes(&clip);
		}
		renderClipnodes.clear();
	}
}

void BspRenderer::deleteRenderModelClipnodes(RenderClipnodes* renderClip)
{
	for (int i = 0; i < MAX_MAP_HULLS; i++)
	{
		delete renderClip->clipnodeBuffer[i];
		renderClip->clipnodeBuffer[i] = NULL;

		delete renderClip->wireframeClipnodeBuffer[i];
		renderClip->wireframeClipnodeBuffer[i] = NULL;
	}
}

void BspRenderer::deleteRenderFaces()
{
	if (renderModels.size())
	{
		for (auto& r : renderModels)
			delete r;
		renderModels.clear();
	}
}

void BspRenderer::deleteTextures()
{
	for (size_t i = 0; i < glTextures.size(); i++)
	{
		if (glTextures[i].size())
		{
			for (auto& tex : glTextures[i])
			{
				if (tex != missingTex
					&& tex != aaatriggerTex_rgba
					&& tex != skyTex_rgba)
				{
					delete tex;
					tex = missingTex;
				}
			}
		}
	}

	glTextures.clear();
}

void BspRenderer::deleteLightmapTextures()
{
	for (size_t i = 0; i < glLightmapTextures.size(); i++)
	{
		if (glLightmapTextures[i])
		{
			delete glLightmapTextures[i];
			glLightmapTextures[i] = NULL;
		}
	}
	glLightmapTextures.clear();
}

void BspRenderer::deleteFaceMaths()
{
	faceMaths.clear();
}

int BspRenderer::refreshModel(int modelIdx, bool refreshClipnodes, bool triangulate)
{
	if (modelIdx < 0 || modelIdx >= map->modelCount)
		return 0;

	while (modelIdx >= (int)renderModels.size())
	{
		print_log(get_localized_string(LANG_0280), modelIdx, renderModels.size());
		addNewRenderFace();
	}

	BSPMODEL& model = map->models[modelIdx];
	RenderModel* renderModel = new RenderModel();
	renderModel->renderFaces.resize(model.nFaces);

	std::vector<RenderGroup> renderGroups{};
	std::vector<std::vector<lightmapVert>> renderGroupVerts{};
	std::vector<cVert> wireframeVerts_full;

	for (int i = 0; i < model.nFaces; i++)
	{
		int faceIdx = model.iFirstFace + i;
		BSPFACE32& face = map->faces[faceIdx];

		if (face.nEdges <= 0 || face.iTextureInfo < 0)
			continue;

		BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
		BSPMIPTEX* tex = NULL;


		float textureStep = map->CalcFaceTextureStep(i) * 1.0f;


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
			lw = (float)lmap->w / (float)MAX_LIGHTMAP_ATLAS_SIZE;
			lh = (float)lmap->h / (float)MAX_LIGHTMAP_ATLAS_SIZE;
		}

		bool isSpecial = texinfo.nFlags & TEX_SPECIAL;
		bool hasLighting = face.nStyles[0] != 255 && face.nLightmapOffset >= 0 && !isSpecial;

		for (int s = 0; s < MAX_LIGHTMAPS; s++)
		{
			if (map->is_bsp_pathos)
			{
				if (face.nStyles[s] == LM_AMBIENT_STYLE ||
					face.nStyles[s] == LM_DIFFUSE_STYLE ||
					face.nStyles[s] == LM_LIGHTVECS_STYLE)
				{
					lightmapAtlas[s] = NULL;
					continue;
				}
			}
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
			int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];

			vec3 vert = map->verts[vertIdx];
			verts[e].pos = vert.flip();

			verts[e].r = 0.0f;
			if (ent && ent->rendermode > 0)
			{
				verts[e].g = 1.001f + std::fabs((float)ent->rendermode);
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
				float fLightMapU = lmap->midTexU + (fU - lmap->midPolyU) / textureStep;
				float fLightMapV = lmap->midTexV + (fV - lmap->midPolyV) / textureStep;

				float uu = (fLightMapU / (float)lmap->w) * lw;
				float vv = (fLightMapV / (float)lmap->h) * lh;

				float pixelStep = 1.0f / (float)MAX_LIGHTMAP_ATLAS_SIZE;

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

		if (triangulate)
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
			if (texinfo.iMiptex <= -1 || texinfo.iMiptex >= map->textureCount)
				continue;
			bool textureMatch = !texturesLoaded || std::find(renderGroups[k].textures.begin(), renderGroups[k].textures.end(), glTextures[texinfo.iMiptex][0]) != renderGroups[k].textures.end();
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
			newGroup.textures = texturesLoaded && texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount ? glTextures[texinfo.iMiptex] : std::vector<Texture*>{ greyTex };
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

	renderModel->renderGroups = std::move(renderGroups);


	for (size_t i = 0; i < renderModel->renderGroups.size(); i++)
	{
		lightmapVert* result_verts = new lightmapVert[renderGroupVerts[i].size() + 1];

		if (renderGroupVerts[i].size() > 0)
			memcpy(result_verts, &renderGroupVerts[i][0], renderGroupVerts[i].size() * sizeof(lightmapVert));

		renderModel->renderGroups[i].buffer = new VertexBuffer(g_app->bspShader, result_verts, (int)(renderGroupVerts[i].size()), GL_TRIANGLES);
		renderModel->renderGroups[i].buffer->ownData = true;
		renderModel->renderGroups[i].buffer->frameId = 0;
	}

	if (wireframeVerts_full.size())
	{
		std::vector<cVert> cleanupWireframe = removeDuplicateWireframeLines(wireframeVerts_full);

#ifdef _DEBUG
		if (g_settings.verboseLogs)
			print_log("Optimize wireframe {} model: {} to {} lines.\n", modelIdx, wireframeVerts_full.size(), cleanupWireframe.size());
#endif
		cVert* resultWireFrame = new cVert[cleanupWireframe.size()];
		memcpy(resultWireFrame, cleanupWireframe.data(), cleanupWireframe.size() * sizeof(cVert));

		renderModel->wireframeBuffer = new VertexBuffer(g_app->colorShader, resultWireFrame, (int)(cleanupWireframe.size()), GL_LINES);
		renderModel->wireframeBuffer->ownData = true;
		renderModel->wireframeBuffer->frameId = 0;
	}

	for (int i = 0; i < model.nFaces; i++)
	{
		refreshFace(model.iFirstFace + i);
	}

	delete renderModels[modelIdx];
	renderModels[modelIdx] = renderModel;

	if (refreshClipnodes)
		generateClipnodeBuffer(modelIdx);

	return (int)(renderModel->renderGroups.size());
}

bool BspRenderer::refreshModelClipnodes(int modelIdx)
{
	if (!clipnodesLoaded)
	{
		return false;
	}
	if (modelIdx < 0 || modelIdx >= (int)renderClipnodes.size())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0280), modelIdx, renderClipnodes.size());
		if (modelIdx < 0)
			return false;
		generateClipnodeBuffer(modelIdx);
	}

	for (int hullIdx = 0; hullIdx < MAX_MAP_HULLS; hullIdx++)
	{
		int nodeIdx = map->models[modelIdx].iHeadnodes[hullIdx];
		if (hullIdx == 0 && clipnodesBufferCache.find(nodeIdx) != clipnodesBufferCache.end())
		{
			g_mutex_list[2].lock();
			clipnodesBufferCache.erase(nodeIdx);
			g_mutex_list[2].unlock();
		}
		else if (hullIdx > 0 && nodesBufferCache.find(nodeIdx) != nodesBufferCache.end())
		{
			g_mutex_list[2].lock();
			nodesBufferCache.erase(nodeIdx);
			g_mutex_list[2].unlock();
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
	g_mutex_list[2].lock();
	clipnodesBufferCache.clear();
	nodesBufferCache.clear();
	g_mutex_list[2].unlock();

	while ((int)renderClipnodes.size() < map->modelCount)
	{
		renderClipnodes.emplace_back(RenderClipnodes());
	}
	while (renderClipnodes.size() > (size_t)map->modelCount)
	{
		renderClipnodes.pop_back();
	}
	std::vector<int> tmpRenderHulls(MAX_MAP_HULLS);
	std::iota(tmpRenderHulls.begin(), tmpRenderHulls.end(), 0);

	// Using 4x threads instead of very big count
	std::for_each(std::execution::par_unseq, tmpRenderHulls.begin(), tmpRenderHulls.end(),
		[&](int hull)
		{
			for (int i = 0; i < (int)renderClipnodes.size(); i++)
			{
				generateClipnodeBufferForHull(i, hull);
			}
		}
	);
}


void BspRenderer::generateNavMeshBuffer() {
	int hull = 3;
	RenderClipnodes* renderClip = &renderClipnodes[0];
	renderClip->clipnodeBuffer[hull] = NULL;
	renderClip->wireframeClipnodeBuffer[hull] = NULL;

	NavMesh* navMesh = NavMeshGenerator().generate(map, hull);
	std::vector<Polygon3D> navPolys = navMesh->getPolys();

	g_app->debugNavMesh = navMesh;
	g_app->debugNavPoly = 529;
	debugNavMesh = navMesh;
	debugFaces = navPolys;

	static COLOR4 hullColors[] = {
		COLOR4(255, 255, 255, 128),
		COLOR4(96, 255, 255, 128),
		COLOR4(255, 96, 255, 128),
		COLOR4(255, 255, 96, 128),
	};
	COLOR4 color = hullColors[hull];

	std::vector<cVert> allVerts;
	std::vector<cVert> wireframeVerts;
	std::vector<FaceMath> navFaceMaths;

	for (size_t m = 0; m < navPolys.size(); m++) {
		Polygon3D& poly = navPolys[m];

		vec3 normal = poly.plane_z;

		// calculations for face picking
		{
			FaceMath faceMath;
			faceMath.normal = normal;
			faceMath.fdist = poly.fdist;
			faceMath.worldToLocal = poly.worldToLocal;
			faceMath.localVerts = poly.localVerts;
			navFaceMaths.push_back(faceMath);
		}

		// create the verts for rendering
		{
			std::vector<vec3> renderVerts;
			renderVerts.resize(poly.verts.size());
			for (size_t i = 0; i < poly.verts.size(); i++) {
				renderVerts[i] = poly.verts[i].flip();
			}

			COLOR4 wireframeColor = { 0, 0, 0, 255 };
			for (size_t k = 0; k < renderVerts.size(); k++) {
				wireframeVerts.emplace_back(cVert(renderVerts[k], wireframeColor));
				wireframeVerts.emplace_back(cVert(renderVerts[(k + 1) % renderVerts.size()], wireframeColor));
			}

			vec3 lightDir = vec3(1, 1, -1).normalize();
			float dot = (dotProduct(normal, lightDir) + 1) / 2.0f;
			if (dot > 0.5f) {
				dot = dot * dot;
			}
			color = hullColors[hull];
			if (normal.z < -0.8 || true) {
				static int r = 0;
				r = (r + 1) % 8;
				if (r == 0) {
					color = COLOR4(255, 32, 32, 255);
				}
				else if (r == 1) {
					color = COLOR4(255, 255, 32, 255);
				}
				else if (r == 2) {
					color = COLOR4(255, 32, 255, 255);
				}
				else if (r == 3) {
					color = COLOR4(255, 128, 255, 255);
				}
				else if (r == 4) {
					color = COLOR4(32, 32, 255, 255);
				}
				else if (r == 5) {
					color = COLOR4(32, 255, 255, 255);
				}
				else if (r == 6) {
					color = COLOR4(32, 128, 255, 255);
				}
				else if (r == 7) {
					color = COLOR4(32, 255, 128, 255);
				}
			}
			COLOR4 faceColor = color * (dot);

			// convert from TRIANGLE_FAN style verts to TRIANGLES
			for (size_t k = 2; k < renderVerts.size(); k++) {
				allVerts.emplace_back(cVert(renderVerts[0], faceColor));
				allVerts.emplace_back(cVert(renderVerts[k - 1], faceColor));
				allVerts.emplace_back(cVert(renderVerts[k], faceColor));
			}
		}
	}

	cVert* output = new cVert[allVerts.size()];
	std::copy(allVerts.begin(), allVerts.end(), output);

	cVert* wireOutput = new cVert[wireframeVerts.size()];
	std::copy(wireframeVerts.begin(), wireframeVerts.end(), wireOutput);


	if (allVerts.size() == 0 || wireframeVerts.size() == 0) {
		renderClip->clipnodeBuffer[hull] = NULL;
		renderClip->wireframeClipnodeBuffer[hull] = NULL;
		delete[] output;
		delete[] wireOutput;
		return;
	}

	renderClip->clipnodeBuffer[hull] = new VertexBuffer(g_app->colorShader, output, (int)allVerts.size(), GL_TRIANGLES);
	renderClip->clipnodeBuffer[hull]->ownData = true;

	renderClip->wireframeClipnodeBuffer[hull] = new VertexBuffer(g_app->colorShader, wireOutput, (int)wireframeVerts.size(), GL_LINES);
	renderClip->wireframeClipnodeBuffer[hull]->ownData = true;

	renderClip->faceMaths[hull] = std::move(navFaceMaths);

	std::string navmesh_hull3_path = g_working_dir + map->bsp_name + "_hull" + std::to_string(hull) + ".obj";

	std::ofstream file(navmesh_hull3_path, std::ios::out | std::ios::trunc);
	for (size_t i = 0; i < allVerts.size(); i++) {
		vec3 v = vec3(allVerts[i].pos.x, allVerts[i].pos.y, allVerts[i].pos.z);
		file << "v " << std::fixed << std::setprecision(2) << v.x << " " << v.y << " " << v.z << std::endl;
	}
	for (size_t i = 0; i < allVerts.size(); i += 3) {
		file << "f " << (i + 1) << " " << (i + 2) << " " << (i + 3) << std::endl;
	}
	print_log("Wrote {} verts to {}\n", allVerts.size(), navmesh_hull3_path);
	file.close();
}

void BspRenderer::generateLeafNavMeshBuffer() {
	int hull = NAV_HULL;
	RenderClipnodes* renderClip = &renderClipnodes[0];
	renderClip->clipnodeBuffer[hull] = NULL;
	renderClip->wireframeClipnodeBuffer[hull] = NULL;

	LeafNavMesh* navMesh = LeafNavMeshGenerator().generate(map);
	g_app->debugLeafNavMesh = navMesh;

	static COLOR4 hullColors[] = {
		COLOR4(255, 255, 255, 128),
		COLOR4(96, 255, 255, 128),
		COLOR4(255, 96, 255, 128),
		COLOR4(255, 255, 96, 128),
	};
	COLOR4 color = hullColors[hull];

	std::vector<cVert> allVerts;
	std::vector<cVert> wireframeVerts;
	std::vector<FaceMath> navFaceMaths;

	for (size_t lf = 0; lf < navMesh->nodes.size(); lf++) {
		LeafNode& mesh = navMesh->nodes[lf];

		color = hullColors[hull];
		static int r = 0;
		r = (r + 1) % 8;
		if (r == 0) {
			color = COLOR4(255, 32, 32, 128);
		}
		else if (r == 1) {
			color = COLOR4(255, 255, 32, 128);
		}
		else if (r == 2) {
			color = COLOR4(255, 32, 255, 128);
		}
		else if (r == 3) {
			color = COLOR4(255, 128, 255, 128);
		}
		else if (r == 4) {
			color = COLOR4(32, 32, 255, 128);
		}
		else if (r == 5) {
			color = COLOR4(32, 255, 255, 128);
		}
		else if (r == 6) {
			color = COLOR4(32, 128, 255, 128);
		}
		else if (r == 7) {
			color = COLOR4(32, 255, 128, 128);
		}

		for (size_t m = 0; m < mesh.leafFaces.size(); m++) {
			Polygon3D& poly = mesh.leafFaces[m];

			vec3 normal = poly.plane_z;

			// calculations for face picking
			{
				FaceMath faceMath;
				faceMath.normal = normal;
				faceMath.fdist = poly.fdist;
				faceMath.worldToLocal = poly.worldToLocal;
				faceMath.localVerts = poly.localVerts;
				navFaceMaths.push_back(faceMath);
			}

			// create the verts for rendering
			{
				std::vector<vec3> renderVerts;
				renderVerts.resize(poly.verts.size());
				for (size_t i = 0; i < poly.verts.size(); i++) {
					renderVerts[i] = poly.verts[i].flip();
				}

				COLOR4 wireframeColor = { 0, 0, 0, 255 };
				for (size_t k = 0; k < renderVerts.size(); k++) {
					wireframeVerts.emplace_back(cVert(renderVerts[k], wireframeColor));
					wireframeVerts.emplace_back(cVert(renderVerts[(k + 1) % renderVerts.size()], wireframeColor));
				}

				vec3 lightDir = vec3(1, 1, -1).normalize();
				float dot = (dotProduct(normal, lightDir) + 1) / 2.0f;
				if (dot > 0.5f) {
					dot = dot * dot;
				}
				COLOR4 faceColor = color * (dot);

				// convert from TRIANGLE_FAN style verts to TRIANGLES
				for (size_t k = 2; k < renderVerts.size(); k++) {
					allVerts.emplace_back(cVert(renderVerts[0], faceColor));
					allVerts.emplace_back(cVert(renderVerts[k - 1], faceColor));
					allVerts.emplace_back(cVert(renderVerts[k], faceColor));
				}
			}
		}
	}

	cVert* output = new cVert[allVerts.size()];
	std::copy(allVerts.begin(), allVerts.end(), output);

	cVert* wireOutput = new cVert[wireframeVerts.size()];
	std::copy(wireframeVerts.begin(), wireframeVerts.end(), wireOutput);

	if (allVerts.size() == 0 || wireframeVerts.size() == 0) {
		renderClip->clipnodeBuffer[hull] = NULL;
		renderClip->wireframeClipnodeBuffer[hull] = NULL;
		delete[] output;
		delete[] wireOutput;
		return;
	}

	renderClip->clipnodeBuffer[hull] = new VertexBuffer(g_app->colorShader, output, (int)allVerts.size(), GL_TRIANGLES);
	renderClip->clipnodeBuffer[hull]->ownData = true;

	renderClip->wireframeClipnodeBuffer[hull] = new VertexBuffer(g_app->colorShader, wireOutput, (int)wireframeVerts.size(), GL_LINES);
	renderClip->wireframeClipnodeBuffer[hull]->ownData = true;

	renderClip->faceMaths[hull] = std::move(navFaceMaths);
}


void BspRenderer::generateClipnodeBufferForHull(int modelIdx, int hullIdx)
{
	if (hullIdx < 0 || hullIdx > 3)
		return;

	BSPMODEL& model = map->models[modelIdx];
	Clipper clipper;

	vec3 min = vec3(model.nMins.x, model.nMins.y, model.nMins.z);
	vec3 max = vec3(model.nMaxs.x, model.nMaxs.y, model.nMaxs.z);

	while (modelIdx >= (int)renderClipnodes.size())
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

	if (hullIdx == 0 && clipnodesBufferCache.find(nodeIdx) != clipnodesBufferCache.end())
	{
		oldHullIdxStruct = clipnodesBufferCache[nodeIdx];
	}
	else if (hullIdx > 0 && nodesBufferCache.find(nodeIdx) != nodesBufferCache.end())
	{
		oldHullIdxStruct = nodesBufferCache[nodeIdx];
	}

	if (oldHullIdxStruct.modelIdx >= 0 && oldHullIdxStruct.hullIdx >= 0)
	{
		//return;
		/* // Instead of cache.... Just do nothing.
		* todo need rewrite cache?
		RenderClipnodes* cachedRenderClip = &renderClipnodes[oldHullIdxStruct.modelIdx];


		std::vector<FaceMath>& tfaceMaths = cachedRenderClip->faceMaths[oldHullIdxStruct.hullIdx];

		cVert* output = new cVert[cachedRenderClip->clipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts];
		memcpy(output, cachedRenderClip->clipnodeBuffer[oldHullIdxStruct.hullIdx]->data,
			cachedRenderClip->clipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts * sizeof(cVert));

		cVert* wireOutput = new cVert[cachedRenderClip->wireframeClipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts];
		memcpy(wireOutput, cachedRenderClip->wireframeClipnodeBuffer[oldHullIdxStruct.hullIdx]->data,
			cachedRenderClip->wireframeClipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts * sizeof(cVert));

		renderClip->clipnodeBuffer[hullIdx] = new VertexBuffer(g_app->colorShader, output,
			(GLsizei)cachedRenderClip->clipnodeBuffer[oldHullIdxStruct.hullIdx]->numVerts, GL_TRIANGLES);
		renderClip->clipnodeBuffer[hullIdx]->ownData = true;

		renderClip->wireframeClipnodeBuffer[hullIdx] = new VertexBuffer(g_app->colorShader,  wireOutput,




		return;*/
	}

	std::vector<NodeVolumeCuts> solidNodes = map->get_model_leaf_volume_cuts(modelIdx, hullIdx, CONTENTS_SOLID);
	//
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
			faceMath.fdist = getDistAlongAxis(faceMath.normal, faceVerts[0]);

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
			faceColor.a = (g_render_flags & RENDER_TRANSPARENT) ? 128 : 255;

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

	/*if (modelIdx > 0 && hullIdx == 0)
	{
		allVerts = scaleVerts(allVerts, 0.1f);
		wireframeVerts = scaleVerts(wireframeVerts, 0.1f);
	}*/

	cVert* output = new cVert[allVerts.size()];
	std::copy(allVerts.begin(), allVerts.end(), output);

	cVert* wireOutput = new cVert[wireframeVerts.size()];
	std::copy(wireframeVerts.begin(), wireframeVerts.end(), wireOutput);

	renderClip.clipnodeBuffer[hullIdx] = new VertexBuffer(g_app->colorShader, output, (int)(allVerts.size()), GL_TRIANGLES);
	renderClip.clipnodeBuffer[hullIdx]->ownData = true;
	renderClip.clipnodeBuffer[hullIdx]->frameId = 0;

	renderClip.wireframeClipnodeBuffer[hullIdx] = new VertexBuffer(g_app->colorShader, wireOutput, (int)(wireframeVerts.size()), GL_LINES);
	renderClip.wireframeClipnodeBuffer[hullIdx]->ownData = true;
	renderClip.wireframeClipnodeBuffer[hullIdx]->frameId = 0;

	nodeBuffStr curHullIdxStruct = nodeBuffStr();
	curHullIdxStruct.hullIdx = hullIdx;
	curHullIdxStruct.modelIdx = modelIdx;

	g_mutex_list[2].lock();
	if (hullIdx == 0)
	{
		clipnodesBufferCache[nodeIdx] = curHullIdxStruct;
	}
	else
	{
		nodesBufferCache[nodeIdx] = curHullIdxStruct;
	}
	g_mutex_list[2].unlock();
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
			g_mutex_list[2].lock();
			clipnodesBufferCache.erase(nodeIdx);
			g_mutex_list[2].unlock();
		}
		else if (hullIdx > 0 && nodesBufferCache.find(nodeIdx) != nodesBufferCache.end())
		{
			g_mutex_list[2].lock();
			nodesBufferCache.erase(nodeIdx);
			g_mutex_list[2].unlock();
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
	for (auto& clip : renderClipnodes)
	{
		for (int k = 0; k < MAX_MAP_HULLS; k++)
		{
			VertexBuffer* clipBuf = clip.clipnodeBuffer[k];
			if (clipBuf && clipBuf->get_data() && clipBuf->numVerts > 0)
			{
				cVert* vertData = (cVert*)clipBuf->get_data();
				for (int v = 0; v < clipBuf->numVerts; v++)
				{
					vertData[v].c.a = newValue;
				}
				clip.clipnodeBuffer[k]->uploaded = false;
			}
		}
	}
}

void BspRenderer::preRenderEnts()
{
	renderEnts.resize(map->ents.size(), RenderEnt());

	for (int i = 0; i < (int)map->ents.size(); i++)
	{
		refreshEnt(i);
	}
}

bool BspRenderer::setRenderAngles(const std::string& classname, mat4x4& outmat, vec3& outangles)
{
	if (classname.empty())
	{
		outmat.rotateY((outangles.y * (HL_PI / 180.0f)));
		outmat.rotateZ(-(outangles.x * (HL_PI / 180.0f)));
		outmat.rotateX((outangles.z * (HL_PI / 180.0f)));
		return false;
	}
	else
	{
		// based at cs 1.6 gamedll
		if (classname == "func_breakable")
		{
			outangles.y = 0.0f;
			outmat.rotateY(0.0f);
			outmat.rotateZ(-(outangles.x * (HL_PI / 180.0f)));
			outmat.rotateX((outangles.z * (HL_PI / 180.0f)));
		}
		else if (IsEntNotSupportAngles(classname))
		{
			outangles = vec3();
		}
		else if (classname == "env_sprite")
		{
			if (std::fabs(outangles.y) >= EPSILON && std::fabs(outangles.z) < EPSILON)
			{
				outangles.z = 0.0f;
				outmat.rotateY(0.0);
				outmat.rotateZ(-(outangles.x * (HL_PI / 180.0f)));
				outmat.rotateX((outangles.y * (HL_PI / 180.0f)));
			}
			else
			{
				outmat.rotateY((outangles.y * (HL_PI / 180.0f)));
				outmat.rotateZ(-(outangles.x * (HL_PI / 180.0f)));
				outmat.rotateX((outangles.z * (HL_PI / 180.0f)));
			}
		}
		else
		{
			bool foundAngles = false;
			for (const auto& prefix : g_settings.entsNegativePitchPrefix)
			{
				if (starts_with(classname, prefix))
				{
					outmat.rotateY((outangles.y * (HL_PI / 180.0f)));
					outmat.rotateZ((outangles.x * (HL_PI / 180.0f)));
					outmat.rotateX((outangles.z * (HL_PI / 180.0f)));
					foundAngles = true;
					break;
				}
			}
			if (!foundAngles)
			{
				outmat.rotateY((outangles.y * (HL_PI / 180.0f)));
				outmat.rotateZ(-(outangles.x * (HL_PI / 180.0f)));
				outmat.rotateX((outangles.z * (HL_PI / 180.0f)));
			}
		}
	}

	return !outangles.IsZero();
}

void BspRenderer::refreshEnt(int entIdx, int refreshFlags)
{
	if (entIdx >= (int)map->ents.size())
		return;

	if (renderEnts.size() != map->ents.size())
		renderEnts.resize(map->ents.size(), RenderEnt());

	int skin = -1;
	int sequence = -1;
	int body = -1;
	float scale = 1.0f;

	auto& rendEntity = renderEnts[entIdx];

	Entity* ent = map->ents[entIdx];

	if (refreshFlags & Entity_RefreshModel)
	{
		rendEntity.modelIdx = ent->getBspModelIdx();
		rendEntity.isDuplicateModel = false;

		if (rendEntity.modelIdx >= 0)
		{
			for (int i = 0; i < (int)map->ents.size(); i++)
			{
				if (i != entIdx)
				{
					if (map->ents[i]->getBspModelIdx() == rendEntity.modelIdx)
					{
						rendEntity.isDuplicateModel = true;
						break;
					}
				}
			}
		}
	}

	if (g_app->pointEntRenderer)
		rendEntity.pointEntCube = g_app->pointEntRenderer->getEntCube(ent);
	else
		rendEntity.pointEntCube = NULL;

	if (refreshFlags & Entity_RefreshAnglesOrigin)
	{
		vec3 origin = ent->origin;
		rendEntity.modelMat4x4.loadIdentity();
		rendEntity.modelMat4x4.translate(origin.x, origin.z, -origin.y);
		rendEntity.modelMat4x4_angles.loadIdentity();
		rendEntity.modelMat4x4_angles.translate(origin.x, origin.z, -origin.y);
		rendEntity.offset = origin;
	}

	bool setAngles = false;

	if (refreshFlags & Entity_RefreshAnglesOrigin)
	{
		rendEntity.angles = vec3();
		rendEntity.needAngles = false;
	}

	for (unsigned int i = 0; i < ent->keyOrder.size(); i++)
	{
		if (refreshFlags & Entity_RefreshAnglesOrigin)
		{
			if (ent->keyOrder[i] == "angles")
			{
				setAngles = true;
				rendEntity.angles = parseVector(ent->keyvalues["angles"]);
			}
			if (ent->keyOrder[i] == "angle")
			{
				setAngles = true;
				float y = str_to_float(ent->keyvalues["angle"]);

				if (y >= 0.0f)
				{
					rendEntity.angles.y = y;
				}
				else if (y == -1.0f)
				{
					rendEntity.angles.x = -90.0f;
					rendEntity.angles.y = 0.0f;
					rendEntity.angles.z = 0.0f;
				}
				else if (y <= -2.0f)
				{
					rendEntity.angles.x = 90.0f;
					rendEntity.angles.y = 0.0f;
					rendEntity.angles.z = 0.0f;
				}
			}
			if (ent->classname.size() && ent->classname.find("light") != std::string::npos && ent->keyOrder[i] == "pitch")
			{
				setAngles = true;
				float x = str_to_float(ent->keyvalues["pitch"]);
				rendEntity.angles.x = -x;
			}
		}
	}

	if (refreshFlags & Entity_RefreshOther)
	{
		if (ent->hasKey("scale") || g_app->fgd)
		{
			if (ent->hasKey("scale") && isFloating(ent->keyvalues["scale"]))
			{
				scale = str_to_float(ent->keyvalues["scale"]);
			}
			if (scale <= 0 && g_app->fgd)
			{
				FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->classname);
				if (fgdClass)
				{
					scale = fgdClass->scale;
				}
			}
		}
	}

	if (refreshFlags & Entity_RefreshSequence)
	{
		if (ent->hasKey("sequence") || g_app->fgd)
		{
			if (ent->hasKey("sequence") && isNumeric(ent->keyvalues["sequence"]))
			{
				sequence = str_to_int(ent->keyvalues["sequence"]);
			}
			if (sequence <= 0 && g_app->fgd)
			{
				FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->classname);
				if (fgdClass)
				{
					sequence = fgdClass->modelSequence;
				}
			}
		}
	}

	if (refreshFlags & Entity_RefreshBodySkin)
	{
		if (ent->hasKey("skin") || g_app->fgd)
		{
			if (ent->hasKey("skin") && isNumeric(ent->keyvalues["skin"]))
			{
				skin = str_to_int(ent->keyvalues["skin"]);
			}
			if (skin <= 0 && g_app->fgd)
			{
				FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->classname);
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
				body = str_to_int(ent->keyvalues["body"]);
			}
			if (body == 0 && g_app->fgd)
			{
				FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->classname);
				if (fgdClass)
				{
					body = fgdClass->modelBody;
				}
			}
		}
	}

	if (refreshFlags & Entity_RefreshModel)
	{
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
					FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->classname);
					if (fgdClass && !fgdClass->model.empty())
					{
						modelpath = fgdClass->model;
					}
				}

				if (rendEntity.mdlFileName.size() && !modelpath.size() || rendEntity.mdlFileName != modelpath)
				{
					rendEntity.mdlFileName = modelpath;
					std::string lowerpath = toLowerCase(modelpath);
					std::string newModelPath;
					if (ends_with(lowerpath, ".mdl"))
					{
						if (FindPathInAssets(map, modelpath, newModelPath))
						{
							rendEntity.mdl = AddNewModelToRender(newModelPath, body + sequence * 100 + skin * 1000);
							rendEntity.mdl->UpdateModelMeshList();
						}
						else
						{
							FindPathInAssets(map, modelpath, newModelPath, true);
							rendEntity.mdl = NULL;
						}
					}
					else
					{
						rendEntity.mdl = NULL;
						if (ends_with(lowerpath, ".spr"))
						{
							if (FindPathInAssets(map, modelpath, newModelPath))
							{
								if (rendEntity.pointEntCube && std::fabs(scale - 1.0f) < EPSILON)
								{
									rendEntity.spr = AddNewSpriteToRender(newModelPath, rendEntity.pointEntCube->mins, rendEntity.pointEntCube->maxs, 1.0f);
								}
								else
								{
									rendEntity.spr = AddNewSpriteToRender(newModelPath, scale);
								}
							}
							else
							{
								FindPathInAssets(map, modelpath, newModelPath, true);
								rendEntity.spr = NULL;
							}
						}
						else
						{
							rendEntity.spr = NULL;
						}
					}
				}
			}
			else if (g_app->fgd)
			{
				FgdClass* fgdClass = g_app->fgd->getFgdClass(ent->classname);
				if (fgdClass && fgdClass->isSprite && fgdClass->sprite.size())
				{
					rendEntity.spr = NULL;

					std::string lowerpath = toLowerCase(fgdClass->sprite);
					std::string newModelPath;
					if (ends_with(lowerpath, ".mdl"))
					{
						if (FindPathInAssets(map, fgdClass->sprite, newModelPath))
						{
							rendEntity.mdl = AddNewModelToRender(newModelPath, body + sequence * 100 + skin * 1000);
							rendEntity.mdl->UpdateModelMeshList();
						}
						else
						{
							FindPathInAssets(map, fgdClass->sprite, newModelPath, true);
							rendEntity.mdl = NULL;
						}
					}
					else
					{
						rendEntity.mdl = NULL;
						if (ends_with(lowerpath, ".spr"))
						{
							if (FindPathInAssets(map, fgdClass->sprite, newModelPath))
							{
								if (rendEntity.pointEntCube && std::fabs(scale - 1.0f) < EPSILON)
								{
									rendEntity.spr = AddNewSpriteToRender(newModelPath, rendEntity.pointEntCube->mins, rendEntity.pointEntCube->maxs, 1.0f);
								}
								else
								{
									rendEntity.spr = AddNewSpriteToRender(newModelPath, scale);
								}
							}
							else
							{
								FindPathInAssets(map, fgdClass->sprite, newModelPath, true);
								rendEntity.spr = NULL;
							}
						}
						else
						{
							rendEntity.spr = NULL;
						}
					}
				}
				else
				{
					fgdClass = g_app->fgd->getFgdClass(ent->classname);
					if (fgdClass && !fgdClass->model.empty())
					{
						std::string lowerpath = toLowerCase(fgdClass->model);
						std::string newModelPath;
						if (ends_with(lowerpath, ".mdl"))
						{
							if (FindPathInAssets(map, fgdClass->model, newModelPath))
							{
								rendEntity.mdl = AddNewModelToRender(newModelPath, body + sequence * 100 + skin * 1000);
								rendEntity.mdl->UpdateModelMeshList();
							}
							else
							{
								FindPathInAssets(map, fgdClass->model, newModelPath, true);
								rendEntity.mdl = NULL;
							}
						}
						else
						{
							rendEntity.mdl = NULL;
							if (ends_with(lowerpath, ".spr"))
							{
								if (FindPathInAssets(map, fgdClass->model, newModelPath))
								{
									if (rendEntity.pointEntCube && std::fabs(scale - 1.0f) < EPSILON)
									{
										rendEntity.spr = AddNewSpriteToRender(newModelPath, rendEntity.pointEntCube->mins, rendEntity.pointEntCube->maxs, 1.0f);
									}
									else
									{
										rendEntity.spr = AddNewSpriteToRender(newModelPath, scale);
									}
								}
								else
								{
									FindPathInAssets(map, fgdClass->model, newModelPath, true);
									rendEntity.spr = NULL;
								}
							}
							else
							{
								rendEntity.spr = NULL;
							}
						}
					}
				}
			}
		}
	}

	if (refreshFlags & Entity_RefreshBodySkin)
	{
		if (body != -1)
		{
			if (rendEntity.mdl && rendEntity.mdl->GetBody() != body && rendEntity.mdl->m_pstudiohdr)
			{
				rendEntity.mdl->SetBody(body);
			}
		}
		if (skin != -1)
		{
			if (rendEntity.mdl && rendEntity.mdl->GetSkin() != skin)
			{
				rendEntity.mdl->SetSkin(skin);
			}
		}
	}

	if (refreshFlags & Entity_RefreshSequence)
	{
		if (sequence != -1)
		{
			if (rendEntity.mdl && rendEntity.mdl->GetSequence() != sequence)
			{
				rendEntity.mdl->SetSequence(sequence);
			}
		}
	}

	if (setAngles)
	{
		rendEntity.needAngles = setRenderAngles(ent->classname, rendEntity.modelMat4x4_angles, rendEntity.angles);
	}
}

void BspRenderer::calcFaceMaths()
{
	deleteFaceMaths();

	while ((int)faceMaths.size() < map->faceCount)
	{
		faceMaths.emplace_back(FaceMath());
	}
	while ((int)faceMaths.size() > map->faceCount)
	{
		faceMaths.pop_back();
	}

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
	if (faceIdx < 0 || faceIdx >= (int)faceMaths.size())
	{
		return;
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

	vec3 v1 = vec3();
	for (int e = 0; e < face.nEdges; e++)
	{
		int edgeIdx = map->surfedges[face.iFirstEdge + e];
		BSPEDGE32& edge = map->edges[abs(edgeIdx)];
		int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];
		allVerts[e] = map->verts[vertIdx];

		// 2 verts can share the same position on a face, so need to find one that isn't shared (aomdc_1intro)
		if (e > 0 && allVerts[e] != allVerts[0])
		{
			v1 = allVerts[e];
		}
	}

	if (allVerts.size() == 0)
	{
		// error ?
		allVerts.emplace_back(vec3());
	}

	faceMath.center = getCentroid(allVerts);

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

	delete[] lightmaps;

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
	g_mutex_list[2].lock();
	clipnodesBufferCache.clear();
	nodesBufferCache.clear();
	g_mutex_list[2].unlock();

	if (g_app->SelectedMap == map)
		g_app->selectMap(NULL);
	map->setBspRender(NULL);

	delete map;
	map = NULL;
}

void BspRenderer::reuploadTextures()
{
	if (!glTexturesSwap.size())
	{
		loadTextures();
	}

	if (!glTexturesSwap.size())
		return;

	deleteTextures();

	//loadTextures();

	glTextures = glTexturesSwap;
	glTexturesSwap.clear();

	for (size_t i = 0; i < glTextures.size(); i++)
	{
		for (auto& tex : glTextures[i])
			tex->upload();
	}

	texturesLoaded = true;

	needReloadDebugTextures = true;
}

void BspRenderer::delayLoadData()
{
	if (!lightmapsUploaded && lightmapFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		for (int i = 0; i < glLightmapTextures.size(); i++)
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
		preRenderFaces();
		texturesLoaded = true;
	}

	if (!clipnodesLoaded && clipnodesFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		if (renderClipnodes.size())
		{
			for (auto& clip : renderClipnodes)
			{
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

void BspRenderer::highlightFace(int faceIdx, int highlight, bool reupload)
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
		r = rgroup->special ? 2.0f : 0.15f;
		g = 0.0f;
		b = 0.0f;
	}

	if (highlight == 2)
	{
		r = rgroup->special ? 3.0f : 0.0f;
		g = 0.0f;
		b = 0.15f;
	}

	if (highlight == 3)
	{
		r = rgroup->special ? 4.0f : 0.0f;
		g = 0.15f;
		b = 0.15f;
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

	if (modelIdx <= -1)
	{
		return false;
	}

	int relativeFaceIdx = faceIdx - map->models[modelIdx].iFirstFace;
	*renderFace = &renderModels[modelIdx]->renderFaces[relativeFaceIdx];
	*renderGroup = &renderModels[modelIdx]->renderGroups[(*renderFace)->group];

	return true;
}

unsigned int BspRenderer::getFaceTextureId(int faceIdx)
{
	BSPFACE32& face = map->faces[faceIdx];
	BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
	if (texinfo.iMiptex < 0 || texinfo.iMiptex >= map->textureCount || glTextures[texinfo.iMiptex].empty())
		return missingTex->id;
	return glTextures[texinfo.iMiptex][0]->id;
}

void BspRenderer::render(bool modelVertsDraw, int clipnodeHull)
{
	mapOffset = map->ents.size() ? map->ents[0]->origin : vec3();
	renderOffset = mapOffset.flip();
	localCameraOrigin = cameraOrigin - mapOffset;

	if (delayEntUndo)
	{
		delayEntUndo = false;
		pushUndoState(delayEntUndoDesc, FL_ENTITIES);
	}

	g_app->matmodel.loadIdentity();
	g_app->matmodel.translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_app->colorShader->updateMatrixes();

	g_app->matmodel.loadIdentity();
	g_app->matmodel.translate(renderOffset.x, renderOffset.y, renderOffset.z);
	g_app->bspShader->updateMatrixes();

	static double leafUpdTime = 0.0;

	if (map->models && fabs(g_app->curTime - leafUpdTime) > 0.25)
	{
		leafUpdTime = g_app->curTime;
		std::vector<int> nodeBranch;
		int childIdx = -1;
		int headNode = map->models[0].iHeadnodes[0];
		int hull = g_app->clipnodeRenderHull;
		if (hull < 0)
			hull = 0;
		map->pointContents(headNode, localCameraOrigin, hull, nodeBranch, curLeafIdx, childIdx);

		if (g_app->pickMode == PICK_FACE_LEAF && hull == 0)
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
		size_t ent_count = std::min(map->ents.size(), renderEnts.size());

		for (size_t i = 0; i < ent_count; i++)
		{
			RenderEnt& ent = renderEnts[i];
			ent.modelMat4x4_calc = ent.modelMat4x4;
			ent.modelMat4x4_calc.translate(renderOffset.x, renderOffset.y, renderOffset.z);
			ent.modelMat4x4_calc_angles = ent.modelMat4x4_angles;
			ent.modelMat4x4_calc_angles.translate(renderOffset.x, renderOffset.y, renderOffset.z);
		}
	}

	std::vector<int> highlightEnts = g_app->pickInfo.selectedEnts;


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

			if (!map->ents.empty() && !map->ents[0]->hide)
				drawModel(0, pass, false, false);

			size_t ent_count = std::min(map->ents.size(), renderEnts.size());
			for (int i = 0; i < (int)ent_count; i++)
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
			int entCount = (int)std::min(map->ents.size(), renderEnts.size());

			for (int i = 0; i < entCount; i++)
			{
				if (map->ents[i]->hide)
					continue;
				if (renderEnts[i].modelIdx > 0 && renderEnts[i].modelIdx < map->modelCount)
				{
					if (clipnodeHull <= -1 && renderModels[renderEnts[i].modelIdx]->renderGroups.size())
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
			for (int highlightEnt : highlightEnts)
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



	if (g_app->pickMode == PICK_FACE_LEAF && !ortho_overview && !make_screenshot)
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
	if (hullIdx <= -1)
	{
		hullIdx = getBestClipnodeHull(modelIdx);
	}

	if (hullIdx <= -1 || hullIdx > 3 || ortho_overview || make_screenshot)
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

	if (oldHullIdxStruct.hullIdx >= 0 && oldHullIdxStruct.modelIdx >= 0)
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
		if (modelIdx < (int)renderClipnodes.size())
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

	if (modelIdx < 0 || modelIdx >= (int)renderModels.size())
	{
		return;
	}

	if (pass == REND_PASS_COLORSHADER)
	{
		RenderModel* rend_mdl = renderModels[modelIdx];
		if (rend_mdl->wireframeBuffer && !ortho_overview && !make_screenshot)
		{
			if (ent && ent->isDuplicateModel)
				rend_mdl->wireframeBuffer->frameId--;

			if (highlight || (g_render_flags & RENDER_WIREFRAME))
			{
				if (highlight && !rend_mdl->highlighted)
				{
					rend_mdl->highlighted = true;
					auto wireframeVerts = (cVert*)rend_mdl->wireframeBuffer->get_data();
					for (int n = 0; n < rend_mdl->wireframeBuffer->numVerts; n++)
					{
						wireframeVerts[n].c = COLOR4(245, 212, 66, 255);
					}
				}
				else if (!highlight && rend_mdl->highlighted)
				{
					rend_mdl->highlighted = false;
					auto wireframeVerts = (cVert*)rend_mdl->wireframeBuffer->get_data();
					if (modelIdx > 0)
					{
						for (int n = 0; n < rend_mdl->wireframeBuffer->numVerts; n++)
						{
							wireframeVerts[n].c = COLOR4(0, 100, 255, 255);
						}
					}
					else
					{
						for (int n = 0; n < rend_mdl->wireframeBuffer->numVerts; n++)
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
					rend_mdl->wireframeBuffer->drawFull();
				}
				else if (ent && ent->needAngles)
				{
					if (!highlight)
					{
						glLineWidth(std::min(g_app->lineWidthRange[1], 2.5f));
						g_app->matmodel = ent->modelMat4x4_calc;
						g_app->colorShader->updateMatrixes();
						rend_mdl->wireframeBuffer->drawFull();
						rend_mdl->wireframeBuffer->frameId--;
						glLineWidth(1.3f);
					}
					else
					{
						g_app->matmodel = ent->modelMat4x4_calc;
						g_app->colorShader->updateMatrixes();
						rend_mdl->wireframeBuffer->drawFull();
					}
				}
				else
				{
					if (ent)
					{
						g_app->matmodel = ent->modelMat4x4_calc;
						g_app->colorShader->updateMatrixes();
					}
					rend_mdl->wireframeBuffer->drawFull();
				}

				g_app->colorShader->popMatrix();
			}
		}

	}

	for (auto& rgroup : renderModels[modelIdx]->renderGroups)
	{
		if (rgroup.special)
		{
			if (ortho_overview | make_screenshot)
				continue;

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

				if (texturesLoaded && g_render_flags & RENDER_TEXTURES && !rgroup.textures.empty())
				{
					if (rgroup.textures.size() > 1)
					{
						if (std::abs(g_app->curTime - rgroup.frametime) > 0.1f)
						{
							rgroup.frametime = g_app->curTime;
							rgroup.frameid++;
							if (rgroup.frameid >= (int)rgroup.textures.size())
							{
								rgroup.frameid = 0;
							}
						}
					}


					rgroup.textures[rgroup.frameid]->bind(0);
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


				if (g_app->pickMode != PICK_OBJECT && highlight && !ortho_overview && !make_screenshot)
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
					if (highlight && !ortho_overview && !make_screenshot)
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

void BspRenderer::drawPointEntities(std::vector<int> highlightEnts, int pass)
{
	// skip worldspawn
	bool isAltPressed = g_app->pressed[GLFW_KEY_LEFT_ALT] || g_app->pressed[GLFW_KEY_RIGHT_ALT];

	g_app->modelShader->pushMatrix();
	g_app->colorShader->pushMatrix();

	size_t ent_count = std::min(map->ents.size(), renderEnts.size());

	for (int i = 1; i < (int)ent_count; i++)
	{
		if (renderEnts[i].modelIdx >= 0)
			continue;

		Entity* mapEnt = map->ents[i];

		if (mapEnt->hide)
			continue;

		if (ortho_overview || make_screenshot)
		{
			if (!starts_with(mapEnt->classname, "cycler_") &&
				!starts_with(mapEnt->classname, "func_"))
			{
				continue;
			}
		}

		if (g_app->pickInfo.IsSelectedEnt(i) && !ortho_overview && !make_screenshot)
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
					else if (renderEnts[i].spr && !ortho_overview)
					{
						renderEnts[i].spr->DrawSprite();
					}
				}
				else if (pass == REND_PASS_COLORSHADER && !ortho_overview && !make_screenshot)
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
				if (pass == REND_PASS_COLORSHADER && !ortho_overview && !make_screenshot)
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
					else if (renderEnts[i].spr && !ortho_overview)
					{
						renderEnts[i].spr->DrawSprite();

						if (isAltPressed)
						{
							renderEnts[i].spr->DrawAxes();
							renderEnts[i].spr->DrawBBox();
						}

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
				if (pass == REND_PASS_COLORSHADER && !ortho_overview && !make_screenshot)
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

		auto& rendEntity = renderEnts[i];

		if (rendEntity.modelIdx >= 0 && rendEntity.modelIdx < map->modelCount)
		{
			bool isSpecial = false;

			for (auto& rgroup : renderModels[rendEntity.modelIdx]->renderGroups)
			{
				if (rgroup.special)
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

			if (pickModelPoly(start, dir, rendEntity.offset, rendEntity.modelIdx, hullIdx, tempPickInfo))
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
			if (g_render_flags & RENDER_MODELS && rendEntity.mdl)
			{
				//rendEntity.mdl->ExtractBBox(mins, maxs);
				mins = rendEntity.offset + rendEntity.mdl->mins;
				maxs = rendEntity.offset + rendEntity.mdl->maxs;

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
			if (g_render_flags & RENDER_MODELS && rendEntity.spr)
			{
				auto& group = rendEntity.spr->sprite_groups[rendEntity.spr->current_group];

				mins = rendEntity.offset + group.sprites[group.current_spr].spriteCube->mins;
				maxs = rendEntity.offset + group.sprites[group.current_spr].spriteCube->maxs;

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
			mins = rendEntity.offset + rendEntity.pointEntCube->mins;
			maxs = rendEntity.offset + rendEntity.pointEntCube->maxs;
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
		if (model.iFirstFace + k >= (int)faceMaths.size())
		{
			calcFaceMaths();
			break;
		}

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
				vec3& v = edgeIdx > 0 ? map->verts[edge.iVertex[0]] : map->verts[edge.iVertex[1]];
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

	if (hullIdx <= -1 && modelIdx >= 0 && modelIdx < map->modelCount && renderModels[modelIdx]->renderGroups.empty())
	{
		// clipnodes are visible for this model because it has no faces
		hullIdx = getBestClipnodeHull(modelIdx);
	}

	if (clipnodesLoaded && (selectWorldClips || selectEntClips) && hullIdx >= 0 && modelIdx >= 0 && modelIdx < map->modelCount)
	{
		int nodeIdx = map->models[modelIdx].iHeadnodes[hullIdx];
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

		if (oldHullIdxStruct.modelIdx < 0 || oldHullIdxStruct.hullIdx < 0)
		{
			oldHullIdxStruct.modelIdx = modelIdx;
			oldHullIdxStruct.hullIdx = hullIdx;
			generateClipnodeBufferForHull(modelIdx, hullIdx);
		}
		for (int i = 0; i < (int)renderClipnodes[oldHullIdxStruct.modelIdx].faceMaths[oldHullIdxStruct.hullIdx].size(); i++)
		{
			FaceMath& faceMath = renderClipnodes[oldHullIdxStruct.modelIdx].faceMaths[oldHullIdxStruct.hullIdx][i];

			float t = tempPickInfo.bestDist;
			if (pickFaceMath(start, dir, faceMath, t))
			{
				foundBetterPick = true;
				tempPickInfo.bestDist = t;
				tempPickInfo.selectedFaces.clear();


				// Nav mesh WIP code
				if (g_app->debugNavMesh && modelIdx == 0 && hullIdx == 3) {
					static int lastPick = 0;

					g_app->debugPoly = debugFaces[i];
					g_app->debugNavPoly = i;

					//Polygon3D merged = debugFaces[lastPick].merge(debugFaces[i]);
					//vector<vector<vec3>> split = debugFaces[i].split(debugFaces[lastPick]);
					//logf("split %d by %d == %d\n", i, lastPick, split.size());

					NavNode& node = g_app->debugNavMesh->nodes[i];

					lastPick = i;
					print_log("Picked hull {}, face {}, verts {}, area {}\nNav links {}\n", hullIdx, i, debugFaces[i].verts.size(), debugFaces[i].area, node.numLinks());
				}

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
	if (t < EPSILON2 || t > bestDist)
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

	intersectVec = intersection;
	intersectDist = bestDist = t;

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

void BspRenderer::pushUndoState(const std::string& actionDesc, unsigned int targets)
{
	if (g_settings.verboseLogs)
		print_log("SAVE MODEL STATES TO BACKUP\n");
	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0290));
		return;
	}

	LumpState newLumps = map->duplicate_lumps(targets);
	LumpState oldLumps = undoLumpState;

	bool differences[HEADER_LUMPS] = { false };

	unsigned int targetLumps = 0;

	bool anyDifference = false;
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (targets & (1 << i))
		{
			if (newLumps.lumps[i].size() != oldLumps.lumps[i].size() ||
				!std::equal(newLumps.lumps[i].begin(), newLumps.lumps[i].end(),
					oldLumps.lumps[i].begin()))
			{
				anyDifference = true;
				differences[i] = true;
				if (g_settings.verboseLogs)
				{
					print_log(get_localized_string(LANG_0291), g_lump_names[i], oldLumps.lumps[i].size(), newLumps.lumps[i].size());
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
			oldLumps.lumps[i].clear();
			newLumps.lumps[i].clear();
		}
	}


	EditBspCommand* editCommand = new EditBspCommand(actionDesc, oldLumps, newLumps, targetLumps);
	pushUndoCommand(editCommand);

	if (differences[LUMP_ENTITIES])
	{
		preRenderEnts();
	}
}

void BspRenderer::pushUndoCommand(EditBspCommand* cmd)
{
	clearRedoCommands();

	while (!undoHistory.empty() && (int)undoHistory.size() >= g_settings.undoLevels)
	{
		delete undoHistory[0];
		undoHistory.erase(undoHistory.begin());
	}

	undoHistory.push_back(cmd);
	cmd->execute();

	undoLumpState = map->duplicate_lumps();
	calcUndoMemoryUsage();
}

void BspRenderer::undo()
{
	if (undoHistory.empty())
	{
		return;
	}

	EditBspCommand* undoCommand = undoHistory.back();
	if (g_app->isLoading)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0293), undoCommand->desc);
		return;
	}

	undoCommand->undo();
	undoHistory.pop_back();
	redoHistory.push_back(undoCommand);
	g_app->updateEnts();

	undoLumpState = map->duplicate_lumps();
	calcUndoMemoryUsage();
}

void BspRenderer::redo()
{
	if (redoHistory.empty())
	{
		return;
	}

	EditBspCommand* redoCommand = redoHistory.back();
	if (g_app->isLoading)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0294), redoCommand->desc);
		return;
	}

	redoCommand->execute();
	redoHistory.pop_back();
	undoHistory.push_back(redoCommand);
	g_app->updateEnts();

	undoLumpState = map->duplicate_lumps();
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

void BspRenderer::clearUndoCommands()
{
	for (size_t i = 0; i < undoHistory.size(); i++)
	{
		delete undoHistory[i];
	}

	undoHistory.clear();
	calcUndoMemoryUsage();
}

void BspRenderer::calcUndoMemoryUsage()
{
	undoMemoryUsageZip = (undoHistory.size() + redoHistory.size()) * sizeof(EditBspCommand*);
	undoMemoryUsage = undoMemoryUsageZip;

	for (size_t i = 0; i < undoHistory.size(); i++)
	{
		undoMemoryUsageZip += undoHistory[i]->memoryUsageZip();
		undoMemoryUsage += undoHistory[i]->memoryUsage();
	}
	for (size_t i = 0; i < redoHistory.size(); i++)
	{
		undoMemoryUsageZip += redoHistory[i]->memoryUsageZip();
		undoMemoryUsageZip += redoHistory[i]->memoryUsage();
	}
}

void BspRenderer::clearDrawCache()
{
	drawedClipnodes.clear();
	drawedNodes.clear();
}

void BspRenderer::pushEntityUndoStateDelay(const std::string& desc)
{
	delayEntUndo = true;
	delayEntUndoDesc = desc;
}

PickInfo::PickInfo()
{
	selectedEnts.clear();
	selectedFaces.clear();
	bestDist = 0.0f;
}

void PickInfo::AddSelectedEnt(int entIdx)
{
	if (entIdx < 0xFFFFFFu && !IsSelectedEnt(entIdx))
	{
		selectedEnts.push_back(entIdx);
	}
	pickCount++;
}

void PickInfo::SetSelectedEnt(int entIdx)
{
	selectedEnts.clear();
	AddSelectedEnt(entIdx);
}

void PickInfo::DelSelectedEnt(int entIdx)
{
	auto it = std::find(selectedEnts.begin(), selectedEnts.end(), entIdx);
	if (it != selectedEnts.end())
	{
		pickCount++;
		selectedEnts.erase(it);
	}
}

bool PickInfo::IsSelectedEnt(int entIdx)
{
	return selectedEnts.size() && std::find(selectedEnts.begin(), selectedEnts.end(), entIdx) != selectedEnts.end();
}

