#include "lang.h"
#include "Gui.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Settings.h"
#include "Renderer.h"
#include <lodepng.h>
#include <algorithm>
#include <string>
#include "BspMerger.h"
#include "filedialog/ImFileDialog.h"
// embedded binary data

#include "fonts/rusfont.h"
#include "fonts/robotomono.h"
#include "fonts/robotomedium.h"
#include "icons/object.h"
#include "icons/face.h"
#include "imgui_stdlib.h"
#include "quantizer.h"
#include <execution>
#include "vis.h"

float g_tooltip_delay = 0.6f; // time in seconds before showing a tooltip

bool filterNeeded = true;

std::string iniPath;

Gui::Gui(Renderer* app)
{
	guiHoverAxis = 0;
	this->app = app;
}

void Gui::init()
{
	iniPath = g_config_dir + "imgui.ini";
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	imgui_io = &ImGui::GetIO();

	imgui_io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//imgui_io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	imgui_io->IniFilename = !g_settings.save_windows ? NULL : iniPath.c_str();

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();

	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForOpenGL(app->window, true);
	ImGui_ImplOpenGL3_Init("#version 130");
	// ImFileDialog requires you to set the CreateTexture and DeleteTexture
	ifd::FileDialog::Instance().CreateTexture = [](unsigned char* data, int w, int h, char fmt) -> void* {
		GLuint tex;

		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, (fmt == 0) ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, data);
		glBindTexture(GL_TEXTURE_2D, 0);
		return (void*)(uint64_t)tex;
		};
	ifd::FileDialog::Instance().DeleteTexture = [](void* tex) {
		GLuint texID = (GLuint)((uintptr_t)tex);
		glDeleteTextures(1, &texID);
		};

	loadFonts();

	imgui_io->ConfigWindowsMoveFromTitleBarOnly = true;

	// load icons
	unsigned char* icon_data = NULL;
	unsigned int w, h;

	lodepng_decode32(&icon_data, &w, &h, object_icon, sizeof(object_icon));
	objectIconTexture = new Texture(w, h, icon_data, "objIcon");
	objectIconTexture->upload(GL_RGBA);
	lodepng_decode32(&icon_data, &w, &h, face_icon, sizeof(face_icon));
	faceIconTexture = new Texture(w, h, icon_data, "faceIcon");
	faceIconTexture->upload(GL_RGBA);
}

ImVec4 imguiColorFromConsole(unsigned short colors)
{
	bool intensity = (colors & PRINT_INTENSITY) != 0;
	float red = (colors & PRINT_RED) ? (intensity ? 1.0f : 0.5f) : 0.0f;
	float green = (colors & PRINT_GREEN) ? (intensity ? 1.0f : 0.5f) : 0.0f;
	float blue = (colors & PRINT_BLUE) ? (intensity ? 1.0f : 0.5f) : 0.0f;
	return ImVec4(red, green, blue, 1.0f);
}

void Gui::draw()
{
	Bsp* map = app->getSelectedMap();
	// Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGui::PushFont(defaultFont);
	drawMenuBar();

	drawFpsOverlay();
	drawToolbar();
	drawStatusMessage();

	if (showDebugWidget)
	{
		drawDebugWidget();
	}
	if (showKeyvalueWidget)
	{
		drawKeyvalueEditor();
	}
	if (showTextureBrowser)
	{
		drawTextureBrowser();
	}
	if (showTransformWidget)
	{
		drawTransformWidget();
	}
	if (showLogWidget)
	{
		drawLog();
	}
	if (showSettingsWidget)
	{
		drawSettings();
	}
	if (showHelpWidget)
	{
		drawHelp();
	}
	if (showAboutWidget)
	{
		drawAbout();
	}
	if (showImportMapWidget)
	{
		drawImportMapWidget();
	}
	if (showMergeMapWidget)
	{
		drawMergeWindow();
	}
	if (showLimitsWidget)
	{
		drawLimits();
	}
	if (showFaceEditWidget)
	{
		drawFaceEditorWidget();
	}
	if (showLightmapEditorWidget)
	{
		drawLightMapTool();
	}
	if (showEntityReport)
	{
		drawEntityReport();
	}
	if (showGOTOWidget)
	{
		drawGOTOWidget();
	}
	if (map && map->is_mdl_model)
	{
		drawMDLWidget();
	}

	if (app->pickMode == PICK_OBJECT)
	{
		if (contextMenuEnt != -1)
		{
			ImGui::OpenPopup(get_localized_string(LANG_0427).c_str());
			contextMenuEnt = -1;
		}
		if (emptyContextMenu)
		{
			emptyContextMenu = 0;
			ImGui::OpenPopup(get_localized_string(LANG_0428).c_str());
		}
	}
	else
	{
		if (contextMenuEnt != -1 || emptyContextMenu)
		{
			emptyContextMenu = 0;
			contextMenuEnt = -1;
			ImGui::OpenPopup(get_localized_string(LANG_0429).c_str());
		}
	}

	draw3dContextMenus();

	ImGui::PopFont();

	// Rendering
	glViewport(0, 0, app->windowWidth, app->windowHeight);
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());


	if (shouldReloadFonts)
	{
		shouldReloadFonts = false;

		ImGui_ImplOpenGL3_DestroyFontsTexture();
		imgui_io->Fonts->Clear();

		loadFonts();

		imgui_io->Fonts->Build();
		ImGui_ImplOpenGL3_CreateFontsTexture();
	}
}

void Gui::openContextMenu(int entIdx)
{
	if (entIdx < 0)
	{
		emptyContextMenu = 1;
	}
	contextMenuEnt = entIdx;
}

void Gui::copyTexture()
{
	Bsp* map = app->getSelectedMap();
	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0313));
		return;
	}
	else if (app->pickInfo.selectedFaces.size() == 0 || app->pickInfo.selectedFaces.size() > 1)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0314));
		return;
	}
	BSPTEXTUREINFO& texinfo = map->texinfos[map->faces[app->pickInfo.selectedFaces[0]].iTextureInfo];
	copiedMiptex = texinfo.iMiptex == -1 || texinfo.iMiptex >= map->textureCount ? 0 : texinfo.iMiptex;
}

void Gui::pasteTexture()
{
	refreshSelectedFaces = true;
}

void Gui::copyLightmap()
{
	Bsp* map = app->getSelectedMap();

	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1049));
		return;
	}
	else if (app->pickInfo.selectedFaces.size() == 0 || app->pickInfo.selectedFaces.size() > 1)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1050));
		return;
	}

	copiedLightmapFace = app->pickInfo.selectedFaces[0];

	int size[2];
	GetFaceLightmapSize(map, app->pickInfo.selectedFaces[0], size);
	copiedLightmap.width = size[0];
	copiedLightmap.height = size[1];
	copiedLightmap.layers = map->lightmap_count(app->pickInfo.selectedFaces[0]);
	//copiedLightmap.luxelFlags = new unsigned char[size[0] * size[1]];
	//qrad_get_lightmap_flags(map, app->pickInfo.faceIdx, copiedLightmap.luxelFlags);
}

void Gui::pasteLightmap()
{
	Bsp* map = app->getSelectedMap();
	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1149));
		return;
	}
	else if (app->pickInfo.selectedFaces.size() == 0 || app->pickInfo.selectedFaces.size() > 1)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1150));
		return;
	}
	int faceIdx = app->pickInfo.selectedFaces[0];

	int size[2];
	GetFaceLightmapSize(map, faceIdx, size);

	LIGHTMAP dstLightmap = LIGHTMAP();
	dstLightmap.width = size[0];
	dstLightmap.height = size[1];
	dstLightmap.layers = map->lightmap_count(faceIdx);

	if (dstLightmap.width != copiedLightmap.width || dstLightmap.height != copiedLightmap.height)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, "WARNING: lightmap sizes don't match ({}x{} != {}{})",
			copiedLightmap.width,
			copiedLightmap.height,
			dstLightmap.width,
			dstLightmap.height);
		// TODO: resize the lightmap, or maybe just shift if the face is the same size
	}

	BSPFACE32& src = map->faces[copiedLightmapFace];
	BSPFACE32& dst = map->faces[faceIdx];
	dst.nLightmapOffset = src.nLightmapOffset;
	memcpy(dst.nStyles, src.nStyles, 4);

	map->getBspRender()->reloadLightmaps();
}

void ExportModel(Bsp* src_map, int id, int ExportType, bool movemodel)
{
	print_log(get_localized_string(LANG_0315));
	src_map->update_ent_lump();
	src_map->update_lump_pointers();
	src_map->write(src_map->bsp_path + ".tmp.bsp");

	print_log(get_localized_string(LANG_0316));

	Bsp* tmpMap = new Bsp(src_map->bsp_path + ".tmp.bsp");

	tmpMap->force_skip_crc = true;

	if (ExportType == 1)
	{
		tmpMap->is_bsp29 = true;
		tmpMap->bsp_header.nVersion = 29;
	}
	else
	{
		tmpMap->is_bsp29 = false;
		tmpMap->bsp_header.nVersion = 30;
	}

	print_log(get_localized_string(LANG_0317));
	removeFile(src_map->bsp_path + ".tmp.bsp");

	vec3 modelOrigin = tmpMap->get_model_center(id);

	BSPMODEL tmpModel = src_map->models[id];

	while (tmpMap->modelCount < 2)
	{
		print_log(get_localized_string(LANG_0318));
		tmpMap->create_model();
	}

	tmpMap->models[1] = tmpModel;
	print_log(get_localized_string(LANG_0319));
	tmpMap->models[0] = tmpModel;
	tmpMap->models[0].nVisLeafs = 0;
	tmpMap->models[0].iHeadnodes[0] = tmpMap->models[0].iHeadnodes[1] =
		tmpMap->models[0].iHeadnodes[2] = tmpMap->models[0].iHeadnodes[3] = CONTENTS_EMPTY;

	for (int i = 1; i < tmpMap->ents.size(); i++)
	{
		delete tmpMap->ents[i];
	}
	print_log(get_localized_string(LANG_0320));

	Entity* tmpEnt = new Entity("worldspawn");
	Entity* tmpEnt2 = new Entity("func_wall");

	tmpEnt->setOrAddKeyvalue("compiler", g_version_string);
	tmpEnt->setOrAddKeyvalue("message", "bsp model");

	tmpEnt2->setOrAddKeyvalue("model", "*1");

	print_log(get_localized_string(LANG_0321));
	tmpMap->modelCount = 2;
	tmpMap->lumps[LUMP_MODELS] = (unsigned char*)tmpMap->models;
	tmpMap->bsp_header.lump[LUMP_MODELS].nLength = sizeof(BSPMODEL) * 2;
	tmpMap->ents.clear();

	tmpMap->ents.push_back(tmpEnt);
	tmpMap->ents.push_back(tmpEnt2);

	tmpMap->update_ent_lump();
	tmpMap->update_lump_pointers();

	print_log(get_localized_string(LANG_0322));
	STRUCTCOUNT removed = tmpMap->remove_unused_model_structures(CLEAN_LIGHTMAP | CLEAN_PLANES | CLEAN_NODES | CLEAN_CLIPNODES | CLEAN_CLIPNODES_SOMETHING | CLEAN_LEAVES | CLEAN_FACES | CLEAN_SURFEDGES | CLEAN_TEXINFOS |
		CLEAN_EDGES | CLEAN_VERTICES | CLEAN_TEXTURES | CLEAN_VISDATA);
	if (!removed.allZero())
		removed.print_delete_stats(1);

	print_log(get_localized_string(LANG_0323));
	tmpMap->modelCount = 1;
	tmpMap->models[0] = tmpMap->models[1];
	tmpMap->lumps[LUMP_MODELS] = (unsigned char*)tmpMap->models;
	tmpMap->bsp_header.lump[LUMP_MODELS].nLength = sizeof(BSPMODEL);

	print_log(get_localized_string(LANG_0324));
	tmpMap->ents.clear();
	tmpMap->ents.push_back(tmpEnt);
	tmpMap->update_ent_lump();
	tmpMap->update_lump_pointers();

	print_log(get_localized_string(LANG_0325));

	/*int markid = 0;
	for (int i = 0; i < tmpMap->leafCount; i++)
	{
		BSPLEAF32& tmpLeaf = tmpMap->leaves[i];
		if (tmpLeaf.nMarkSurfaces > 0)
		{
			tmpLeaf.iFirstMarkSurface = markid;
			markid += tmpLeaf.nMarkSurfaces;
		}
	}*/

	//tmpMap->models[0].nVisLeafs = tmpMap->leafCount - 1;

	if (movemodel)
		tmpMap->move(-modelOrigin, 0, true, true);


	tmpMap->update_lump_pointers();

	print_log(get_localized_string(LANG_0326));
	remove_unused_wad_files(src_map, tmpMap, ExportType);

	print_log(get_localized_string(LANG_0327));
	removed = tmpMap->remove_unused_model_structures(CLEAN_LIGHTMAP | CLEAN_PLANES | CLEAN_NODES | CLEAN_CLIPNODES | CLEAN_CLIPNODES_SOMETHING | CLEAN_LEAVES | CLEAN_FACES | CLEAN_SURFEDGES | CLEAN_TEXINFOS |
		CLEAN_EDGES | CLEAN_VERTICES | CLEAN_TEXTURES | CLEAN_VISDATA | CLEAN_MARKSURFACES);

	if (!removed.allZero())
		removed.print_delete_stats(1);

	while (tmpMap->models[0].nVisLeafs >= tmpMap->leafCount)
		tmpMap->create_leaf(CONTENTS_EMPTY);

	tmpMap->models[0].nVisLeafs = tmpMap->leafCount - 1;

	for (int i = 0; i < tmpMap->leafCount; i++)
	{
		tmpMap->leaves[i].nVisOffset = -1;
	}

	if (tmpMap->validate())
	{
		tmpMap->update_ent_lump();
		tmpMap->update_lump_pointers();
		removeFile(g_working_dir + src_map->bsp_name + "_model" + std::to_string(id) + ".bsp");
		tmpMap->write(g_working_dir + src_map->bsp_name + "_model" + std::to_string(id) + ".bsp");
	}

	delete tmpMap;
	delete tmpEnt2;
}


void Gui::draw3dContextMenus()
{
	ImGuiContext& g = *GImGui;

	Bsp* map = app->getSelectedMap();

	if (!map)
		return;

	int entIdx = app->pickInfo.GetSelectedEnt();

	if (app->originHovered && entIdx >= 0)
	{
		Entity* ent = map->ents[entIdx];
		if (ImGui::BeginPopup(get_localized_string(LANG_1066).c_str()) || ImGui::BeginPopup(get_localized_string(LANG_1067).c_str()))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0430).c_str(), ""))
			{
				app->transformedOrigin = app->getEntOrigin(map, ent);
				app->applyTransform(map);
				pickCount++; // force gui refresh
			}

			if (ent && ImGui::BeginMenu(get_localized_string(LANG_0431).c_str()))
			{
				BSPMODEL& model = map->models[ent->getBspModelIdx()];

				if (ImGui::MenuItem(get_localized_string(LANG_0432).c_str()))
				{
					app->transformedOrigin.z = app->oldOrigin.z + model.nMaxs.z;
					app->applyTransform(map);
					pickCount++;
				}
				if (ImGui::MenuItem(get_localized_string(LANG_0433).c_str()))
				{
					app->transformedOrigin.z = app->oldOrigin.z + model.nMins.z;
					app->applyTransform(map);
					pickCount++;
				}
				ImGui::Separator();
				if (ImGui::MenuItem(get_localized_string(LANG_0434).c_str()))
				{
					app->transformedOrigin.x = app->oldOrigin.x + model.nMins.x;
					app->applyTransform(map);
					pickCount++;
				}
				if (ImGui::MenuItem(get_localized_string(LANG_0435).c_str()))
				{
					app->transformedOrigin.x = app->oldOrigin.x + model.nMaxs.x;
					app->applyTransform(map);
					pickCount++;
				}
				ImGui::Separator();
				if (ImGui::MenuItem(get_localized_string(LANG_0436).c_str()))
				{
					app->transformedOrigin.y = app->oldOrigin.y + model.nMins.y;
					app->applyTransform(map);
					pickCount++;
				}
				if (ImGui::MenuItem(get_localized_string(LANG_0437).c_str()))
				{
					app->transformedOrigin.y = app->oldOrigin.y + model.nMaxs.y;
					app->applyTransform(map);
					pickCount++;
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		return;
	}
	if (app->pickMode == PICK_FACE)
	{
		if (ImGui::BeginPopup(get_localized_string(LANG_1068).c_str()))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0438).c_str(), get_localized_string(LANG_0439).c_str()))
			{
				copyTexture();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0440).c_str(), get_localized_string(LANG_0441).c_str(), false, copiedMiptex >= 0 && copiedMiptex < map->textureCount))
			{
				pasteTexture();
			}

			ImGui::Separator();

			if (ImGui::MenuItem(get_localized_string(LANG_0442).c_str(), get_localized_string(LANG_0443).c_str()))
			{
				copyLightmap();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0444).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0445).c_str(), "", false, copiedLightmapFace >= 0 && copiedLightmapFace < map->faceCount))
			{
				pasteLightmap();
			}

			ImGui::EndPopup();
		}
	}
	else /*if (app->pickMode == PICK_OBJECT)*/
	{
		if (ImGui::BeginPopup(get_localized_string(LANG_1151).c_str()) && entIdx >= 0)
		{
			Entity* ent = map->ents[entIdx];
			int modelIdx = ent->getBspModelIdx();
			if (modelIdx < 0 && ent->isWorldSpawn())
				modelIdx = 0;

			if (modelIdx != 0 || !app->copiedEnts.empty())
			{
				if (modelIdx != 0)
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0446).c_str(), get_localized_string(LANG_0447).c_str(), false, app->pickInfo.selectedEnts.size()))
					{
						app->cutEnt();
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0448).c_str(), get_localized_string(LANG_0439).c_str(), false, app->pickInfo.selectedEnts.size()))
					{
						app->copyEnt();
					}
				}

				if (!app->copiedEnts.empty())
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0449).c_str(), get_localized_string(LANG_0441).c_str(), false))
					{
						app->pasteEnt(false);
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0450).c_str(), 0, false))
					{
						app->pasteEnt(true);
					}
				}

				if (modelIdx != 0)
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0451).c_str(), get_localized_string(LANG_0452).c_str()))
					{
						app->deleteEnts();
					}
				}
			}
			if (map->ents[entIdx]->hide)
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0453).c_str(), get_localized_string(LANG_0454).c_str()))
				{
					map->ents[entIdx]->hide = false;
					map->getBspRender()->preRenderEnts();
					app->updateEntConnections();
				}
			}
			else if (ImGui::MenuItem(get_localized_string(LANG_0455).c_str(), get_localized_string(LANG_0454).c_str()))
			{
				map->hideEnts();
				app->clearSelection();
				map->getBspRender()->preRenderEnts();
				app->updateEntConnections();
				pickCount++;
			}

			ImGui::Separator();
			if (app->pickInfo.selectedEnts.size() == 1)
			{
				if (modelIdx >= 0)
				{
					BSPMODEL& model = map->models[modelIdx];
					if (ImGui::BeginMenu(get_localized_string(LANG_0456).c_str()))
					{
						if (modelIdx > 0 || map->is_bsp_model)
						{
							if (ImGui::BeginMenu(get_localized_string(LANG_0457).c_str(), !app->invalidSolid && app->isTransformableSolid))
							{
								if (ImGui::MenuItem(get_localized_string(LANG_0458).c_str()))
								{
									map->regenerate_clipnodes(modelIdx, -1);
									checkValidHulls();
									print_log(get_localized_string(LANG_0328), modelIdx);
								}

								ImGui::Separator();

								for (int i = 1; i < MAX_MAP_HULLS; i++)
								{
									if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str()))
									{
										map->regenerate_clipnodes(modelIdx, i);
										checkValidHulls();
										print_log(get_localized_string(LANG_0329), i, modelIdx);
									}
								}
								ImGui::EndMenu();
							}

							if (ImGui::BeginMenu(get_localized_string(LANG_0459).c_str(), !app->isLoading))
							{
								if (ImGui::MenuItem(get_localized_string(LANG_0460).c_str()))
								{
									map->delete_hull(0, modelIdx, -1);
									map->delete_hull(1, modelIdx, -1);
									map->delete_hull(2, modelIdx, -1);
									map->delete_hull(3, modelIdx, -1);
									map->getBspRender()->refreshModel(modelIdx);
									checkValidHulls();
									print_log(get_localized_string(LANG_0330), modelIdx);
								}
								if (ImGui::MenuItem(get_localized_string(LANG_1069).c_str()))
								{
									map->delete_hull(1, modelIdx, -1);
									map->delete_hull(2, modelIdx, -1);
									map->delete_hull(3, modelIdx, -1);
									map->getBspRender()->refreshModelClipnodes(modelIdx);
									checkValidHulls();
									print_log(get_localized_string(LANG_0331), modelIdx);
								}

								ImGui::Separator();

								for (int i = 0; i < MAX_MAP_HULLS; i++)
								{
									bool isHullValid = model.iHeadnodes[i] >= 0;

									if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), 0, false, isHullValid))
									{
										map->delete_hull(i, modelIdx, -1);
										checkValidHulls();
										if (i == 0)
											map->getBspRender()->refreshModel(modelIdx);
										else
											map->getBspRender()->refreshModelClipnodes(modelIdx);
										print_log(get_localized_string(LANG_0332), i, modelIdx);
									}
								}

								ImGui::EndMenu();
							}

							if (ImGui::BeginMenu(get_localized_string(LANG_0461).c_str(), !app->isLoading))
							{
								if (ImGui::MenuItem(get_localized_string(LANG_1152).c_str()))
								{
									map->simplify_model_collision(modelIdx, 1);
									map->simplify_model_collision(modelIdx, 2);
									map->simplify_model_collision(modelIdx, 3);
									map->getBspRender()->refreshModelClipnodes(modelIdx);
									print_log(get_localized_string(LANG_0333), modelIdx);
								}

								ImGui::Separator();

								for (int i = 1; i < MAX_MAP_HULLS; i++)
								{
									bool isHullValid = map->models[modelIdx].iHeadnodes[i] >= 0;

									if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), 0, false, isHullValid))
									{
										map->simplify_model_collision(modelIdx, 1);
										map->getBspRender()->refreshModelClipnodes(modelIdx);
										print_log(get_localized_string(LANG_0334), i, modelIdx);
									}
								}

								ImGui::EndMenu();
							}

							bool canRedirect = map->models[modelIdx].iHeadnodes[1] != map->models[modelIdx].iHeadnodes[2] || map->models[modelIdx].iHeadnodes[1] != map->models[modelIdx].iHeadnodes[3];

							if (ImGui::BeginMenu(get_localized_string(LANG_0462).c_str(), canRedirect && !app->isLoading))
							{
								for (int i = 1; i < MAX_MAP_HULLS; i++)
								{
									if (ImGui::BeginMenu(("Hull " + std::to_string(i)).c_str()))
									{

										for (int k = 1; k < MAX_MAP_HULLS; k++)
										{
											if (i == k)
												continue;

											bool isHullValid = map->models[modelIdx].iHeadnodes[k] >= 0 && map->models[modelIdx].iHeadnodes[k] != map->models[modelIdx].iHeadnodes[i];

											if (ImGui::MenuItem(("Hull " + std::to_string(k)).c_str(), 0, false, isHullValid))
											{
												map->models[modelIdx].iHeadnodes[i] = map->models[modelIdx].iHeadnodes[k];
												map->getBspRender()->refreshModelClipnodes(modelIdx);
												checkValidHulls();
												print_log(get_localized_string(LANG_0335), i, k, modelIdx);
											}
										}

										ImGui::EndMenu();
									}
								}

								ImGui::EndMenu();
							}
						}
						if (ImGui::BeginMenu(get_localized_string(LANG_0463).c_str(), !app->isLoading))
						{
							for (int i = 0; i < MAX_MAP_HULLS; i++)
							{
								if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str()))
								{
									map->print_model_hull(modelIdx, i);
									showLogWidget = true;
								}
							}
							ImGui::EndMenu();
						}

						ImGui::EndMenu();
					}


					ImGui::Separator();

					bool allowDuplicate = app->pickInfo.selectedEnts.size() > 0;
					if (allowDuplicate && app->pickInfo.selectedEnts.size() > 1)
					{
						for (auto& tmpEntIdx : app->pickInfo.selectedEnts)
						{
							if (tmpEntIdx < 0)
							{
								allowDuplicate = false;
								break;
							}
							else
							{
								if (map->ents[tmpEntIdx]->getBspModelIdx() <= 0)
								{
									allowDuplicate = false;
									break;
								}
							}
						}
					}
					if (modelIdx > 0)
					{
						if (ImGui::MenuItem(get_localized_string(LANG_0464).c_str(), 0, false, !app->isLoading && allowDuplicate))
						{
							print_log(get_localized_string(LANG_0336), app->pickInfo.selectedEnts.size());
							for (auto& tmpEntIdx : app->pickInfo.selectedEnts)
							{
								DuplicateBspModelCommand* command = new DuplicateBspModelCommand("Duplicate BSP Model", tmpEntIdx);
								map->getBspRender()->pushUndoCommand(command);
							}
						}
						if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
						{
							ImGui::BeginTooltip();
							ImGui::TextUnformatted(get_localized_string(LANG_0465).c_str());
							ImGui::EndTooltip();
						}
					}
					if (ImGui::BeginMenu(get_localized_string(LANG_0466).c_str(), !app->isLoading))
					{
						if (ImGui::BeginMenu(get_localized_string(LANG_0467).c_str(), !app->isLoading))
						{
							if (ImGui::MenuItem(get_localized_string(LANG_0468).c_str(), 0, false, !app->isLoading))
							{
								ExportModel(map, modelIdx, 0, false);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_0469).c_str(), 0, false, !app->isLoading))
							{
								ExportModel(map, modelIdx, 2, false);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_0470).c_str(), 0, false, !app->isLoading))
							{
								ExportModel(map, modelIdx, 1, false);
							}
							ImGui::EndMenu();
						}

						if (ImGui::BeginMenu(get_localized_string(LANG_0471).c_str(), !app->isLoading))
						{
							if (ImGui::MenuItem(get_localized_string(LANG_1070).c_str(), 0, false, !app->isLoading))
							{
								ExportModel(map, modelIdx, 0, true);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1071).c_str(), 0, false, !app->isLoading))
							{
								ExportModel(map, modelIdx, 2, true);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1072).c_str(), 0, false, !app->isLoading))
							{
								ExportModel(map, modelIdx, 1, true);
							}
							ImGui::EndMenu();
						}

						ImGui::EndMenu();
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(get_localized_string(LANG_0472).c_str());
						ImGui::EndTooltip();
					}

				}
			}
			if (modelIdx > 0)
			{
				if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", get_localized_string(LANG_0473).c_str()))
				{
					if (!app->movingEnt)
						app->grabEnt();
					else
					{
						app->ungrabEnt();
					}
				}

				if (ImGui::MenuItem(get_localized_string(LANG_0474).c_str(), get_localized_string(LANG_0475).c_str()))
				{
					showTransformWidget = true;
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem(get_localized_string(LANG_0476).c_str(), get_localized_string(LANG_0477).c_str()))
			{
				showKeyvalueWidget = true;
			}


			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup(get_localized_string(LANG_1153).c_str()))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_1073).c_str(), get_localized_string(LANG_1074).c_str(), false, app->copiedEnts.size()))
			{
				app->pasteEnt(false);
			}
			if (ImGui::MenuItem(get_localized_string(LANG_1075).c_str(), 0, false, app->copiedEnts.size()))
			{
				app->pasteEnt(true);
			}

			ImGui::EndPopup();
		}
	}

}

bool ExportWad(Bsp* map)
{
	bool retval = true;
	if (map->textureCount > 0)
	{
		Wad* tmpWad = new Wad(map->bsp_path);
		std::vector<WADTEX*> tmpWadTex;
		for (int i = 0; i < map->textureCount; i++)
		{
			int oldOffset = ((int*)map->textures)[i + 1];
			if (oldOffset >= 0)
			{
				BSPMIPTEX* bspTex = (BSPMIPTEX*)(map->textures + oldOffset);
				if (bspTex->nOffsets[0] <= 0)
					continue;
				WADTEX* oldTex = new WADTEX(bspTex);
				tmpWadTex.push_back(oldTex);
			}
		}
		if (!tmpWadTex.empty())
		{
			createDir(g_working_dir);
			tmpWad->write(g_working_dir + map->bsp_name + ".wad", tmpWadTex);
		}
		else
		{
			retval = false;
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0337));
		}
		tmpWadTex.clear();
		delete tmpWad;
	}
	else
	{
		retval = false;
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0338));
	}
	return retval;
}

void ImportWad(Bsp* map, Renderer* app, std::string path)
{
	Wad* tmpWad = new Wad(path);

	if (!tmpWad->readInfo())
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0339));
		delete tmpWad;
		return;
	}
	else
	{
		for (int i = 0; i < (int)tmpWad->dirEntries.size(); i++)
		{
			WADTEX* wadTex = tmpWad->readTexture(i);
			COLOR3* imageData = ConvertWadTexToRGB(wadTex);
			if (map->is_bsp2 || map->is_bsp29)
			{
				map->add_texture(wadTex->szName, (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight);
			}
			else
			{
				map->add_texture(wadTex);
			}
			delete[] imageData;
			delete wadTex;
		}
		for (int i = 0; i < app->mapRenderers.size(); i++)
		{
			app->mapRenderers[i]->reloadTextures();
		}
	}

	delete tmpWad;
}


void Gui::drawMenuBar()
{
	ImGuiContext& g = *GImGui;
	ImGui::BeginMainMenuBar();
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = NULL;


	if (map)
	{
		rend = map->getBspRender();
		if (ifd::FileDialog::Instance().IsDone("WadOpenDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				for (int i = 0; i < map->ents.size(); i++)
				{
					if (map->ents[i]->keyvalues["classname"] == "worldspawn")
					{
						std::vector<std::string> wadNames = splitString(map->ents[i]->keyvalues["wad"], ";");
						std::string newWadNames;
						for (int k = 0; k < wadNames.size(); k++)
						{
							if (wadNames[k].find(res.filename().string()) == std::string::npos)
								newWadNames += wadNames[k] + ";";
						}
						map->ents[i]->setOrAddKeyvalue("wad", newWadNames);
						break;
					}
				}
				app->updateEnts();
				ImportWad(map, app, res.string());
				app->reloadBspModels();
				g_settings.lastdir = res.parent_path().string();
			}
			ifd::FileDialog::Instance().Close();
		}
	}

	if (ifd::FileDialog::Instance().IsDone("MapOpenDialog"))
	{
		if (ifd::FileDialog::Instance().HasResult())
		{
			std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
			std::string pathlowercase = toLowerCase(res.string());
			if (pathlowercase.ends_with(".wad"))
			{
				if (!app->SelectedMap)
				{
					app->addMap(new Bsp(""));
					app->selectMapId(0);
				}

				if (app->SelectedMap)
				{
					bool foundInMap = false;
					for (auto& wad : app->SelectedMap->getBspRender()->wads)
					{
						std::string tmppath = toLowerCase(wad->filename);
						if (tmppath.find(basename(pathlowercase)) != std::string::npos)
						{
							foundInMap = true;
							print_log(get_localized_string(LANG_0340));
							break;
						}
					}

					if (!foundInMap)
					{
						Wad* wad = new Wad(res.string());
						if (wad->readInfo())
						{
							app->SelectedMap->getBspRender()->wads.push_back(wad);
							if (!app->SelectedMap->ents[0]->keyvalues["wad"].ends_with(";"))
								app->SelectedMap->ents[0]->keyvalues["wad"] += ";";
							app->SelectedMap->ents[0]->keyvalues["wad"] += basename(res.string()) + ";";
							app->SelectedMap->update_ent_lump();
							app->updateEnts();
							map->getBspRender()->reload();
						}
						else
							delete wad;
					}
				}
			}
			else if (pathlowercase.ends_with(".mdl"))
			{
				Bsp* tmpMap = new Bsp(res.string());
				tmpMap->is_mdl_model = true;
				app->addMap(tmpMap);
			}
			else
			{
				app->addMap(new Bsp(res.string()));
			}
			g_settings.lastdir = res.parent_path().string();
		}
		ifd::FileDialog::Instance().Close();
	}

	if (ImGui::BeginMenu(get_localized_string(LANG_0478).c_str()))
	{
		if (ImGui::MenuItem(get_localized_string(LANG_0479).c_str(), NULL, false, map && !map->is_mdl_model && !app->isLoading))
		{
			map->update_ent_lump();
			map->update_lump_pointers();
			map->write(map->bsp_path);
		}
		if (ImGui::BeginMenu(get_localized_string(LANG_0480).c_str(), map && !map->is_mdl_model && !app->isLoading))
		{
			bool old_is_bsp30ext = map->is_bsp30ext;
			bool old_is_bsp2 = map->is_bsp2;
			bool old_is_bsp2_old = map->is_bsp2_old;
			bool old_is_bsp29 = map->is_bsp29;
			bool old_is_32bit_clipnodes = map->is_32bit_clipnodes;
			bool old_is_broken_clipnodes = map->is_broken_clipnodes;
			bool old_is_blue_shift = map->is_blue_shift;
			bool old_is_colored_lightmap = map->is_colored_lightmap;

			int old_bsp_version = map->bsp_header.nVersion;

			bool is_default_format = !old_is_bsp30ext && !old_is_bsp2 &&
				!old_is_bsp2_old && !old_is_bsp29 && !old_is_32bit_clipnodes && !old_is_broken_clipnodes
				&& !old_is_blue_shift && old_is_colored_lightmap && old_bsp_version == 30;

			bool is_need_reload = false;

			if (ImGui::MenuItem(get_localized_string(LANG_0481).c_str(), NULL, is_default_format))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = false;
					map->is_32bit_clipnodes = false;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;

					map->bsp_header.nVersion = 30;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0341));
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (is_default_format)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0482).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0483).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0484).c_str());
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0485).c_str(), NULL, old_is_blue_shift))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = false;
					map->is_32bit_clipnodes = false;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = true;
					map->is_colored_lightmap = true;

					map->bsp_header.nVersion = 30;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_blue_shift)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0486).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0487).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0488).c_str());
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0489).c_str(), NULL, old_is_bsp29 && !old_is_broken_clipnodes && old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = false;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
				}
			}


			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp29 && !old_is_broken_clipnodes && old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0490).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0491).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0492).c_str());
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0493).c_str(), NULL, old_is_bsp29 && !old_is_broken_clipnodes && !old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = false;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = false;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
				}
			}



			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp29 && !old_is_broken_clipnodes && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0494).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0495).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0496).c_str());
				}
				ImGui::EndTooltip();
			}

			if (old_is_broken_clipnodes)
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0497).c_str(), NULL, old_is_bsp29 && old_is_colored_lightmap))
				{
					if (map->isValid())
					{
						map->update_ent_lump();
						map->update_lump_pointers();

						map->is_bsp30ext = false;
						map->is_bsp2 = false;
						map->is_bsp2_old = false;
						map->is_bsp29 = true;
						map->is_32bit_clipnodes = false;
						map->is_broken_clipnodes = true;
						map->is_blue_shift = false;
						map->is_colored_lightmap = true;

						map->bsp_header.nVersion = 29;

						if (map->validate() && map->isValid())
						{
							is_need_reload = true;
							map->write(map->bsp_path);
						}
					}
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					if (old_is_bsp29 && old_is_colored_lightmap)
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0498).c_str());
					}
					else if (map->isValid())
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0499).c_str());
					}
					else
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0500).c_str());
					}
					ImGui::EndTooltip();
				}

				if (ImGui::MenuItem(get_localized_string(LANG_0501).c_str(), NULL, old_is_bsp29 && !old_is_colored_lightmap))
				{
					if (map->isValid())
					{
						map->update_ent_lump();
						map->update_lump_pointers();

						map->is_bsp30ext = false;
						map->is_bsp2 = false;
						map->is_bsp2_old = false;
						map->is_bsp29 = true;
						map->is_32bit_clipnodes = false;
						map->is_broken_clipnodes = true;
						map->is_blue_shift = false;
						map->is_colored_lightmap = false;

						map->bsp_header.nVersion = 29;

						if (map->validate() && map->isValid())
						{
							is_need_reload = true;
							map->write(map->bsp_path);
						}
					}
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					if (old_is_bsp29 && !map->is_colored_lightmap && !old_is_colored_lightmap)
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0502).c_str());
					}
					else if (map->isValid())
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0503).c_str());
					}
					else
					{
						ImGui::TextUnformatted(get_localized_string(LANG_0504).c_str());
					}
					ImGui::EndTooltip();
				}

			}

			if (ImGui::MenuItem(get_localized_string(LANG_0505).c_str(), NULL, old_is_bsp2 && !old_is_bsp2_old && old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = true;
					map->is_bsp2_old = false;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp2 && !old_is_bsp2_old && old_is_colored_lightmap)
				{
					ImGui::TextUnformatted("Map already saved in BSP2(29) + COLOR LIGHT format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to BSP2(29) + COLOR LIGHT compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to BSP2(29) + COLOR LIGH.");
				}
				ImGui::EndTooltip();
			}



			if (ImGui::MenuItem(get_localized_string(LANG_0506).c_str(), NULL, old_is_bsp2 && !old_is_bsp2_old && !old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = true;
					map->is_bsp2_old = false;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = false;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp2 && !old_is_bsp2_old && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted("Map already saved in BSP2(29) + MONO LIGHT format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to BSP2(29) + MONO LIGHT compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to BSP2(29) + MONO LIGH.");
				}
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem(get_localized_string(LANG_0507).c_str(), NULL, old_is_bsp2_old && old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = true;
					map->is_bsp2_old = true;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
				}
			}


			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp2_old && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0508).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0509).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0510).c_str());
				}
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem(get_localized_string(LANG_0511).c_str(), NULL, old_is_bsp2_old && !old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = false;
					map->is_bsp2 = true;
					map->is_bsp2_old = true;
					map->is_bsp29 = true;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = false;

					map->bsp_header.nVersion = 29;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
				}
			}


			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp2_old && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0512).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0513).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0514).c_str());
				}
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem(get_localized_string(LANG_0515).c_str(), NULL, old_is_bsp30ext && old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = true;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = false;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = true;

					map->bsp_header.nVersion = 30;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp30ext && old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0516).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0517).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0518).c_str());
				}
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem(get_localized_string(LANG_0519).c_str(), NULL, old_is_bsp2_old && !old_is_colored_lightmap))
			{
				if (map->isValid())
				{
					map->update_ent_lump();
					map->update_lump_pointers();

					map->is_bsp30ext = true;
					map->is_bsp2 = false;
					map->is_bsp2_old = false;
					map->is_bsp29 = false;
					map->is_32bit_clipnodes = true;
					map->is_broken_clipnodes = false;
					map->is_blue_shift = false;
					map->is_colored_lightmap = false;

					map->bsp_header.nVersion = 30;

					if (map->validate() && map->isValid())
					{
						is_need_reload = true;
						map->write(map->bsp_path);
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_bsp30ext && !old_is_colored_lightmap)
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0520).c_str());
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0521).c_str());
				}
				else
				{
					ImGui::TextUnformatted(get_localized_string(LANG_0522).c_str());
				}
				ImGui::EndTooltip();
			}


			map->is_bsp30ext = old_is_bsp30ext;
			map->is_bsp2 = old_is_bsp2;
			map->is_bsp2_old = old_is_bsp2_old;
			map->is_bsp29 = old_is_bsp29;
			map->is_32bit_clipnodes = old_is_32bit_clipnodes;
			map->is_broken_clipnodes = old_is_broken_clipnodes;
			map->is_blue_shift = old_is_blue_shift;
			map->is_colored_lightmap = old_is_colored_lightmap;
			map->bsp_header.nVersion = old_bsp_version;
			if (is_need_reload)
			{
				app->reloadMaps();
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu(get_localized_string(LANG_0523).c_str()))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0524).c_str()))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select map path", "Map file (*.bsp){.bsp}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0525).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0526).c_str()))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select model path", "Model file (*.mdl){.mdl}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0527).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0528).c_str()))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select wad path", "Wad file (*.wad){.wad}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0529).c_str());
				ImGui::EndTooltip();
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0530).c_str(), NULL, false, !app->isLoading && map))
		{
			filterNeeded = true;
			int mapRenderId = map->getBspRenderId();
			BspRenderer* mapRender = map->getBspRender();
			if (mapRenderId >= 0)
			{
				app->deselectObject();
				app->clearSelection();
				app->deselectMap();
				delete mapRender;
				app->mapRenderers.erase(app->mapRenderers.begin() + mapRenderId);
				app->selectMapId(0);
			}
		}


		if (app->mapRenderers.size() > 1)
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0531).c_str(), NULL, false, !app->isLoading))
			{
				filterNeeded = true;
				if (map)
				{
					app->deselectObject();
					app->clearSelection();
					app->deselectMap();
					app->clearMaps();
					app->addMap(new Bsp(""));
					app->selectMapId(0);
				}
			}
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_0532).c_str(), !app->isLoading && map))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0533).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				std::string entFilePath;
				if (g_settings.sameDirForEnt) {
					std::string bspFilePath = map->bsp_path;
					if (bspFilePath.size() < 4 || bspFilePath.rfind(".bsp") != bspFilePath.size() - 4) {
						entFilePath = bspFilePath + ".ent";
					}
					else {
						entFilePath = bspFilePath.substr(0, bspFilePath.size() - 4) + ".ent";
					}
				}
				else {
					entFilePath = g_working_dir + (map->bsp_name + ".ent");
					createDir(g_working_dir);
				}

				print_log(get_localized_string(LANG_0342), entFilePath);
				std::ofstream entFile(entFilePath, std::ios::trunc);
				map->update_ent_lump();
				if (map->bsp_header.lump[LUMP_ENTITIES].nLength > 0)
				{
					std::string entities = std::string(map->lumps[LUMP_ENTITIES], map->lumps[LUMP_ENTITIES] + map->bsp_header.lump[LUMP_ENTITIES].nLength - 1);
					entFile.write(entities.c_str(), entities.size());
				}
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0534).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				print_log(get_localized_string(LANG_0343), g_working_dir, map->bsp_name + ".wad");
				if (ExportWad(map))
				{
					print_log(get_localized_string(LANG_0344));
					map->delete_embedded_textures();
					if (map->ents.size())
					{
						std::string wadstr = map->ents[0]->keyvalues["wad"];
						if (wadstr.find(map->bsp_name + ".wad" + ";") == std::string::npos)
						{
							map->ents[0]->keyvalues["wad"] += map->bsp_name + ".wad" + ";";
						}
					}
				}
			}
			if (ImGui::BeginMenu("Wavefront (.obj) [WIP]", map && !map->is_mdl_model))
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0535).c_str(), NULL))
				{
					map->ExportToObjWIP(g_working_dir, EXPORT_XYZ, 1);
				}

				for (int scale = 2; scale < 10; scale++, scale++)
				{
					std::string scaleitem = "UpScale x" + std::to_string(scale);
					if (ImGui::MenuItem(scaleitem.c_str(), NULL))
					{
						map->ExportToObjWIP(g_working_dir, EXPORT_XYZ, scale);
					}
				}

				for (int scale = 16; scale > 0; scale--, scale--)
				{
					std::string scaleitem = "DownScale x" + std::to_string(scale);
					if (ImGui::MenuItem(scaleitem.c_str(), NULL))
					{
						map->ExportToObjWIP(g_working_dir, EXPORT_XYZ, -scale);
					}
				}
				ImGui::EndMenu();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0536).c_str());
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem("ValveHammerEditor (.map) [WIP]", NULL, false, map && !map->is_mdl_model))
			{
				map->ExportToMapWIP(g_working_dir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export .map ( NOT WORKING at this time:) )");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0537).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				map->ExportPortalFile();
			}


			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0538).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0539).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				map->ExportExtFile();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export face extens (.ext) file for rad.exe");
				ImGui::EndTooltip();
			}



			if (ImGui::MenuItem(get_localized_string(LANG_0540).c_str(), NULL, false, map && !map->is_mdl_model))
			{
				map->ExportLightFile();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0541).c_str());
				ImGui::EndTooltip();
			}



			if (ImGui::BeginMenu(get_localized_string(LANG_1076).c_str(), map && !map->is_mdl_model))
			{
				int modelIdx = -1;

				if (app->pickInfo.GetSelectedEnt() >= 0)
				{
					modelIdx = map->ents[app->pickInfo.GetSelectedEnt()]->getBspModelIdx();
				}

				for (int i = 0; i < map->modelCount; i++)
				{
					if (ImGui::BeginMenu(((modelIdx != i ? "Export Model" : "+ Export Model") + std::to_string(i) + ".bsp").c_str()))
					{
						if (ImGui::BeginMenu(get_localized_string(LANG_1077).c_str(), i >= 0))
						{
							if (ImGui::MenuItem(get_localized_string(LANG_1154).c_str(), 0, false, i >= 0))
							{
								ExportModel(map, i, 0, false);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1155).c_str(), 0, false, i >= 0))
							{
								ExportModel(map, i, 2, false);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1156).c_str(), 0, false, i >= 0))
							{
								ExportModel(map, i, 1, false);
							}
							ImGui::EndMenu();
						}
						if (ImGui::BeginMenu(get_localized_string(LANG_1078).c_str(), i >= 0))
						{
							if (ImGui::MenuItem(get_localized_string(LANG_1173).c_str(), 0, false, i >= 0))
							{
								ExportModel(map, i, 0, true);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1174).c_str(), 0, false, i >= 0))
							{
								ExportModel(map, i, 2, true);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1175).c_str(), 0, false, i >= 0))
							{
								ExportModel(map, i, 1, true);
							}
							ImGui::EndMenu();
						}

						ImGui::EndMenu();
					}
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu(get_localized_string(LANG_0542).c_str(), map && !map->is_mdl_model))
			{
				std::string hash = "##1";
				for (auto& wad : map->getBspRender()->wads)
				{
					if (wad->dirEntries.size() == 0)
						continue;
					hash += "1";
					if (ImGui::MenuItem((basename(wad->filename) + hash).c_str()))
					{
						print_log(get_localized_string(LANG_0345), basename(wad->filename));

						createDir(g_working_dir + "wads/" + basename(wad->filename));

						std::vector<int> texturesIds;
						for (int i = 0; i < wad->dirEntries.size(); i++)
						{
							texturesIds.push_back(i);
						}

						std::for_each(std::execution::par_unseq, texturesIds.begin(), texturesIds.end(), [&](int file)
							{
								{
									WADTEX* texture = wad->readTexture(file);

									if (texture->szName[0] != '\0')
									{
										print_log(get_localized_string(LANG_0346), texture->szName, basename(wad->filename));
										COLOR4* texturedata = ConvertWadTexToRGBA(texture);

										lodepng_encode32_file((g_working_dir + "wads/" + basename(wad->filename) + "/" + std::string(texture->szName) + ".png").c_str()
											, (unsigned char*)texturedata, texture->nWidth, texture->nHeight);


										/*	int lastMipSize = (texture->nWidth / 8) * (texture->nHeight / 8);

											COLOR3* palette = (COLOR3*)(texture->data + texture->nOffsets[3] + lastMipSize + sizeof(short) - 40);

											lodepng_encode24_file((g_working_dir + "wads/" + basename(wad->filename) + "/" + std::string(texture->szName) + ".pal.png").c_str()
																  , (unsigned char*)palette, 8, 32);*/
										delete texturedata;
									}
									delete texture;
								}
							});
					}
				}

				ImGui::EndMenu();
			}

			if (ImGui::MenuItem(get_localized_string("LANG_DUMP_TEX").c_str(), NULL, false, map))
			{
				createDir(g_working_dir + map->bsp_name + "/dump_textures/");

				if (dumpTextures.size())
				{
					for (const auto& tex : dumpTextures)
					{
						if (tex != rend->missingTex)
						{
							if (tex->format == GL_RGBA)
								lodepng_encode32_file((g_working_dir + map->bsp_name + "/dump_textures/" + std::string(tex->texName) + ".png").c_str(), (const unsigned char*)tex->data, tex->width, tex->height);
							else
								lodepng_encode24_file((g_working_dir + map->bsp_name + "/dump_textures/" + std::string(tex->texName) + ".png").c_str(), (const unsigned char*)tex->data, tex->width, tex->height);
						}
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string("LANG_DUMP_TEX_DESC").c_str());
				ImGui::EndTooltip();
			}

			ImGui::EndMenu();
		}


		if (ImGui::BeginMenu(get_localized_string(LANG_0543).c_str(), !app->isLoading && map && !map->is_mdl_model))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0544).c_str(), NULL))
			{
				showImportMapWidget_Type = SHOW_IMPORT_MODEL_BSP;
				showImportMapWidget = true;
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0545).c_str(), NULL))
			{
				showImportMapWidget_Type = SHOW_IMPORT_MODEL_ENTITY;
				showImportMapWidget = true;
			}

			if (map && ImGui::MenuItem(get_localized_string(LANG_1079).c_str(), NULL))
			{
				if (map)
				{
					map->ImportLightFile();
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0347));
				}
			}

			if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0546).c_str());
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem(get_localized_string(LANG_1080).c_str(), NULL))
			{
				if (map)
				{
					std::string entFilePath;
					if (g_settings.sameDirForEnt) {
						std::string bspFilePath = map->bsp_path;
						if (bspFilePath.size() < 4 || bspFilePath.rfind(".bsp") != bspFilePath.size() - 4) {
							entFilePath = bspFilePath + ".ent";
						}
						else {
							entFilePath = bspFilePath.substr(0, bspFilePath.size() - 4) + ".ent";
						}
					}
					else {
						entFilePath = g_working_dir + (map->bsp_name + ".ent");
					}

					print_log(get_localized_string(LANG_1052), entFilePath);
					if (fileExists(entFilePath))
					{
						int len;
						char* newlump = loadFile(entFilePath, len);
						map->replace_lump(LUMP_ENTITIES, newlump, len);
						map->load_ents();
						for (int i = 0; i < app->mapRenderers.size(); i++)
						{
							BspRenderer* mapRender = app->mapRenderers[i];
							mapRender->reload();
						}
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0348));
					}
				}
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0547).c_str(), NULL))
			{
				if (map)
				{
					ifd::FileDialog::Instance().Open("WadOpenDialog", "Open .wad", "Wad file (*.wad){.wad},.*", false, g_settings.lastdir);
				}

				if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					std::string embtextooltip;
					ImGui::TextUnformatted(fmt::format(fmt::runtime(get_localized_string(LANG_0349)), g_working_dir, map->bsp_name + ".wad").c_str());
					ImGui::EndTooltip();
				}
			}

			static bool ditheringEnabled = false;

			if (ImGui::BeginMenu(get_localized_string(LANG_0548).c_str()))
			{
				ImGui::MenuItem(get_localized_string(LANG_0549).c_str(), 0, ditheringEnabled);

				std::string hash = "##1";
				for (auto& wad : map->getBspRender()->wads)
				{
					if (wad->dirEntries.size() == 0)
						continue;
					hash += "1";
					if (ImGui::MenuItem((basename(wad->filename) + hash).c_str()))
					{
						print_log(get_localized_string(LANG_0350), basename(wad->filename));
						if (!dirExists(g_working_dir + "wads/" + basename(wad->filename)))
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0351), g_working_dir + "wads/" + basename(wad->filename));
						}
						else
						{
							copyFile(wad->filename, wad->filename + ".bak");

							Wad* resetWad = new Wad(wad->filename);
							resetWad->write(NULL, 0);
							delete resetWad;

							Wad* tmpWad = new Wad(wad->filename);

							std::vector<WADTEX*> textureList{};
							fs::path tmpPath = g_working_dir + "wads/" + basename(wad->filename);

							std::vector<std::string> files{};

							for (auto& dir_entry : std::filesystem::directory_iterator(tmpPath))
							{
								if (!dir_entry.is_directory() && toLowerCase(dir_entry.path().string()).ends_with(".png"))
								{
									files.emplace_back(dir_entry.path().string());
								}
							}

							std::for_each(std::execution::par_unseq, files.begin(), files.end(), [&](const auto file)
								{
									print_log(get_localized_string(LANG_0352), basename(file), basename(wad->filename));
									COLOR4* image_bytes = NULL;
									unsigned int w2, h2;
									auto error = lodepng_decode_file((unsigned char**)&image_bytes, &w2, &h2, file.c_str(),
										LodePNGColorType::LCT_RGBA, 8);
									COLOR3* image_bytes_rgb = (COLOR3*)&image_bytes[0];
									if (error == 0 && image_bytes)
									{
										for (unsigned int i = 0; i < w2 * h2; i++)
										{
											COLOR4& curPixel = image_bytes[i];

											if (curPixel.a == 0)
											{
												image_bytes_rgb[i] = COLOR3(0, 0, 255);
											}
											else
											{
												image_bytes_rgb[i] = COLOR3(curPixel.r, curPixel.g, curPixel.b);
											}
										}

										int oldcolors = 0;
										if ((oldcolors = GetImageColors((COLOR3*)image_bytes, w2 * h2)) > 256)
										{
											print_log(get_localized_string(LANG_0353), basename(file));
											Quantizer* tmpCQuantizer = new Quantizer(256, 8);

											if (ditheringEnabled)
												tmpCQuantizer->ApplyColorTableDither((COLOR3*)image_bytes, w2, h2);
											else
												tmpCQuantizer->ApplyColorTable((COLOR3*)image_bytes, w2 * h2);

											print_log(get_localized_string(LANG_0354), oldcolors, GetImageColors((COLOR3*)image_bytes, w2 * h2));

											delete tmpCQuantizer;
										}
										std::string tmpTexName = stripExt(basename(file));

										WADTEX* tmpWadTex = create_wadtex(tmpTexName.c_str(), (COLOR3*)image_bytes, w2, h2);
										g_mutex_list[1].lock();
										textureList.push_back(tmpWadTex);
										g_mutex_list[1].unlock();
										free(image_bytes);
									}
								});
							print_log(get_localized_string(LANG_0355));

							tmpWad->write(textureList);
							delete tmpWad;
							map->getBspRender()->reloadTextures();
						}
					}
				}
				ImGui::EndMenu();
			}

			ImGui::EndMenu();
		}

		if (map && dirExists(g_game_dir + "/svencoop_addon/maps/"))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0550).c_str()))
			{
				std::string mapPath = g_game_dir + "/svencoop_addon/maps/" + map->bsp_name + ".bsp";
				std::string entPath = g_game_dir + "/svencoop_addon/scripts/maps/bspguy/maps/" + map->bsp_name + ".ent";

				map->update_ent_lump(true); // strip nodes before writing (to skip slow node graph generation)
				map->write(mapPath);
				map->update_ent_lump(false); // add the nodes back in for conditional loading in the ent file

				std::ofstream entFile(entPath, std::ios::trunc);
				if (entFile.is_open())
				{
					print_log(get_localized_string(LANG_1053), entPath);
					entFile.write((const char*)map->lumps[LUMP_ENTITIES], map->bsp_header.lump[LUMP_ENTITIES].nLength - 1);
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0356), entPath);
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0357));
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0551).c_str());
				ImGui::EndTooltip();
			}
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0552).c_str(), 0, false, map && !map->is_mdl_model && !app->isLoading))
		{
			app->reloadMaps();
		}
		if (ImGui::MenuItem(get_localized_string(LANG_0553).c_str(), 0, false, map && !map->is_mdl_model && !app->isLoading))
		{
			if (map)
			{
				print_log(get_localized_string(LANG_0358), map->bsp_name);
				map->validate();
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem(get_localized_string(LANG_0554).c_str(), 0, false, !app->isLoading))
		{
			if (!showSettingsWidget)
			{
				reloadSettings = true;
			}
			showSettingsWidget = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem(get_localized_string(LANG_0555).c_str(), NULL))
		{
			if (fileSize(g_settings_path) == 0)
			{
				g_settings.save();
				glfwTerminate();
				std::quick_exit(0);
			}
			g_settings.save();
			if (fileSize(g_settings_path) == 0)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0359));
			}
			else
			{
				glfwTerminate();
				std::quick_exit(0);
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(get_localized_string(LANG_0556).c_str(), (map && !map->is_mdl_model)))
	{
		Command* undoCmd = !rend->undoHistory.empty() ? rend->undoHistory[rend->undoHistory.size() - 1] : NULL;
		Command* redoCmd = !rend->redoHistory.empty() ? rend->redoHistory[rend->redoHistory.size() - 1] : NULL;
		std::string undoTitle = undoCmd ? "Undo " + undoCmd->desc : "Can't undo";
		std::string redoTitle = redoCmd ? "Redo " + redoCmd->desc : "Can't redo";
		bool canUndo = undoCmd && (!app->isLoading || undoCmd->allowedDuringLoad);
		bool canRedo = redoCmd && (!app->isLoading || redoCmd->allowedDuringLoad);
		bool entSelected = app->pickInfo.selectedEnts.size();
		bool mapSelected = map;
		bool nonWorldspawnEntSelected = !entSelected;

		if (!nonWorldspawnEntSelected)
		{
			for (auto& ent : app->pickInfo.selectedEnts)
			{
				if (ent < 0)
				{
					nonWorldspawnEntSelected = true;
					break;
				}
				if (map->ents[ent]->hasKey("classname") && map->ents[ent]->keyvalues["classname"] == "worldspawn")
				{
					nonWorldspawnEntSelected = true;
					break;
				}
			}
		}

		if (ImGui::MenuItem(undoTitle.c_str(), get_localized_string(LANG_0557).c_str(), false, canUndo))
		{
			rend->undo();
		}
		else if (ImGui::MenuItem(redoTitle.c_str(), get_localized_string(LANG_0558).c_str(), false, canRedo))
		{
			rend->redo();
		}

		ImGui::Separator();

		if (ImGui::MenuItem(get_localized_string(LANG_1081).c_str(), get_localized_string(LANG_1082).c_str(), false, nonWorldspawnEntSelected && app->pickInfo.selectedEnts.size()))
		{
			app->cutEnt();
		}
		if (ImGui::MenuItem(get_localized_string(LANG_1083).c_str(), get_localized_string(LANG_1084).c_str(), false, nonWorldspawnEntSelected && app->pickInfo.selectedEnts.size()))
		{
			app->copyEnt();
		}
		if (ImGui::MenuItem(get_localized_string(LANG_1157).c_str(), get_localized_string(LANG_1158).c_str(), false, mapSelected && app->copiedEnts.size()))
		{
			app->pasteEnt(false);
		}
		if (ImGui::MenuItem(get_localized_string(LANG_1159).c_str(), 0, false, entSelected && app->copiedEnts.size()))
		{
			app->pasteEnt(true);
		}
		if (ImGui::MenuItem(get_localized_string(LANG_1085).c_str(), get_localized_string(LANG_1086).c_str(), false, nonWorldspawnEntSelected))
		{
			app->deleteEnts();
		}
		if (ImGui::MenuItem(get_localized_string(LANG_0559).c_str(), get_localized_string(LANG_0560).c_str()))
		{
			map->hideEnts(false);
			map->getBspRender()->preRenderEnts();
			app->updateEntConnections();
			pickCount++;
		}

		ImGui::Separator();


		bool allowDuplicate = app->pickInfo.selectedEnts.size() > 0;
		if (allowDuplicate)
		{
			for (auto& ent : app->pickInfo.selectedEnts)
			{
				if (ent < 0)
				{
					allowDuplicate = false;
					break;
				}
				else
				{
					if (map->ents[ent]->getBspModelIdx() <= 0)
					{
						allowDuplicate = false;
						break;
					}
				}
			}
		}

		if (ImGui::MenuItem(get_localized_string(LANG_1087).c_str(), 0, false, !app->isLoading && allowDuplicate))
		{
			print_log(get_localized_string(LANG_1054), app->pickInfo.selectedEnts.size());
			for (auto& ent : app->pickInfo.selectedEnts)
			{
				DuplicateBspModelCommand* command = new DuplicateBspModelCommand("Duplicate BSP Model", ent);
				map->getBspRender()->pushUndoCommand(command);
			}
		}

		if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", get_localized_string(LANG_1088).c_str(), false, nonWorldspawnEntSelected))
		{
			if (!app->movingEnt)
				app->grabEnt();
			else
			{
				app->ungrabEnt();
			}
		}
		if (ImGui::MenuItem(get_localized_string(LANG_1089).c_str(), get_localized_string(LANG_1090).c_str(), false, entSelected))
		{
			showTransformWidget = !showTransformWidget;
		}

		ImGui::Separator();

		if (ImGui::MenuItem(get_localized_string(LANG_1091).c_str(), get_localized_string(LANG_1092).c_str(), false, entSelected))
		{
			showKeyvalueWidget = !showKeyvalueWidget;
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(get_localized_string(LANG_0561).c_str(), (map && !map->is_mdl_model)))
	{
		if (ImGui::MenuItem(get_localized_string(LANG_0562).c_str(), NULL))
		{
			showEntityReport = true;
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0563).c_str(), NULL))
		{
			showLimitsWidget = true;
		}

		ImGui::Separator();


		if (ImGui::MenuItem(get_localized_string(LANG_0564).c_str(), 0, false, !app->isLoading && map))
		{
			CleanMapCommand* command = new CleanMapCommand("Clean " + map->bsp_name, app->getSelectedMapId(), rend->undoLumpState);
			rend->pushUndoCommand(command);
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0565).c_str(), 0, false, !app->isLoading && map))
		{
			OptimizeMapCommand* command = new OptimizeMapCommand("Optimize " + map->bsp_name, app->getSelectedMapId(), rend->undoLumpState);
			rend->pushUndoCommand(command);
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_0566).c_str(), map))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0567).c_str(), NULL, app->clipnodeRenderHull == -1))
			{
				app->clipnodeRenderHull = -1;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0568).c_str(), NULL, app->clipnodeRenderHull == 0))
			{
				app->clipnodeRenderHull = 0;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0569).c_str(), NULL, app->clipnodeRenderHull == 1))
			{
				app->clipnodeRenderHull = 1;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0570).c_str(), NULL, app->clipnodeRenderHull == 2))
			{
				app->clipnodeRenderHull = 2;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0571).c_str(), NULL, app->clipnodeRenderHull == 3))
			{
				app->clipnodeRenderHull = 3;
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();

		bool hasAnyCollision = anyHullValid[1] || anyHullValid[2] || anyHullValid[3];

		if (ImGui::BeginMenu(get_localized_string(LANG_1093).c_str(), hasAnyCollision && !app->isLoading && map))
		{
			for (int i = 1; i < MAX_MAP_HULLS; i++)
			{
				if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), NULL, false, anyHullValid[i]))
				{
					//for (int k = 0; k < app->mapRenderers.size(); k++) {
					//	Bsp* map = app->mapRenderers[k]->map;
					map->delete_hull(i, -1);
					map->getBspRender()->reloadClipnodes();
					//	app->mapRenderers[k]->reloadClipnodes();
					print_log(get_localized_string(LANG_0360), i, map->bsp_name);
					//}
					checkValidHulls();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_1094).c_str(), hasAnyCollision && !app->isLoading && map))
		{
			for (int i = 1; i < MAX_MAP_HULLS; i++)
			{
				if (ImGui::BeginMenu(("Hull " + std::to_string(i)).c_str()))
				{
					for (int k = 1; k < MAX_MAP_HULLS; k++)
					{
						if (i == k)
							continue;
						if (ImGui::MenuItem(("Hull " + std::to_string(k)).c_str(), "", false, anyHullValid[k]))
						{
							//for (int j = 0; j < app->mapRenderers.size(); j++) {
							//	Bsp* map = app->mapRenderers[j]->map;
							map->delete_hull(i, k);
							map->getBspRender()->reloadClipnodes();
							//	app->mapRenderers[j]->reloadClipnodes();
							print_log(get_localized_string(LANG_0361), i, k, map->bsp_name);
							//}
							checkValidHulls();
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_0572).c_str(), !app->isLoading && map))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0573).c_str()))
			{
				for (int i = 0; i < map->faceCount; i++)
				{
					BSPFACE32& face = map->faces[i];
					BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
					if (info.nFlags & TEX_SPECIAL)
					{
						continue;
					}
					int bmins[2];
					int bmaxs[2];
					if (!GetFaceExtents(map, i, bmins, bmaxs))
					{
						info.nFlags += TEX_SPECIAL;
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0574).c_str());
				ImGui::EndTooltip();
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0575).c_str()))
			{
				for (int i = 0; i < map->leafCount; i++)
				{
					for (int n = 0; n < 3; n++)
					{
						if (map->leaves[i].nMins[n] > map->leaves[i].nMaxs[n])
						{
							print_log(get_localized_string(LANG_0362), i);
							std::swap(map->leaves[i].nMins[n], map->leaves[i].nMaxs[n]);
						}
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0576).c_str());
				ImGui::EndTooltip();
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0577).c_str()))
			{
				for (int i = 0; i < map->modelCount; i++)
				{
					for (int n = 0; n < 3; n++)
					{
						if (map->models[i].nMins[n] > map->models[i].nMaxs[n])
						{
							print_log(get_localized_string(LANG_0363), i);
							std::swap(map->models[i].nMins[n], map->models[i].nMaxs[n]);
						}
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0578).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0579).c_str()))
			{
				for (int i = 0; i < map->marksurfCount; i++)
				{
					if (map->marksurfs[i] >= map->faceCount)
					{
						map->marksurfs[i] = 0;
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0580).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0581).c_str()))
			{
				std::set<int> used_models; // Protected map
				used_models.insert(0);

				for (auto const& s : map->ents)
				{
					int ent_mdl_id = s->getBspModelIdx();
					if (ent_mdl_id >= 0)
					{
						if (!used_models.count(ent_mdl_id))
						{
							used_models.insert(ent_mdl_id);
						}
					}
				}

				for (int i = 0; i < map->modelCount; i++)
				{
					if (!used_models.count(i))
					{
						Entity* ent = new Entity("func_wall");
						ent->setOrAddKeyvalue("model", "*" + std::to_string(i));
						ent->setOrAddKeyvalue("origin", map->models[i].vOrigin.toKeyvalueString());
						map->ents.push_back(ent);
					}
				}

				map->update_ent_lump();
				if (map->getBspRender())
				{
					app->reloading = true;
					map->getBspRender()->reload();
					app->reloading = false;
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0582).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0583).c_str()))
			{
				bool foundfixes = false;
				for (int i = 0; i < map->textureCount; i++)
				{
					int texOffset = ((int*)map->textures)[i + 1];
					if (texOffset >= 0)
					{
						int texlen = map->getBspTextureSize(i);
						int dataOffset = (map->textureCount + 1) * sizeof(int);
						BSPMIPTEX* tex = (BSPMIPTEX*)(map->textures + texOffset);
						if (tex->szName[0] == '\0' || strlen(tex->szName) >= MAXTEXTURENAME)
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1055), i);
						}
						if (tex->nOffsets[0] > 0 && dataOffset + texOffset + texlen > map->bsp_header.lump[LUMP_TEXTURES].nLength)
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0364), i, map->bsp_header.lump[LUMP_TEXTURES].nLength, dataOffset + texOffset + texlen);

							char* newlump = new char[dataOffset + texOffset + texlen];
							memset(newlump, 0, dataOffset + texOffset + texlen);
							memcpy(newlump, map->textures, map->bsp_header.lump[LUMP_TEXTURES].nLength);
							map->replace_lump(LUMP_TEXTURES, newlump, dataOffset + texOffset + texlen);
							foundfixes = true;
						}
					}
				}
				if (foundfixes)
				{
					map->update_lump_pointers();
				}
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0584).c_str()))
			{
				std::set<std::string> textureset = std::set<std::string>();

				for (int i = 0; i < map->faceCount; i++)
				{
					BSPFACE32& face = map->faces[i];
					BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
					if (info.iMiptex >= 0 && info.iMiptex < map->textureCount)
					{
						int texOffset = ((int*)map->textures)[info.iMiptex + 1];
						if (texOffset >= 0)
						{
							BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
							if (tex.nOffsets[0] <= 0 && tex.szName[0] != '\0')
							{
								if (textureset.count(tex.szName))
									continue;
								textureset.insert(tex.szName);
								bool textureFoundInWad = false;
								for (auto& s : map->getBspRender()->wads)
								{
									if (s->hasTexture(tex.szName))
									{
										textureFoundInWad = true;
										break;
									}
								}
								if (!textureFoundInWad)
								{
									COLOR3* imageData = new COLOR3[tex.nWidth * tex.nHeight];
									memset(imageData, 255, tex.nWidth * tex.nHeight * sizeof(COLOR3));
									map->add_texture(tex.szName, (unsigned char*)imageData, tex.nWidth, tex.nHeight);
									delete[] imageData;
								}
							}
							else if (tex.nOffsets[0] <= 0)
							{
								print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0365), i);
								memset(tex.szName, 0, MAXTEXTURENAME);
								memcpy(tex.szName, "aaatrigger", 10);
							}
						}
					}
				}
				map->getBspRender()->reuploadTextures();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0585).c_str());
				ImGui::TextUnformatted(get_localized_string(LANG_0586).c_str());
				ImGui::EndTooltip();
			}


			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(get_localized_string(LANG_0587).c_str(), (map && !map->is_mdl_model)))
	{
		if (ImGui::MenuItem(get_localized_string(LANG_0588).c_str(), 0, false, map))
		{
			Entity* newEnt = new Entity();
			vec3 origin = (cameraOrigin + app->cameraForward * 100);
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "info_player_deathmatch");

			CreateEntityCommand* createCommand = new CreateEntityCommand("Create Entity", app->getSelectedMapId(), newEnt);
			rend->pushUndoCommand(createCommand);

			delete newEnt;
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0589).c_str(), 0, false, !app->isLoading && map))
		{
			vec3 origin = cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "func_illusionary");

			float snapSize = pow(2.0f, app->gridSnapLevel * 1.0f);
			if (snapSize < 16)
			{
				snapSize = 16;
			}

			CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", app->getSelectedMapId(), newEnt, snapSize, true);
			rend->pushUndoCommand(command);

			delete newEnt;

			newEnt = map->ents[map->ents.size() - 1];
			if (newEnt && newEnt->getBspModelIdx() >= 0)
			{
				BSPMODEL& model = map->models[newEnt->getBspModelIdx()];
				for (int i = 0; i < model.nFaces; i++)
				{
					map->faces[model.iFirstFace + i].nStyles[0] = 0;
				}
			}
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0590).c_str(), 0, false, !app->isLoading && map))
		{
			vec3 origin = cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "trigger_once");

			float snapSize = pow(2.0f, app->gridSnapLevel * 1.0f);

			if (snapSize < 16)
			{
				snapSize = 16;
			}

			CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", app->getSelectedMapId(), newEnt, snapSize, false);
			rend->pushUndoCommand(command);

			delete newEnt;

			newEnt = map->ents[map->ents.size() - 1];
			if (newEnt && newEnt->getBspModelIdx() >= 0)
			{
				BSPMODEL& model = map->models[newEnt->getBspModelIdx()];
				model.iFirstFace = 0;
				model.nFaces = 0;
			}
		}

		if (ImGui::MenuItem(get_localized_string(LANG_0591).c_str(), 0, false, !app->isLoading && map))
		{
			vec3 origin = cameraOrigin + app->cameraForward * 100;
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);

			Entity* newEnt = new Entity();
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "func_wall");

			float snapSize = pow(2.0f, app->gridSnapLevel * 1.0f);
			if (snapSize < 16)
			{
				snapSize = 16;
			}

			CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", app->getSelectedMapId(), newEnt, snapSize, false);
			rend->pushUndoCommand(command);

			delete newEnt;

			newEnt = map->ents[map->ents.size() - 1];
			if (newEnt && newEnt->getBspModelIdx() >= 0)
			{
				BSPMODEL& model = map->models[newEnt->getBspModelIdx()];
				for (int i = 0; i < model.nFaces; i++)
				{
					map->faces[model.iFirstFace + i].nStyles[0] = 0;
				}
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(get_localized_string(LANG_0592).c_str()))
	{
		if (map && map->is_mdl_model)
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0594).c_str(), "", showLogWidget))
			{
				showLogWidget = !showLogWidget;
			}
		}
		else
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0595).c_str(), NULL, showDebugWidget))
			{
				showDebugWidget = !showDebugWidget;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0596).c_str(), get_localized_string(LANG_0477).c_str(), showKeyvalueWidget))
			{
				showKeyvalueWidget = !showKeyvalueWidget;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_1160).c_str(), get_localized_string(LANG_1161).c_str(), showTransformWidget))
			{
				showTransformWidget = !showTransformWidget;
			}
			if (ImGui::MenuItem("Go to", get_localized_string(LANG_1095).c_str(), showGOTOWidget))
			{
				showGOTOWidget = !showGOTOWidget;
				showGOTOWidget_update = true;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0597).c_str(), "", showFaceEditWidget))
			{
				showFaceEditWidget = !showFaceEditWidget;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0598).c_str(), "", showTextureBrowser))
			{
				showTextureBrowser = !showTextureBrowser;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0599).c_str(), "", showLightmapEditorWidget))
			{
				showLightmapEditorWidget = !showLightmapEditorWidget;
				FaceSelectePressed();
				showLightmapEditorUpdate = true;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0600).c_str(), "", showMergeMapWidget))
			{
				showMergeMapWidget = !showMergeMapWidget;
			}
			if (ImGui::MenuItem(get_localized_string(LANG_1096).c_str(), "", showLogWidget))
			{
				showLogWidget = !showLogWidget;
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(get_localized_string(LANG_0601).c_str()))
	{
		Bsp* selectedMap = app->getSelectedMap();
		for (BspRenderer* bspRend : app->mapRenderers)
		{
			if (bspRend->map && !bspRend->map->is_bsp_model)
			{
				if (ImGui::MenuItem(bspRend->map->bsp_name.c_str(), NULL, selectedMap == bspRend->map))
				{
					selectedMap->getBspRender()->renderCameraAngles = cameraAngles;
					selectedMap->getBspRender()->renderCameraOrigin = cameraOrigin;
					app->deselectObject();
					app->clearSelection();
					app->selectMap(bspRend->map);
					cameraAngles = bspRend->renderCameraAngles;
					cameraOrigin = bspRend->renderCameraOrigin;
					makeVectors(cameraAngles, app->cameraForward, app->cameraRight, app->cameraUp);
				}
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(get_localized_string(LANG_0602).c_str()))
	{
		if (ImGui::MenuItem(get_localized_string(LANG_0603).c_str()))
		{
			showHelpWidget = true;
		}
		if (ImGui::MenuItem(get_localized_string(LANG_0604).c_str()))
		{
			showAboutWidget = true;
		}
		ImGui::EndMenu();
	}

	if (DebugKeyPressed)
	{
		if (ImGui::BeginMenu(get_localized_string(LANG_0605).c_str()))
		{
			ImGui::EndMenu();
		}
	}

	ImGui::EndMainMenuBar();
}

void Gui::drawToolbar()
{
	ImVec2 window_pos = ImVec2(10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin(get_localized_string(LANG_0606).c_str(), 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImGuiContext& g = *GImGui;
		ImVec4 dimColor = style.Colors[ImGuiCol_FrameBg];
		ImVec4 selectColor = style.Colors[ImGuiCol_FrameBgActive];
		float iconWidth = (fontSize / 22.0f) * 32;
		ImVec2 iconSize = ImVec2(iconWidth, iconWidth);
		ImVec4 testColor = ImVec4(1, 0, 0, 1);
		selectColor.x *= selectColor.w;
		selectColor.y *= selectColor.w;
		selectColor.z *= selectColor.w;
		selectColor.w = 1;

		dimColor.x *= dimColor.w;
		dimColor.y *= dimColor.w;
		dimColor.z *= dimColor.w;
		dimColor.w = 1;

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_OBJECT ? selectColor : dimColor);
		if (ImGui::ImageButton((void*)(uint64_t)objectIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1), 4))
		{
			app->deselectFaces();
			app->deselectObject();
			app->pickMode = PICK_OBJECT;
			showFaceEditWidget = false;
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(get_localized_string(LANG_0607).c_str());
			ImGui::EndTooltip();
		}

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_FACE ? selectColor : dimColor);
		ImGui::SameLine();
		if (ImGui::ImageButton((void*)(uint64_t)faceIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1), 4))
		{
			FaceSelectePressed();
			showFaceEditWidget = true;
		}
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(get_localized_string(LANG_0608).c_str());
			ImGui::EndTooltip();
		}
	}
	ImGui::End();
}

void Gui::FaceSelectePressed()
{
	if (app->pickInfo.GetSelectedEnt() >= 0 && app->pickMode == PICK_FACE)
	{
		Bsp* map = app->getSelectedMap();
		if (map)
		{
			int modelIdx = map->ents[app->pickInfo.GetSelectedEnt()]->getBspModelIdx();
			if (modelIdx >= 0)
			{
				BspRenderer* mapRenderer = map->getBspRender();
				BSPMODEL& model = map->models[modelIdx];
				for (int i = 0; i < model.nFaces; i++)
				{
					int faceIdx = model.iFirstFace + i;
					mapRenderer->highlightFace(faceIdx, true);
					app->pickInfo.selectedFaces.push_back(faceIdx);
				}
			}
		}
	}

	if (app->pickMode != PICK_FACE)
		app->deselectObject();

	app->pickMode = PICK_FACE;
	pickCount++; // force texture tool refresh
}

void Gui::drawFpsOverlay()
{
	ImVec2 window_pos = ImVec2(imgui_io->DisplaySize.x - 10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin(get_localized_string(LANG_0609).c_str(), 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGui::Text(get_localized_string(LANG_0610).c_str(), imgui_io->Framerate);
		if (ImGui::BeginPopupContextWindow())
		{
			ImGui::Checkbox(get_localized_string(LANG_0611).c_str(), &g_settings.vsync);
			ImGui::EndPopup();
		}
	}
	ImGui::End();
}

void Gui::drawStatusMessage()
{
	static float windowWidth = 32;
	static float loadingWindowWidth = 32;
	static float loadingWindowHeight = 32;

	bool selectedEntity = false;
	Bsp* map = app->getSelectedMap();
	for (auto& i : app->pickInfo.selectedEnts)
	{
		if (map && i >= 0 && (map->ents[i]->getBspModelIdx() < 0 || map->ents[i]->isWorldSpawn()))
		{
			selectedEntity = true;
			break;
		}
	}

	bool showStatus = (app->invalidSolid && !selectedEntity) || !app->isTransformableSolid || badSurfaceExtents || lightmapTooLarge || app->modelUsesSharedStructures;

	if (showStatus)
	{
		ImVec2 window_pos = ImVec2((app->windowWidth - windowWidth) / 2.f, app->windowHeight - 10.f);
		ImVec2 window_pos_pivot = ImVec2(0.0f, 1.0f);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
		ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background

		if (ImGui::Begin(get_localized_string(LANG_0612).c_str(), 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			if (app->modelUsesSharedStructures)
			{
				if (app->transformMode == TRANSFORM_MODE_MOVE && !app->moveOrigin)
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0613).c_str());
				else
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0614).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Model shares planes/clipnodes with other models.\n\nNeed duplicate the model to enable model editing.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (!app->isTransformableSolid && app->pickInfo.selectedEnts.size() > 0 && app->pickInfo.selectedEnts[0] >= 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0615).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"Scaling and vertex manipulation don't work with concave solids yet\n";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (app->invalidSolid && app->pickInfo.selectedEnts.size() > 0 && app->pickInfo.selectedEnts[0] >= 0)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0616).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"The selected solid is not convex or has non-planar faces.\n\n"
						"Transformations will be reverted unless you fix this.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (badSurfaceExtents)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0617).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"One or more of the selected faces contain too many texture pixels on some axis.\n\n"
						"This will crash the game. Increase texture scale to fix.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			if (lightmapTooLarge)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), get_localized_string(LANG_0618).c_str());
				if (ImGui::IsItemHovered())
				{
					const char* info =
						"One or more of the selected faces contain too many texture pixels.\n\n"
						"This will crash the game. Increase texture scale to fix.";
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(info);
					ImGui::EndTooltip();
				}
			}
			windowWidth = ImGui::GetWindowWidth();
		}
		ImGui::End();
	}

	if (app->isLoading)
	{
		ImVec2 window_pos = ImVec2((app->windowWidth - loadingWindowWidth) / 2,
			(app->windowHeight - loadingWindowHeight) / 2);
		ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);

		if (ImGui::Begin(get_localized_string(LANG_0619).c_str(), 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			static clock_t lastTick = clock();
			static int loadTick = 0;

			if (clock() - lastTick / (float)CLOCKS_PER_SEC > 0.05f)
			{
				loadTick = (loadTick + 1) % 8;
				lastTick = clock();
			}

			ImGui::PushFont(consoleFontLarge);
			switch (loadTick)
			{
			case 0: ImGui::Text(get_localized_string(LANG_0620).c_str()); break;
			case 1: ImGui::Text(get_localized_string(LANG_0621).c_str()); break;
			case 2: ImGui::Text(get_localized_string(LANG_0622).c_str()); break;
			case 3: ImGui::Text(get_localized_string(LANG_0623).c_str()); break;
			case 4: ImGui::Text(get_localized_string(LANG_1097).c_str()); break;
			case 5: ImGui::Text(get_localized_string(LANG_1098).c_str()); break;
			case 6: ImGui::Text(get_localized_string(LANG_1099).c_str()); break;
			case 7: ImGui::Text(get_localized_string(LANG_1162).c_str()); break;
			default:  break;
			}
			ImGui::PopFont();

		}
		loadingWindowWidth = ImGui::GetWindowWidth();
		loadingWindowHeight = ImGui::GetWindowHeight();

		ImGui::End();
	}
}

void Gui::drawDebugWidget()
{
	static std::map<std::string, std::set<std::string>> mapTexsUsage{};
	static double lastupdate = 0.0;

	ImGui::SetNextWindowBgAlpha(0.75f);

	ImGui::SetNextWindowSize(ImVec2(300.f, 400.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200.f, 200.f), ImVec2(app->windowWidth - 40.f, app->windowHeight - 40.f));

	Bsp* map = app->getSelectedMap();
	BspRenderer* renderer = map ? map->getBspRender() : NULL;
	int entIdx = app->pickInfo.GetSelectedEnt();

	int debugVisMode = 0;

	if (ImGui::Begin(get_localized_string(LANG_0624).c_str(), &showDebugWidget))
	{
		if (ImGui::CollapsingHeader(get_localized_string(LANG_0625).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0366)), std::ceil(cameraOrigin.x), std::ceil(cameraOrigin.y), std::ceil(cameraOrigin.z)).c_str());
			ImGui::Text(fmt::format("Mouse: {} {}", mousePos.x, mousePos.y).c_str());
			ImGui::Text(fmt::format("Mouse left {} right {}", app->curLeftMouse, app->curRightMouse).c_str());
			ImGui::Text(fmt::format("Time: {}", app->curTime).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0367)), std::ceil(cameraAngles.x), std::ceil(cameraAngles.y), std::ceil(cameraAngles.z)).c_str());

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0368)), (unsigned int)app->pickInfo.selectedFaces.size()).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0369)), app->pickMode).c_str());
		}
		if (ImGui::CollapsingHeader(get_localized_string(LANG_1100).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!map)
			{
				ImGui::Text(get_localized_string(LANG_0626).c_str());
			}
			else
			{
				ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0370)), map->bsp_name.c_str()).c_str());

				if (ImGui::CollapsingHeader(get_localized_string(LANG_0627).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (app->pickInfo.selectedEnts.size())
					{
						ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0371)), entIdx).c_str());
					}

					int modelIdx = -1;

					if (entIdx >= 0)
					{
						modelIdx = map->ents[entIdx]->getBspModelIdx();
					}


					if (modelIdx > 0)
					{
						ImGui::Checkbox(get_localized_string(LANG_0628).c_str(), &app->debugClipnodes);
						ImGui::SliderInt(get_localized_string(LANG_0629).c_str(), &app->debugInt, 0, app->debugIntMax);

						ImGui::Checkbox(get_localized_string(LANG_0630).c_str(), &app->debugNodes);
						ImGui::SliderInt(get_localized_string(LANG_0631).c_str(), &app->debugNode, 0, app->debugNodeMax);
					}

					if (app->pickInfo.selectedFaces.size())
					{
						BSPFACE32& face = map->faces[app->pickInfo.selectedFaces[0]];

						if (modelIdx > 0)
						{
							BSPMODEL& model = map->models[modelIdx];
							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0372)), modelIdx).c_str());

							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0373)), model.nFaces).c_str());
						}

						ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0374)), app->pickInfo.selectedFaces[0]).c_str());
						ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0375)), face.iPlane).c_str());

						if (face.iTextureInfo < map->texinfoCount)
						{
							BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
							if (info.iMiptex >= 0 && info.iMiptex < map->textureCount)
							{
								int texOffset = ((int*)map->textures)[info.iMiptex + 1];
								if (texOffset >= 0)
								{
									BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
									ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0376)), face.iTextureInfo).c_str());
									ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0377)), info.iMiptex).c_str());
									ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0378)), tex.szName, tex.nWidth, tex.nHeight).c_str());
								}
							}
							BSPPLANE& plane = map->planes[face.iPlane];
							BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
							float anglex, angley;
							vec3 xv, yv;
							int val = TextureAxisFromPlane(plane, xv, yv);
							ImGui::Text(fmt::format("Plane type {} : axis ({}x{})", val, anglex = AngleFromTextureAxis(texinfo.vS, true, val),
								angley = AngleFromTextureAxis(texinfo.vT, false, val)).c_str());
							ImGui::Text(fmt::format("Texinfo: {}/{}/{} + {} / {}/{}/{} + {} ", texinfo.vS.x, texinfo.vS.y, texinfo.vS.z, texinfo.shiftS,
								texinfo.vT.x, texinfo.vT.y, texinfo.vT.z, texinfo.shiftT).c_str());

							xv = AxisFromTextureAngle(anglex, true, val);
							yv = AxisFromTextureAngle(angley, false, val);

							ImGui::Text(fmt::format("AxisBack: {}/{}/{} + {} / {}/{}/{} + {} ", xv.x, xv.y, xv.z, texinfo.shiftS,
								yv.x, yv.y, yv.z, texinfo.shiftT).c_str());

						}
						ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0379)), face.nLightmapOffset).c_str());
					}
				}
			}
		}
		int modelIdx = -1;

		if (map && entIdx >= 0)
		{
			modelIdx = map->ents[entIdx]->getBspModelIdx();
		}

		std::string bspTreeTitle = "BSP Tree";
		if (modelIdx >= 0)
		{
			bspTreeTitle += " (Model " + std::to_string(modelIdx) + ")";
		}

		if (ImGui::CollapsingHeader((bspTreeTitle + "##bsptree").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (modelIdx < 0 && entIdx >= 0)
				modelIdx = 0;
			if (modelIdx >= 0)
			{
				if (!map || !renderer)
				{
					ImGui::Text(get_localized_string(LANG_0632).c_str());
				}
				else
				{
					vec3 localCamera = cameraOrigin - renderer->mapOffset;

					static ImVec4 hullColors[] = {
						ImVec4(1, 1, 1, 1),
						ImVec4(0.3f, 1, 1, 1),
						ImVec4(1, 0.3f, 1, 1),
						ImVec4(1, 1, 0.3f, 1),
					};

					for (int i = 0; i < MAX_MAP_HULLS; i++)
					{
						std::vector<int> nodeBranch;
						int leafIdx;
						int childIdx = -1;
						int headNode = map->models[modelIdx].iHeadnodes[i];
						int contents = map->pointContents(headNode, localCamera, i, nodeBranch, leafIdx, childIdx);

						ImGui::PushStyleColor(ImGuiCol_Text, hullColors[i]);
						if (ImGui::TreeNode(("HULL " + std::to_string(i)).c_str()))
						{
							ImGui::Indent();
							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0380)), map->getLeafContentsName(contents)).c_str());
							if (i == 0)
							{
								ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0381)), leafIdx).c_str());
							}
							ImGui::Text(fmt::format("Parent Node: {} (child {})",
								nodeBranch.size() ? nodeBranch[nodeBranch.size() - 1] : headNode,
								childIdx).c_str());
							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0382)), headNode).c_str());
							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0383)), nodeBranch.size()).c_str());

							ImGui::Unindent();
							ImGui::TreePop();
						}
						ImGui::PopStyleColor();
					}
				}
			}
			else
			{
				ImGui::Text(get_localized_string(LANG_0633).c_str());
			}
		}

		if (map && ImGui::CollapsingHeader(get_localized_string(LANG_0634).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			int InternalTextures = 0;
			int TotalInternalTextures = 0;
			int WadTextures = 0;

			for (int i = 0; i < map->textureCount; i++)
			{
				int oldOffset = ((int*)map->textures)[i + 1];
				if (oldOffset > 0)
				{
					BSPMIPTEX* bspTex = (BSPMIPTEX*)(map->textures + oldOffset);
					if (bspTex->nOffsets[0] > 0)
					{
						TotalInternalTextures++;
					}
				}
			}

			if (mapTexsUsage.size())
			{
				for (auto& tmpWad : mapTexsUsage)
				{
					if (tmpWad.first == "internal")
						InternalTextures += (int)tmpWad.second.size();
					else
						WadTextures += (int)tmpWad.second.size();
				}
			}

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0384)), map->textureCount).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0385)), InternalTextures, TotalInternalTextures).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0386)), TotalInternalTextures > 0 ? (int)mapTexsUsage.size() - 1 : (int)mapTexsUsage.size()).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0387)), WadTextures).c_str());

			for (auto& tmpWad : mapTexsUsage)
			{
				if (ImGui::CollapsingHeader((tmpWad.first + "##debug").c_str(), ImGuiTreeNodeFlags_::ImGuiTreeNodeFlags_Bullet
					| ImGuiTreeNodeFlags_::ImGuiTreeNodeFlags_Framed))
				{
					for (auto& texName : tmpWad.second)
					{
						ImGui::Text(texName.c_str());
					}
				}
			}
		}

		if (map && renderer && ImGui::CollapsingHeader(get_localized_string(LANG_1101).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text(get_localized_string(LANG_0635).c_str(), app->debugVec0.x, app->debugVec0.y, app->debugVec0.z);
			ImGui::Text(get_localized_string(LANG_0636).c_str(), app->debugVec1.x, app->debugVec1.y, app->debugVec1.z);
			ImGui::Text(get_localized_string(LANG_0637).c_str(), app->debugVec2.x, app->debugVec2.y, app->debugVec2.z);
			ImGui::Text(get_localized_string(LANG_0638).c_str(), app->debugVec3.x, app->debugVec3.y, app->debugVec3.z);

			float mb = renderer->undoMemoryUsage / (1024.0f * 1024.0f);
			ImGui::Text(get_localized_string(LANG_0639).c_str(), mb);

			bool isScalingObject = app->transformMode == TRANSFORM_MODE_SCALE && app->transformTarget == TRANSFORM_OBJECT;
			bool isMovingOrigin = app->transformMode == TRANSFORM_MODE_MOVE && app->transformTarget == TRANSFORM_ORIGIN && app->originSelected;
			bool isTransformingValid = !(app->modelUsesSharedStructures && app->transformMode != TRANSFORM_MODE_MOVE) && (app->isTransformableSolid || isScalingObject);
			bool isTransformingWorld = entIdx == 0 && app->transformTarget != TRANSFORM_OBJECT;

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0388)), app->isTransformableSolid).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0389)), isScalingObject).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0390)), isMovingOrigin).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0391)), isTransformingValid).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0392)), isTransformingWorld).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0393)), app->transformMode).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0394)), app->transformTarget).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0395)), app->modelUsesSharedStructures).c_str());

			ImGui::Text(fmt::format("showDragAxes {}\nmovingEnt {}\nanyAltPressed {}",
				app->showDragAxes, app->movingEnt, app->anyAltPressed).c_str());

			ImGui::Checkbox(get_localized_string(LANG_0640).c_str(), &app->showDragAxes);
		}

		if (map)
		{
			if (ImGui::Button(get_localized_string(LANG_0641).c_str()))
			{
				for (auto& ent : map->ents)
				{
					if (ent->hasKey("classname") && ent->keyvalues["classname"] == "infodecal")
					{
						map->decalShoot(ent->getOrigin(), "Hello world");
					}
				}
			}

			/*if (ImGui::Button(get_localized_string(LANG_0642).c_str()))
			{
				debugVisMode = 1;
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0643).c_str());
				ImGui::TextUnformatted(get_localized_string(LANG_0644).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			*/
			if (ImGui::Button(get_localized_string(LANG_0645).c_str()))
			{
				debugVisMode = 2;
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0646).c_str());
				ImGui::TextUnformatted(get_localized_string(LANG_1102).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			static int model1 = 0;
			static int model2 = 0;

			ImGui::DragInt(get_localized_string(LANG_0647).c_str(), &model1, 1, 0, MAX_MAP_MODELS);

			ImGui::DragInt(get_localized_string(LANG_0648).c_str(), &model2, 1, 0, MAX_MAP_MODELS);

			if (ImGui::Button(get_localized_string(LANG_0649).c_str()))
			{
				if (model1 >= 0 && model2 >= 0)
				{
					if (model1 != model2)
					{
						if (model1 < map->modelCount &&
							model2 < map->modelCount)
						{
							std::swap(map->models[model1], map->models[model2]);


							for (int i = 0; i < map->ents.size(); i++)
							{
								if (map->ents[i]->getBspModelIdx() == model1)
								{
									map->ents[i]->setOrAddKeyvalue("model", "*" + std::to_string(model2));
								}
								else if (map->ents[i]->getBspModelIdx() == model2)
								{
									map->ents[i]->setOrAddKeyvalue("model", "*" + std::to_string(model1));
								}
							}
						}
					}
				}
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0650).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
		}

	}

	if (renderer && map && renderer->needReloadDebugTextures)
	{
		renderer->needReloadDebugTextures = false;
		lastupdate = app->curTime;
		mapTexsUsage.clear();

		for (int i = 0; i < map->faceCount; i++)
		{
			BSPTEXTUREINFO& texinfo = map->texinfos[map->faces[i].iTextureInfo];
			if (texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount)
			{
				int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
				if (texOffset >= 0)
				{
					BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));

					if (tex.szName[0] != '\0')
					{
						if (tex.nOffsets[0] <= 0)
						{
							bool fondTex = false;
							for (auto& s : renderer->wads)
							{
								if (s->hasTexture(tex.szName))
								{
									if (!mapTexsUsage[basename(s->filename)].count(tex.szName))
										mapTexsUsage[basename(s->filename)].insert(tex.szName);

									fondTex = true;
								}
							}
							if (!fondTex)
							{
								if (!mapTexsUsage["notfound"].count(tex.szName))
									mapTexsUsage["notfound"].insert(tex.szName);
							}
						}
						else
						{
							if (!mapTexsUsage["internal"].count(tex.szName))
								mapTexsUsage["internal"].insert(tex.szName);
						}
					}
				}
			}
		}

		for (size_t i = 0; i < map->ents.size(); i++)
		{
			if (map->ents[i]->hasKey("classname") && map->ents[i]->keyvalues["classname"] == "infodecal")
			{
				if (map->ents[i]->hasKey("texture"))
				{
					std::string texture = map->ents[i]->keyvalues["texture"];
					if (!mapTexsUsage["decals.wad"].count(texture))
						mapTexsUsage["decals.wad"].insert(texture);
				}
			}
		}

		if (mapTexsUsage.size())
			print_log(get_localized_string(LANG_0396), (int)mapTexsUsage.size());
	}
	ImGui::End();


	if (debugVisMode > 0 && !g_app->reloading && renderer)
	{
		vec3 localCamera = cameraOrigin - renderer->mapOffset;

		vec3 renderOffset;
		vec3 mapOffset = map->ents.size() ? map->ents[0]->getOrigin() : vec3();
		renderOffset = mapOffset.flip();

		std::vector<int> nodeBranch;
		int childIdx = -1;
		int leafIdx = -1;
		int headNode = map->models[0].iHeadnodes[0];
		map->pointContents(headNode, localCamera, 0, nodeBranch, leafIdx, childIdx);

		BSPLEAF32& leaf = map->leaves[leafIdx];
		int thisLeafCount = map->leafCount;

		int oldVisRowSize = ((thisLeafCount + 63) & ~63) >> 3;

		unsigned char* visData = new unsigned char[oldVisRowSize];
		memset(visData, 0xFF, oldVisRowSize);
		//DecompressLeafVis(map->visdata + leaf.nVisOffset, map->leafCount - leaf.nVisOffset, visData, map->leafCount);
		DecompressVis(map->visdata + leaf.nVisOffset, visData, oldVisRowSize, map->leafCount, map->visDataLength - leaf.nVisOffset);


		for (int l = 0; l < map->leafCount - 1; l++)
		{
			if (l == leafIdx || CHECKVISBIT(visData, l))
			{
			}
			else
			{
				// TODO: need precompile it at map start, for fast access?
				auto faceList = map->getLeafFaces(l + 1);
				for (const auto& idx : faceList)
				{
					renderer->highlightFace(idx, true, COLOR4(230 + rand() % 25, 0, 0, 255), true);
				}
			}
		}

		for (int l = 0; l < map->leafCount - 1; l++)
		{
			if (l == leafIdx || CHECKVISBIT(visData, l))
			{
				auto faceList = map->getLeafFaces(l + 1);
				for (const auto& idx : faceList)
				{
					renderer->highlightFace(idx, true, COLOR4(0, 0, 230 + rand() % 25, 255), true);
				}
			}
		}
		delete[] visData;
	}
}

void Gui::drawTextureBrowser()
{
	Bsp* map = app->getSelectedMap();
	BspRenderer* mapRender = map ? map->getBspRender() : NULL;
	ImGui::SetNextWindowSize(ImVec2(610.f, 610.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));
	//ImGui::SetNextWindowContentSize(ImVec2(550, 0.0f));
	if (ImGui::Begin(get_localized_string(LANG_0651).c_str(), &showTextureBrowser, 0))
	{
		// Список встроенных в карту текстур, с возможностью Удалить/Экспортировать/Импортировать/Переименовать
		// Список встроенных в карту WAD текстур, с возможностью Удалить/
		// Список всех WAD файлов и доступных текстур, с возможностью добавления в карту ссылки или копии текстуры.
		if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_::ImGuiTabBarFlags_FittingPolicyScroll |
			ImGuiTabBarFlags_::ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
			ImGuiTabBarFlags_::ImGuiTabBarFlags_Reorderable))
		{
			ImGui::Dummy(ImVec2(0, 10));
			if (ImGui::BeginTabItem(get_localized_string(LANG_0652).c_str()))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGuiListClipper clipper;
				clipper.Begin(1, 30.0f);
				while (clipper.Step())
				{

				}
				clipper.End();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(get_localized_string(LANG_0653).c_str()))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGuiListClipper clipper;
				clipper.Begin(1, 30.0f);
				while (clipper.Step())
				{

				}
				clipper.End();
				ImGui::EndTabItem();
			}

			if (mapRender)
			{
				for (auto& wad : mapRender->wads)
				{
					if (ImGui::BeginTabItem(basename(wad->filename).c_str()))
					{
						ImGui::Dummy(ImVec2(0, 10));
						ImGuiListClipper clipper;
						clipper.Begin(1, 30.0f);
						while (clipper.Step())
						{

						}
						clipper.End();
						ImGui::EndTabItem();
					}
				}
			}
		}
		ImGui::EndTabBar();

	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor()
{
	ImGui::SetNextWindowSize(ImVec2(610.f, 610.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));
	//ImGui::SetNextWindowContentSize(ImVec2(550, 0.0f));
	if (ImGui::Begin(get_localized_string(LANG_1103).c_str(), &showKeyvalueWidget, 0))
	{
		int entIdx = app->pickInfo.GetSelectedEnt();
		Bsp* map = app->getSelectedMap();
		if (entIdx >= 0 && app->fgd
			&& !app->isLoading && !app->isModelsReloading && !app->reloading && map)
		{

			Entity* ent = map->ents[entIdx];
			std::string cname = ent->keyvalues["classname"];
			FgdClass* fgdClass = app->fgd->getFgdClass(cname);

			ImGui::PushFont(largeFont);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(get_localized_string(LANG_0654).c_str()); ImGui::SameLine();
			if (cname != "worldspawn")
			{
				if (ImGui::Button((" " + cname + " ").c_str()))
					ImGui::OpenPopup(get_localized_string(LANG_0655).c_str());
			}
			else
			{
				ImGui::Text(cname.c_str());
			}
			ImGui::PopFont();

			if (fgdClass)
			{
				ImGui::SameLine();
				ImGui::Text("(?)");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted((fgdClass->description).c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
			}


			if (ImGui::BeginPopup(get_localized_string(LANG_1104).c_str()))
			{
				ImGui::Text(get_localized_string(LANG_0656).c_str());
				ImGui::Separator();

				std::vector<FgdGroup> targetGroup = app->fgd->pointEntGroups;

				if (fgdClass)
				{
					if (fgdClass->classType == FGD_CLASS_TYPES::FGD_CLASS_SOLID)
					{
						targetGroup = app->fgd->solidEntGroups;
					}
				}
				else if (ent->hasKey("model") && ent->keyvalues["model"].starts_with('*'))
				{
					targetGroup = app->fgd->solidEntGroups;
				}

				for (FgdGroup& group : targetGroup)
				{
					if (ImGui::BeginMenu(group.groupName.c_str()))
					{
						for (int k = 0; k < group.classes.size(); k++)
						{
							if (ImGui::MenuItem(group.classes[k]->name.c_str()))
							{
								ent->setOrAddKeyvalue("classname", group.classes[k]->name);
								map->getBspRender()->refreshEnt(entIdx);
								map->getBspRender()->pushEntityUndoState("Change Class", entIdx);
							}
						}

						ImGui::EndMenu();
					}
				}

				ImGui::EndPopup();
			}

			ImGui::Dummy(ImVec2(0, 10));

			if (ImGui::BeginTabBar(get_localized_string(LANG_0657).c_str()))
			{
				if (ImGui::BeginTabItem(get_localized_string(LANG_0658).c_str()))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_SmartEditTab(entIdx);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_0659).c_str()))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_FlagsTab(entIdx);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_0660).c_str()))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_RawEditTab(entIdx);
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();

		}
		else
		{
			if (entIdx < 0)
				ImGui::Text(get_localized_string(LANG_0661).c_str());
			else
				ImGui::Text(get_localized_string(LANG_0662).c_str());
		}

	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor_SmartEditTab(int entIdx)
{
	Bsp* map = app->getSelectedMap();
	if (!map || entIdx < 0)
	{
		ImGui::Text(get_localized_string(LANG_1105).c_str());
		return;
	}
	Entity* ent = map->ents[entIdx];
	std::string cname = ent->keyvalues["classname"];
	FgdClass* fgdClass = app->fgd->getFgdClass(cname);
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::BeginChild(get_localized_string(LANG_0663).c_str());

	ImGui::Columns(2, get_localized_string(LANG_0664).c_str(), false); // 4-ways, with border

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - (paddingx * 2)) * 0.5f;

	// needed if autoresize is true
	if (ImGui::GetScrollMaxY() > 0)
		inputWidth -= style.ScrollbarSize * 0.5f;

	struct InputData
	{
		std::string key;
		std::string defaultValue;
		Entity* entRef;
		int entIdx;
		BspRenderer* bspRenderer;

		InputData()
		{
			key = std::string();
			defaultValue = std::string();
			entRef = NULL;
			entIdx = 0;
			bspRenderer = 0;
		}
	};

	if (fgdClass)
	{
		static InputData inputData[128];
		static int lastPickCount = 0;

		if (ent->hasKey("model"))
		{
			bool foundmodel = false;
			for (int i = 0; i < fgdClass->keyvalues.size(); i++)
			{
				KeyvalueDef& keyvalue = fgdClass->keyvalues[i];
				std::string key = keyvalue.name;
				if (key == "model")
				{
					foundmodel = true;
				}
			}
			if (!foundmodel)
			{
				KeyvalueDef keyvalue = KeyvalueDef();
				keyvalue.name = "model";
				keyvalue.defaultValue =
					keyvalue.shortDescription = "Model";
				keyvalue.iType = FGD_KEY_STRING;
				fgdClass->keyvalues.push_back(keyvalue);
			}
		}

		for (int i = 0; i < fgdClass->keyvalues.size() && i < 128; i++)
		{
			KeyvalueDef& keyvalue = fgdClass->keyvalues[i];
			std::string key = keyvalue.name;
			if (key == "spawnflags")
			{
				continue;
			}
			std::string value = ent->keyvalues[key];
			std::string niceName = keyvalue.shortDescription;

			if (!strlen(value) && strlen(keyvalue.defaultValue))
			{
				value = keyvalue.defaultValue;
			}

			if (niceName.size() >= MAX_KEY_LEN)
				niceName = niceName.substr(0, MAX_KEY_LEN - 1);
			if (value.size() >= MAX_VAL_LEN)
				value = value.substr(0, MAX_VAL_LEN - 1);

			inputData[i].key = key;
			inputData[i].defaultValue = keyvalue.defaultValue;
			inputData[i].entIdx = entIdx;
			inputData[i].entRef = ent;
			inputData[i].bspRenderer = map->getBspRender();

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(niceName.c_str()); ImGui::NextColumn();
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::PushStyleColor(ImGuiCol_Text, { 0.9f,0.4f,0.2f,1.0f });
				ImGui::TextUnformatted(keyvalue.shortDescription.c_str());
				ImGui::PopStyleColor();
				if (keyvalue.fullDescription.size())
					ImGui::TextUnformatted(keyvalue.fullDescription.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::SetNextItemWidth(inputWidth);

			if (keyvalue.iType == FGD_KEY_CHOICES && !keyvalue.choices.empty())
			{
				std::string selectedValue = keyvalue.choices[0].name;
				int ikey = atoi(value.c_str());

				for (int k = 0; k < keyvalue.choices.size(); k++)
				{
					KeyvalueChoice& choice = keyvalue.choices[k];

					if ((choice.isInteger && ikey == choice.ivalue) ||
						(!choice.isInteger && value == choice.svalue))
					{
						selectedValue = choice.name;
					}
				}

				if (ImGui::BeginCombo(("##comboval" + std::to_string(i)).c_str(), selectedValue.c_str()))
				{
					for (int k = 0; k < keyvalue.choices.size(); k++)
					{
						KeyvalueChoice& choice = keyvalue.choices[k];
						bool selected = choice.svalue == value || (value.empty() && choice.svalue == keyvalue.defaultValue);
						bool needrefreshmodel = false;
						if (ImGui::Selectable(choice.name.c_str(), selected))
						{
							if (key == "renderamt")
							{
								if (ent->hasKey("renderamt") && ent->keyvalues["renderamt"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendermode")
							{
								if (ent->hasKey("rendermode") && ent->keyvalues["rendermode"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "renderfx")
							{
								if (ent->hasKey("renderfx") && ent->keyvalues["renderfx"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendercolor")
							{
								if (ent->hasKey("rendercolor") && ent->keyvalues["rendercolor"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}

							ent->setOrAddKeyvalue(key, choice.svalue);
							map->getBspRender()->refreshEnt(entIdx);
							map->getBspRender()->pushEntityUndoState("Edit Keyvalue", entIdx);

							map->getBspRender()->refreshEnt(inputData->entIdx);
							pickCount++;
							vertPickCount++;

							if (needrefreshmodel)
							{
								if (map && ent->getBspModelIdx() > 0)
								{
									map->getBspRender()->refreshModel(ent->getBspModelIdx());
									map->getBspRender()->preRenderEnts();
									g_app->updateEntConnections();
								}
							}
							g_app->updateEntConnections();
						}
					}

					ImGui::EndCombo();
				}
			}
			else
			{
				struct InputChangeCallback
				{
					static int keyValueChanged(ImGuiInputTextCallbackData* data)
					{
						if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
						{
							if (data->EventChar < 256)
							{
								if (strchr("-0123456789", (char)data->EventChar))
									return 0;
							}
							return 1;
						}
						InputData* linputData = (InputData*)data->UserData;
						if (!data->Buf || !strlen(linputData->key))
							return 0;

						Entity* ent = linputData->entRef;


						std::string newVal = data->Buf;

						bool needReloadModel = false;
						bool needRefreshModel = false;

						if (!g_app->reloading && !g_app->isModelsReloading && linputData->key == "model")
						{
							if (ent->hasKey("model") && ent->keyvalues["model"] != newVal)
							{
								needReloadModel = true;
							}
						}

						if (linputData->key == "renderamt")
						{
							if (ent->hasKey("renderamt") && ent->keyvalues["renderamt"] != newVal)
							{
								needRefreshModel = true;
							}
						}
						if (linputData->key == "rendermode")
						{
							if (ent->hasKey("rendermode") && ent->keyvalues["rendermode"] != newVal)
							{
								needRefreshModel = true;
							}
						}
						if (linputData->key == "renderfx")
						{
							if (ent->hasKey("renderfx") && ent->keyvalues["renderfx"] != newVal)
							{
								needRefreshModel = true;
							}
						}
						if (linputData->key == "rendercolor")
						{
							if (ent->hasKey("rendercolor") && ent->keyvalues["rendercolor"] != newVal)
							{
								needRefreshModel = true;
							}
						}


						if (!strlen(newVal))
						{
							ent->setOrAddKeyvalue(linputData->key, linputData->defaultValue);
						}
						else
						{
							ent->setOrAddKeyvalue(linputData->key, newVal);
						}

						linputData->bspRenderer->refreshEnt(linputData->entIdx);

						pickCount++;
						vertPickCount++;
						if (needReloadModel)
							g_app->reloadBspModels();

						if (needRefreshModel)
						{
							Bsp* map = g_app->getSelectedMap();
							if (map && ent->getBspModelIdx() > 0)
							{
								map->getBspRender()->refreshModel(ent->getBspModelIdx());
								map->getBspRender()->preRenderEnts();
							}
						}

						g_app->updateEntConnections();

						return 1;
					}
				};

				if (keyvalue.iType == FGD_KEY_INTEGER)
				{
					ImGui::InputText(("##inval" + std::to_string(i)).c_str(), &ent->keyvalues[key.c_str()],
						ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit,
						InputChangeCallback::keyValueChanged, &inputData[i]);
				}
				else
				{
					ImGui::InputText(("##inval" + std::to_string(i)).c_str(), &ent->keyvalues[key.c_str()], ImGuiInputTextFlags_CallbackEdit, InputChangeCallback::keyValueChanged, &inputData[i]);
				}


			}

			ImGui::NextColumn();
		}

		lastPickCount = pickCount;
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_FlagsTab(int entIdx)
{
	Bsp* map = app->getSelectedMap();
	if (!map || entIdx < 0)
	{
		ImGui::Text(get_localized_string(LANG_1163).c_str());
		return;
	}

	Entity* ent = map->ents[entIdx];

	ImGui::BeginChild(get_localized_string(LANG_0665).c_str());

	unsigned int spawnflags = strtoul(ent->keyvalues["spawnflags"].c_str(), NULL, 10);
	FgdClass* fgdClass = app->fgd->getFgdClass(ent->keyvalues["classname"]);

	ImGui::Columns(2, get_localized_string(LANG_0666).c_str(), true);

	static bool checkboxEnabled[32];

	for (int i = 0; i < 32; i++)
	{
		if (i == 16)
		{
			ImGui::NextColumn();
		}
		std::string name;
		std::string description;
		if (fgdClass)
		{
			name = fgdClass->spawnFlagNames[i];
			description = fgdClass->spawnFlagDescriptions[i];
		}

		checkboxEnabled[i] = spawnflags & (1 << i);

		if (ImGui::Checkbox((name + "##flag" + std::to_string(i)).c_str(), &checkboxEnabled[i]))
		{
			if (!checkboxEnabled[i])
			{
				spawnflags &= ~(1U << i);
			}
			else
			{
				spawnflags |= (1U << i);
			}
			if (spawnflags != 0)
				ent->setOrAddKeyvalue("spawnflags", std::to_string(spawnflags));
			else
				ent->removeKeyvalue("spawnflags");

			map->getBspRender()->pushEntityUndoState(checkboxEnabled[i] ? "Enable Flag" : "Disable Flag", entIdx);
		}
		if ((!name.empty() || !description.empty()) && ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::PushStyleColor(ImGuiCol_Text, { 0.9f,0.4f,0.2f,1.0f });
			ImGui::TextUnformatted(name.c_str());
			ImGui::PopStyleColor();
			if (description.size())
				ImGui::TextUnformatted(description.c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_RawEditTab(int entIdx)
{
	Bsp* map = app->getSelectedMap();
	if (!map || entIdx < 0)
	{
		ImGui::Text(get_localized_string(LANG_1176).c_str());
		return;
	}

	Entity* ent = map->ents[entIdx];
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::Columns(4, get_localized_string(LANG_1106).c_str(), false);

	float butColWidth = smallFont->CalcTextSizeA(GImGui->FontSize, 100, 100, " X ").x + style.FramePadding.x * 4;
	float textColWidth = (ImGui::GetWindowWidth() - (butColWidth + style.FramePadding.x * 2) * 2) * 0.5f;

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, textColWidth);
	ImGui::SetColumnWidth(2, textColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	ImGui::NextColumn();
	ImGui::Text(get_localized_string(LANG_0667).c_str()); ImGui::NextColumn();
	ImGui::Text(get_localized_string(LANG_0668).c_str()); ImGui::NextColumn();
	ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::BeginChild(get_localized_string(LANG_0669).c_str());

	ImGui::Columns(4, get_localized_string(LANG_0670).c_str(), false);

	textColWidth -= style.ScrollbarSize; // double space to prevent accidental deletes

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, textColWidth);
	ImGui::SetColumnWidth(2, textColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - paddingx * 2) * 0.5f;

	struct InputData
	{
		int idx;
		int entIdx;
		Entity* entRef;
		BspRenderer* bspRenderer;
	};

	struct TextChangeCallback
	{
		static int keyNameChanged(ImGuiInputTextCallbackData* data)
		{
			InputData* inputData = (InputData*)data->UserData;
			Entity* ent = inputData->entRef;

			std::string key = ent->keyOrder[inputData->idx];
			if (key != data->Buf)
			{
				ent->renameKey(inputData->idx, data->Buf);
				inputData->bspRenderer->refreshEnt(inputData->entIdx);
				if (key == "model" || std::string(data->Buf) == "model")
				{
					g_app->reloadBspModels();
					inputData->bspRenderer->preRenderEnts();
				}
				g_app->updateEntConnections();
			}

			return 1;
		}

		static int keyValueChanged(ImGuiInputTextCallbackData* data)
		{
			InputData* inputData = (InputData*)data->UserData;
			Entity* ent = inputData->entRef;
			std::string key = ent->keyOrder[inputData->idx];

			if (ent->keyvalues[key] != data->Buf)
			{
				bool needrefreshmodel = false;
				if (key == "model")
				{
					if (ent->hasKey("model") && ent->keyvalues["model"] != data->Buf)
					{
						ent->setOrAddKeyvalue(key, data->Buf);
						inputData->bspRenderer->refreshEnt(inputData->entIdx);
						pickCount++;
						vertPickCount++;
						g_app->updateEntConnections();
						g_app->reloadBspModels();
						inputData->bspRenderer->preRenderEnts();
						return 1;
					}
				}
				if (key == "renderamt")
				{
					if (ent->hasKey("renderamt") && ent->keyvalues["renderamt"] != data->Buf)
					{
						needrefreshmodel = true;
					}
				}
				if (key == "rendermode")
				{
					if (ent->hasKey("rendermode") && ent->keyvalues["rendermode"] != data->Buf)
					{
						needrefreshmodel = true;
					}
				}
				if (key == "renderfx")
				{
					if (ent->hasKey("renderfx") && ent->keyvalues["renderfx"] != data->Buf)
					{
						needrefreshmodel = true;
					}
				}
				if (key == "rendercolor")
				{
					if (ent->hasKey("rendercolor") && ent->keyvalues["rendercolor"] != data->Buf)
					{
						needrefreshmodel = true;
					}
				}

				ent->setOrAddKeyvalue(key, data->Buf);
				inputData->bspRenderer->refreshEnt(inputData->entIdx);
				pickCount++;
				vertPickCount++;
				g_app->updateEntConnections();

				if (needrefreshmodel)
				{
					Bsp* map = g_app->getSelectedMap();
					if (map && ent->getBspModelIdx() > 0)
					{
						map->getBspRender()->refreshModel(ent->getBspModelIdx());
						map->getBspRender()->preRenderEnts();
						g_app->updateEntConnections();
						return 1;
					}
				}

			}

			return 1;
		}
	};

	static InputData keyIds[MAX_KEYS_PER_ENT];
	static InputData valueIds[MAX_KEYS_PER_ENT];
	static int lastPickCount = -1;
	static std::string dragNames[MAX_KEYS_PER_ENT];
	static const char* dragIds[MAX_KEYS_PER_ENT];

	if (dragNames[0].empty())
	{
		for (int i = 0; i < MAX_KEYS_PER_ENT; i++)
		{
			std::string name = "::##drag" + std::to_string(i);
			dragNames[i] = std::move(name);
		}
	}

	if (lastPickCount != pickCount)
	{
		for (int i = 0; i < MAX_KEYS_PER_ENT; i++)
		{
			dragIds[i] = dragNames[i].c_str();
		}
	}

	ImVec4 dragColor = style.Colors[ImGuiCol_FrameBg];
	dragColor.x *= 2;
	dragColor.y *= 2;
	dragColor.z *= 2;

	ImVec4 dragButColor = style.Colors[ImGuiCol_Header];

	static bool hoveredDrag[MAX_KEYS_PER_ENT];
	static bool wasKeyDragging = false;
	bool keyDragging = false;

	float startY = 0;
	for (int i = 0; i < ent->keyOrder.size() && i < MAX_KEYS_PER_ENT; i++)
	{

		const char* item = dragIds[i];

		{
			style.SelectableTextAlign.x = 0.5f;
			ImGui::AlignTextToFramePadding();
			ImGui::PushStyleColor(ImGuiCol_Header, hoveredDrag[i] ? dragColor : dragButColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, dragColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, dragColor);
			ImGui::Selectable(item, true);
			ImGui::PopStyleColor(3);
			style.SelectableTextAlign.x = 0.0f;

			hoveredDrag[i] = ImGui::IsItemActive();
			if (hoveredDrag[i])
			{
				keyDragging = true;
			}


			if (i == 0)
			{
				startY = ImGui::GetItemRectMin().y;
			}

			if (ImGui::IsItemActive() && !ImGui::IsItemHovered())
			{
				int n_next = (int)((ImGui::GetMousePos().y - startY) / (ImGui::GetItemRectSize().y + style.FramePadding.y * 2));
				if (n_next >= 0 && n_next < ent->keyOrder.size() && n_next < MAX_KEYS_PER_ENT)
				{
					dragIds[i] = dragIds[n_next];
					dragIds[n_next] = item;

					std::string temp = ent->keyOrder[i];
					ent->keyOrder[i] = ent->keyOrder[n_next];
					ent->keyOrder[n_next] = temp;

					ImGui::ResetMouseDragDelta();
				}
			}

			ImGui::NextColumn();
		}

		{
			bool invalidKey = lastPickCount == pickCount;

			keyIds[i].idx = i;
			keyIds[i].entIdx = app->pickInfo.GetSelectedEnt();
			keyIds[i].entRef = ent;
			keyIds[i].bspRenderer = map->getBspRender();

			if (invalidKey)
			{
				ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			}
			else if (hoveredDrag[i])
			{
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}

			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##key" + std::to_string(i)).c_str(), &ent->keyOrder[i], ImGuiInputTextFlags_CallbackEdit,
				TextChangeCallback::keyNameChanged, &keyIds[i]);


			if (invalidKey || hoveredDrag[i])
			{
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}
		{
			valueIds[i].idx = i;
			valueIds[i].entIdx = app->pickInfo.GetSelectedEnt();
			valueIds[i].entRef = ent;
			valueIds[i].bspRenderer = map->getBspRender();

			if (hoveredDrag[i])
			{
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}
			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##val" + std::to_string(i)).c_str(), &ent->keyvalues[ent->keyOrder[i]], ImGuiInputTextFlags_CallbackEdit,
				TextChangeCallback::keyValueChanged, &valueIds[i]);

			if (ent->keyOrder[i] == "angles" ||
				ent->keyOrder[i] == "angle")
			{
				if (IsEntNotSupportAngles(ent->keyvalues["classname"]))
				{
					ImGui::SetNextItemWidth(inputWidth);
					ImGui::TextUnformatted(get_localized_string(LANG_0671).c_str());
				}
				else if (ent->keyvalues["classname"] == "env_sprite")
				{
					ImGui::SetNextItemWidth(inputWidth);
					ImGui::TextUnformatted(get_localized_string(LANG_0672).c_str());
				}
				else if (ent->keyvalues["classname"] == "func_breakable")
				{
					ImGui::SetNextItemWidth(inputWidth);
					ImGui::TextUnformatted(get_localized_string(LANG_0673).c_str());
				}
			}

			if (hoveredDrag[i])
			{
				ImGui::PopStyleColor();
			}

			ImGui::NextColumn();
		}
		{
			std::string keyOrdname = ent->keyOrder[i];
			ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
			if (ImGui::Button((" X ##delorder" + keyOrdname).c_str()))
			{
				ent->removeKeyvalue(keyOrdname);
				map->getBspRender()->refreshEnt(entIdx);
				if (keyOrdname == "model")
					map->getBspRender()->preRenderEnts();
				app->updateEntConnections();
				map->getBspRender()->pushEntityUndoState("Delete Keyvalue", entIdx);
			}
			ImGui::PopStyleColor(3);
			ImGui::NextColumn();
		}
	}

	if (!keyDragging && wasKeyDragging)
	{
		map->getBspRender()->refreshEnt(entIdx);
		map->getBspRender()->pushEntityUndoState("Move Keyvalue", entIdx);
	}

	wasKeyDragging = keyDragging;

	lastPickCount = pickCount;

	ImGui::Columns(1);

	ImGui::Dummy(ImVec2(0, style.FramePadding.y));
	ImGui::Dummy(ImVec2(butColWidth, 0)); ImGui::SameLine();

	static std::string keyName = "NewKey";


	if (ImGui::Button(get_localized_string(LANG_0674).c_str()))
	{
		if (!ent->hasKey(keyName))
		{
			ent->addKeyvalue(keyName, "");
			map->getBspRender()->refreshEnt(entIdx);
			app->updateEntConnections();
			map->getBspRender()->pushEntityUndoState("Add Keyvalue", entIdx);
			keyName = "";
		}
	}
	ImGui::SameLine();

	ImGui::InputText(get_localized_string(LANG_0675).c_str(), &keyName);

	ImGui::EndChild();
}


void Gui::drawMDLWidget()
{
	Bsp* map = app->getSelectedMap();
	ImGui::SetNextWindowSize(ImVec2(410.f, 200.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(410.f, 330.f), ImVec2(410.f, 330.f));
	bool showMdlWidget = map && map->is_mdl_model;
	if (ImGui::Begin(get_localized_string("LANG_MDL_WIDGET").c_str(), &showMdlWidget))
	{

	}
	ImGui::End();
}

void Gui::drawGOTOWidget()
{
	ImGui::SetNextWindowSize(ImVec2(410.f, 200.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(410.f, 330.f), ImVec2(410.f, 330.f));
	static vec3 coordinates = vec3();
	static vec3 angles = vec3();
	float angles_y = 0.0f;
	static int modelid = -1, faceid = -1, entid = -1;

	if (ImGui::Begin(get_localized_string(LANG_0676).c_str(), &showGOTOWidget, 0))
	{
		if (entid == -1)
			entid = g_app->pickInfo.GetSelectedEnt();
		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
		if (showGOTOWidget_update)
		{
			entid = g_app->pickInfo.GetSelectedEnt();
			coordinates = cameraOrigin;
			angles = cameraAngles;
			angles.normalize_angles();
			angles.z -= 90.0f;
			angles.y = angles.z;
			angles.y = 0.0f;
			angles.unflip();
			showGOTOWidget_update = false;
		}
		ImGui::Text(get_localized_string(LANG_0677).c_str());
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat(get_localized_string(LANG_0678).c_str(), &coordinates.x, 0.1f, 0, 0, "Y: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0679).c_str(), &coordinates.y, 0.1f, 0, 0, "X: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0680).c_str(), &coordinates.z, 0.1f, 0, 0, "Z: %.0f");
		ImGui::PopItemWidth();
		ImGui::Text(get_localized_string(LANG_0681).c_str());
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat(get_localized_string(LANG_0683).c_str(), &angles.x, 0.1f, 0, 0, "PITCH: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0682).c_str(), &angles.z, 0.1f, 0, 0, "YAW: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0684).c_str(), &angles_y, 0.1f, 0, 0, "ROLL: %.0f");
		ImGui::PopItemWidth();
		if (ImGui::IsItemHovered()) {
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Not supported camera rolling");
			ImGui::EndTooltip();
		}

		Bsp* map = app->getSelectedMap();
		ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
		if (ImGui::Button("Go to"))
		{
			cameraOrigin = coordinates;
			map->getBspRender()->renderCameraOrigin = cameraOrigin;
			


			cameraAngles = angles.flip();
			cameraAngles.z = cameraAngles.y + 90.0f;
			cameraAngles = cameraAngles.normalize_angles();
			cameraAngles.y = 0.0f;
			map->getBspRender()->renderCameraAngles = cameraAngles;
			
			makeVectors(cameraAngles, app->cameraForward, app->cameraRight, app->cameraUp);
		}
		ImGui::PopStyleColor(3);
		if (map && !map->is_mdl_model)
		{
			ImGui::Separator();
			ImGui::PushItemWidth(inputWidth);
			ImGui::DragInt(get_localized_string(LANG_0685).c_str(), &modelid);
			ImGui::DragInt(get_localized_string(LANG_0686).c_str(), &faceid);
			ImGui::DragInt(get_localized_string(LANG_0687).c_str(), &entid);
			ImGui::PopItemWidth();
			if (ImGui::Button("Go to##2"))
			{
				if (modelid >= 0 && modelid < map->modelCount)
				{
					for (size_t i = 0; i < map->ents.size(); i++)
					{
						if (map->ents[i]->getBspModelIdx() == modelid)
						{
							app->selectEnt(map, entid);
							app->goToEnt(map, entid);
							break;
						}
					}
				}
				else if (faceid > 0 && faceid < map->faceCount)
				{
					app->goToFace(map, faceid);
					int modelIdx = map->get_model_from_face(faceid);
					if (modelIdx >= 0)
					{
						for (size_t i = 0; i < map->ents.size(); i++)
						{
							if (map->ents[i]->getBspModelIdx() == modelid)
							{
								app->pickInfo.SetSelectedEnt((int)i);
								break;
							}
						}
					}
					app->selectFace(map, faceid);
				}
				else if (entid > 0 && entid < map->ents.size())
				{
					app->selectEnt(map, entid);
					app->goToEnt(map, entid);
				}

				if (modelid != -1 && entid != -1 ||
					modelid != -1 && faceid != -1 ||
					entid != -1 && faceid != -1)
				{
					modelid = entid = faceid = -1;
				}
			}
		}
	}

	ImGui::End();
}
void Gui::drawTransformWidget()
{
	bool transformingEnt = false;
	Entity* ent = NULL;
	int entIdx = app->pickInfo.GetSelectedEnt();
	Bsp* map = app->getSelectedMap();

	if (map && entIdx >= 0)
	{
		ent = map->ents[entIdx];
		transformingEnt = entIdx > 0;
	}

	ImGui::SetNextWindowSize(ImVec2(440.f, 380.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(430, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));


	static float x, y, z;
	static float fx, fy, fz;
	static float last_fx, last_fy, last_fz;
	static float sx, sy, sz;

	static bool shouldUpdateUi = true;

	static int lastPickCount = -1;
	static int lastVertPickCount = -1;
	static bool oldSnappingEnabled = app->gridSnappingEnabled;

	if (ImGui::Begin(get_localized_string(LANG_0688).c_str(), &showTransformWidget, 0))
	{
		if (!ent)
		{
			ImGui::Text(get_localized_string(LANG_1180).c_str());
		}
		else
		{
			ImGuiStyle& style = ImGui::GetStyle();

			if (!shouldUpdateUi)
			{
				shouldUpdateUi = lastPickCount != pickCount ||
					app->draggingAxis != -1 ||
					app->movingEnt ||
					oldSnappingEnabled != app->gridSnappingEnabled ||
					lastVertPickCount != vertPickCount;
			}

			if (shouldUpdateUi)
			{
				shouldUpdateUi = true;
			}

			TransformAxes& activeAxes = *(app->transformMode == TRANSFORM_MODE_SCALE ? &app->scaleAxes : &app->moveAxes);

			if (shouldUpdateUi)
			{
				if (transformingEnt)
				{
					if (app->transformTarget == TRANSFORM_VERTEX)
					{
						x = fx = last_fx = activeAxes.origin.x;
						y = fy = last_fy = activeAxes.origin.y;
						z = fz = last_fz = activeAxes.origin.z;
					}
					else
					{
						vec3 ori = ent->getOrigin();
						if (app->originSelected)
						{
							ori = app->transformedOrigin;
						}
						x = fx = ori.x;
						y = fy = ori.y;
						z = fz = ori.z;
					}

				}
				else
				{
					x = fx = 0.f;
					y = fy = 0.f;
					z = fz = 0.f;
				}
				sx = sy = sz = 1;
				shouldUpdateUi = false;
			}

			oldSnappingEnabled = app->gridSnappingEnabled;
			lastVertPickCount = vertPickCount;
			lastPickCount = pickCount;

			bool scaled = false;
			bool originChanged = false;
			guiHoverAxis = -1;

			float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
			float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
			float inputWidth4 = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.25f;

			static bool inputsWereDragged = false;
			bool inputsAreDragging = false;

			ImGui::Text(get_localized_string(LANG_0689).c_str());
			ImGui::PushItemWidth(inputWidth);

			if (ImGui::DragFloat(get_localized_string(LANG_1107).c_str(), &x, 0.01f, -FLT_MAX_COORD, FLT_MAX_COORD, app->gridSnappingEnabled ? "Y: %.2f" : "Y: %.0f"))
			{
				originChanged = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat(get_localized_string(LANG_1108).c_str(), &y, 0.01f, -FLT_MAX_COORD, FLT_MAX_COORD, app->gridSnappingEnabled ? "X: %.2f" : "X: %.0f"))
			{
				originChanged = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat(get_localized_string(LANG_1109).c_str(), &z, 0.01f, -FLT_MAX_COORD, FLT_MAX_COORD, app->gridSnappingEnabled ? "Z: %.2f" : "Z: %.0f"))
			{
				originChanged = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;


			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y));

			ImGui::Text(get_localized_string(LANG_0690).c_str());
			ImGui::PushItemWidth(inputWidth);

			if (ImGui::DragFloat(get_localized_string(LANG_0691).c_str(), &sx, 0.002f, 0, 0, "Y: %.3f"))
			{
				scaled = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat(get_localized_string(LANG_0692).c_str(), &sy, 0.002f, 0, 0, "X: %.3f"))
			{
				scaled = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat(get_localized_string(LANG_0693).c_str(), &sz, 0.002f, 0, 0, "Z: %.3f"))
			{
				scaled = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;

			if (inputsWereDragged && !inputsAreDragging)
			{
				if (map->getBspRender()->undoEntityState[entIdx].getOrigin() != ent->getOrigin())
				{
					map->getBspRender()->pushEntityUndoState("Move Entity", entIdx);
				}

				if (transformingEnt)
				{
					app->applyTransform(map, true);

					if (app->gridSnappingEnabled)
					{
						fx = last_fx = x;
						fy = last_fy = y;
						fz = last_fz = z;
					}
					else
					{
						x = last_fx = fx;
						y = last_fy = fy;
						z = last_fz = fz;
					}
				}
			}

			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 3));
			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y));
			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));


			ImGui::Columns(4, 0, false);
			ImGui::SetColumnWidth(0, inputWidth4);
			ImGui::SetColumnWidth(1, inputWidth4);
			ImGui::SetColumnWidth(2, inputWidth4);
			ImGui::SetColumnWidth(3, inputWidth4);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(get_localized_string(LANG_0694).c_str()); ImGui::NextColumn();

			if (app->transformMode == TRANSFORM_MODE_NONE)
			{
				ImGui::BeginDisabled();
			}

			if (ImGui::RadioButton(get_localized_string(LANG_0695).c_str(), &app->transformTarget, TRANSFORM_OBJECT))
			{
				pickCount++;
				vertPickCount++;
			}

			ImGui::NextColumn();
			if (app->transformMode == TRANSFORM_MODE_SCALE)
			{
				ImGui::BeginDisabled();
			}

			if (ImGui::RadioButton(get_localized_string(LANG_0696).c_str(), &app->transformTarget, TRANSFORM_VERTEX))
			{
				pickCount++;
				vertPickCount++;
			}

			ImGui::NextColumn();

			if (ImGui::RadioButton(get_localized_string(LANG_0697).c_str(), &app->transformTarget, TRANSFORM_ORIGIN))
			{
				pickCount++;
				vertPickCount++;
			}

			ImGui::NextColumn();
			if (app->transformMode == TRANSFORM_MODE_SCALE)
			{
				ImGui::EndDisabled();
			}
			if (app->transformMode == TRANSFORM_MODE_NONE)
			{
				ImGui::EndDisabled();
			}
			ImGui::Text(get_localized_string(LANG_0698).c_str()); ImGui::NextColumn();
			ImGui::RadioButton(get_localized_string(LANG_1110).c_str(), &app->transformMode, TRANSFORM_MODE_NONE); ImGui::NextColumn();
			ImGui::RadioButton(get_localized_string(LANG_1111).c_str(), &app->transformMode, TRANSFORM_MODE_MOVE); ImGui::NextColumn();
			ImGui::RadioButton(get_localized_string(LANG_1112).c_str(), &app->transformMode, TRANSFORM_MODE_SCALE); ImGui::NextColumn();

			if (app->transformMode == TRANSFORM_MODE_SCALE)
			{
				app->transformTarget = TRANSFORM_OBJECT;
			}

			ImGui::Columns(1);

			const int grid_snap_modes = 11;
			const char* element_names[grid_snap_modes] = { "0", "1", "2", "4", "8", "16", "32", "64", "128", "256", "512" };
			static int current_element = app->gridSnapLevel + 1;

			ImGui::Columns(2, 0, false);
			ImGui::SetColumnWidth(0, inputWidth4);
			ImGui::SetColumnWidth(1, inputWidth4 * 3);
			ImGui::Text(get_localized_string(LANG_0699).c_str()); ImGui::NextColumn();
			ImGui::SetNextItemWidth(inputWidth4 * 3);
			if (ImGui::SliderInt(get_localized_string(LANG_0700).c_str(), &current_element, 0, grid_snap_modes - 1, element_names[current_element]))
			{
				app->gridSnapLevel = current_element - 1;
				app->gridSnappingEnabled = current_element != 0;
				originChanged = true;
			}
			ImGui::Columns(1);

			ImGui::PushItemWidth(inputWidth);
			ImGui::Checkbox(get_localized_string(LANG_0701).c_str(), &app->textureLock);
			ImGui::SameLine();
			ImGui::Text(get_localized_string(LANG_1113).c_str());
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0702).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			ImGui::SameLine();
			if (app->transformMode != TRANSFORM_MODE_MOVE || app->transformTarget != TRANSFORM_OBJECT)
				ImGui::BeginDisabled();
			ImGui::Checkbox(get_localized_string(LANG_0703).c_str(), &app->moveOrigin);
			if (app->transformMode != TRANSFORM_MODE_MOVE || app->transformTarget != TRANSFORM_OBJECT)
				ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::Text(get_localized_string(LANG_0704).c_str());
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0705).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
			ImGui::Text(("Size: " + app->selectionSize.toKeyvalueString(false, "w ", "l ", "h")).c_str());

			if (transformingEnt)
			{
				if (originChanged)
				{
					if (app->transformTarget == TRANSFORM_VERTEX)
					{
						vec3 delta;
						if (app->gridSnappingEnabled)
						{
							delta = vec3(x - last_fx, y - last_fy, z - last_fz);
						}
						else
						{
							delta = vec3(fx - last_fx, fy - last_fy, fz - last_fz);
						}

						app->moveSelectedVerts(delta);
					}
					else if (app->transformTarget == TRANSFORM_OBJECT)
					{
						vec3 newOrigin = app->gridSnappingEnabled ? vec3(x, y, z) : vec3(fx, fy, fz);
						newOrigin = app->gridSnappingEnabled ? app->snapToGrid(newOrigin) : newOrigin;

						if (app->gridSnappingEnabled)
						{
							fx = x;
							fy = y;
							fz = z;
						}
						else
						{
							x = fx;
							y = fy;
							z = fz;
						}

						ent->setOrAddKeyvalue("origin", newOrigin.toKeyvalueString());
						map->getBspRender()->refreshEnt(entIdx);
						app->updateEntConnectionPositions();
					}
					else if (app->transformTarget == TRANSFORM_ORIGIN)
					{
						vec3 newOrigin = app->gridSnappingEnabled ? vec3(x, y, z) : vec3(fx, fy, fz);
						newOrigin = app->gridSnappingEnabled ? app->snapToGrid(newOrigin) : newOrigin;

						app->transformedOrigin = newOrigin;
					}
				}
				if (scaled && ent->isBspModel() && app->isTransformableSolid && !app->modelUsesSharedStructures)
				{
					if (app->transformTarget == TRANSFORM_VERTEX)
					{
						app->scaleSelectedVerts(sx, sy, sz);
					}
					else if (app->transformTarget == TRANSFORM_OBJECT)
					{
						int modelIdx = ent->getBspModelIdx();
						app->scaleSelectedObject(sx, sy, sz);
						map->getBspRender()->refreshModel(modelIdx);
					}
					else if (app->transformTarget == TRANSFORM_ORIGIN)
					{
						print_log(get_localized_string(LANG_0397));
					}
				}
			}

			inputsWereDragged = inputsAreDragging;
		}
	}
	ImGui::End();
}

void Gui::loadFonts()
{
	// data copied to new array so that ImGui doesn't delete static data
	unsigned char* smallFontData = new unsigned char[sizeof(robotomedium)];
	unsigned char* largeFontData = new unsigned char[sizeof(robotomedium)];
	unsigned char* consoleFontData = new unsigned char[sizeof(robotomono)];
	unsigned char* consoleFontLargeData = new unsigned char[sizeof(robotomono)];
	memcpy(smallFontData, robotomedium, sizeof(robotomedium));
	memcpy(largeFontData, robotomedium, sizeof(robotomedium));
	memcpy(consoleFontData, robotomono, sizeof(robotomono));
	memcpy(consoleFontLargeData, robotomono, sizeof(robotomono));

	ImFontConfig config;

	config.SizePixels = fontSize * 2.0f;
	config.OversampleH = 3;
	config.OversampleV = 1;
	config.RasterizerMultiply = 1.5f;
	config.PixelSnapH = true;

	defaultFont = imgui_io->Fonts->AddFontFromMemoryCompressedTTF((const char*)compressed_data, compressed_size, fontSize, &config);
	config.MergeMode = true;
	imgui_io->Fonts->AddFontFromMemoryCompressedTTF((const char*)compressed_data, compressed_size, fontSize, &config, imgui_io->Fonts->GetGlyphRangesDefault());
	config.MergeMode = true;
	imgui_io->Fonts->AddFontFromMemoryCompressedTTF((const char*)compressed_data, compressed_size, fontSize, &config, imgui_io->Fonts->GetGlyphRangesCyrillic());
	imgui_io->Fonts->Build();

	smallFont = imgui_io->Fonts->AddFontFromMemoryTTF((void*)smallFontData, sizeof(robotomedium), fontSize, &config);
	largeFont = imgui_io->Fonts->AddFontFromMemoryTTF((void*)largeFontData, sizeof(robotomedium), fontSize * 1.1f, &config);
	consoleFont = imgui_io->Fonts->AddFontFromMemoryTTF((void*)consoleFontData, sizeof(robotomono), fontSize, &config);
	consoleFontLarge = imgui_io->Fonts->AddFontFromMemoryTTF((void*)consoleFontLargeData, sizeof(robotomono), fontSize * 1.1f, &config);
}

void Gui::drawLog()
{
	static bool AutoScroll = true;  // Keep scrolling if already at the bottom

	ImGui::SetNextWindowSize(ImVec2(750.f, 300.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));
	if (!ImGui::Begin(get_localized_string(LANG_1164).c_str(), &showLogWidget))
	{
		ImGui::End();
		return;
	}

	static std::vector<std::string> log_buffer_copy;
	static std::vector<unsigned short> color_buffer_copy;
	static int real_string_count = 0;

	g_mutex_list[0].lock();
	if (log_buffer_copy.size() != g_log_buffer.size())
	{
		log_buffer_copy = g_log_buffer;
		color_buffer_copy = g_color_buffer;
		real_string_count = 0;
		for (size_t line_no = 0; line_no < log_buffer_copy.size(); line_no++)
		{
			real_string_count++;
			if (line_no + 1 < log_buffer_copy.size() && log_buffer_copy[line_no].size() && log_buffer_copy[line_no][log_buffer_copy[line_no].size() - 1] != '\n')
			{
				real_string_count--;
			}
		}
	}
	g_mutex_list[0].unlock();

	ImGui::BeginChild(get_localized_string(LANG_0706).c_str(), ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

	bool copy = false;
	bool toggledAutoScroll = false;
	if (ImGui::BeginPopupContextWindow())
	{
		if (ImGui::MenuItem(get_localized_string(LANG_1165).c_str()))
		{
			copy = true;
		}
		if (ImGui::MenuItem(get_localized_string(LANG_0707).c_str()))
		{
			g_mutex_list[0].lock();
			g_log_buffer.clear();
			g_color_buffer.clear();
			g_mutex_list[0].unlock();
		}
		if (ImGui::MenuItem(get_localized_string(LANG_0708).c_str(), NULL, &AutoScroll))
		{
			toggledAutoScroll = true;
		}
		ImGui::EndPopup();
	}

	ImGui::PushFont(consoleFont);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));


	if (copy)
	{
		std::string logStr;
		for (const auto& str : log_buffer_copy) {
			logStr += str;
		}

		ImGui::SetClipboardText(logStr.c_str());
	}

	ImGuiListClipper clipper;
	clipper.Begin(real_string_count, ImGui::GetTextLineHeight());
	while (clipper.Step())
	{
		for (int line_no = clipper.DisplayStart; line_no < log_buffer_copy.size(); line_no++)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, imguiColorFromConsole(color_buffer_copy[line_no]));
			clipper.ItemsHeight = ImGui::GetTextLineHeight();
			ImGui::TextUnformatted(log_buffer_copy[line_no].c_str());
			if (line_no + 1 < log_buffer_copy.size() && log_buffer_copy[line_no].size() && log_buffer_copy[line_no][log_buffer_copy[line_no].size() - 1] != '\n')
			{
				clipper.ItemsHeight = 0.0f;
				ImGui::SameLine();
			}
			ImGui::PopStyleColor();
		}
	}
	clipper.End();

	ImGui::PopFont();
	ImGui::PopStyleVar();

	if (AutoScroll && (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() || toggledAutoScroll))
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
	ImGui::End();
}

void Gui::drawSettings()
{
	ImGui::SetNextWindowSize(ImVec2(790.f, 340.f), ImGuiCond_FirstUseEver);

	bool oldShowSettings = showSettingsWidget;
	bool apply_settings_pressed = false;
	static std::string langForSelect = g_settings.selected_lang;

	if (ImGui::Begin(get_localized_string(LANG_1114).c_str(), &showSettingsWidget))
	{
		ImGuiContext& g = *GImGui;
		const int settings_tabs = 7;

		static int resSelected = 0;
		static int fgdSelected = 0;


		static const char* tab_titles[settings_tabs] = {
			"General",
			"FGDs",
			"Asset Paths",
			"Optimizing",
			"Limits",
			"Rendering",
			"Controls"
		};

		// left
		ImGui::BeginChild(get_localized_string(LANG_0709).c_str(), ImVec2(150, 0), true);

		for (int i = 0; i < settings_tabs; i++)
		{
			if (ImGui::Selectable(tab_titles[i], settingsTab == i))
				settingsTab = i;
		}

		ImGui::Separator();


		ImGui::Dummy(ImVec2(0, 60));
		if (ImGui::Button(get_localized_string(LANG_0710).c_str()))
		{
			apply_settings_pressed = true;
		}

		ImGui::EndChild();


		ImGui::SameLine();

		// right

		ImGui::BeginGroup();
		float footerHeight = settingsTab <= 2 ? ImGui::GetFrameHeightWithSpacing() + 4.f : 0.f;
		ImGui::BeginChild(get_localized_string(LANG_0711).c_str(), ImVec2(0, -footerHeight)); // Leave room for 1 line below us
		ImGui::Text(tab_titles[settingsTab]);
		ImGui::Separator();

		if (reloadSettings)
		{
			reloadSettings = false;
		}

		float pathWidth = ImGui::GetWindowWidth() - 60.f;
		float delWidth = 50.f;

		if (ifd::FileDialog::Instance().IsDone("GameDir"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.gamedir = res.parent_path().string();
				g_settings.lastdir = res.parent_path().string();
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("WorkingDir"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.workingdir = res.parent_path().string();
				g_settings.lastdir = res.parent_path().string();
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("fgdOpen"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.fgdPaths[fgdSelected].path = res.string();
				g_settings.fgdPaths[fgdSelected].enabled = true;
				g_settings.lastdir = res.parent_path().string();
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("resOpen"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.resPaths[resSelected].path = res.string();
				g_settings.resPaths[resSelected].enabled = true;
				g_settings.lastdir = res.parent_path().string();
			}
			ifd::FileDialog::Instance().Close();
		}

		ImGui::BeginChild(get_localized_string(LANG_0712).c_str());
		if (settingsTab == 0)
		{
			ImGui::Text(get_localized_string(LANG_0713).c_str());
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText(get_localized_string(LANG_0714).c_str(), &g_settings.gamedir);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			if (ImGui::Button(get_localized_string(LANG_0715).c_str()))
			{
				ifd::FileDialog::Instance().Open("GameDir", "Select game dir", std::string(), false, g_settings.lastdir);
			}
			ImGui::Text(get_localized_string(LANG_0716).c_str());
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText(get_localized_string(LANG_0717).c_str(), &g_settings.workingdir);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			if (ImGui::Button(get_localized_string(LANG_0718).c_str()))
			{
				ifd::FileDialog::Instance().Open("WorkingDir", "Select working dir", std::string(), false, g_settings.lastdir);
			}
			if (ImGui::DragFloat(get_localized_string(LANG_0719).c_str(), &fontSize, 0.1f, 8, 48, get_localized_string(LANG_0720).c_str()))
			{
				shouldReloadFonts = true;
			}
			ImGui::DragInt(get_localized_string(LANG_0721).c_str(), &g_settings.undoLevels, 0.05f, 0, 64);
#ifndef NDEBUG
			ImGui::BeginDisabled();
#endif
			ImGui::Checkbox(get_localized_string(LANG_0722).c_str(), &g_verbose);
#ifndef NDEBUG
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0723).c_str());
				ImGui::EndTooltip();
			}
#endif
			ImGui::SameLine();

			ImGui::Checkbox(get_localized_string(LANG_0724).c_str(), &g_settings.backUpMap);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0725).c_str());
				ImGui::EndTooltip();
			}

			ImGui::Checkbox(get_localized_string(LANG_0726).c_str(), &g_settings.preserveCrc32);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0727).c_str());
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			ImGui::Checkbox(get_localized_string(LANG_0728).c_str(), &g_settings.autoImportEnt);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0729).c_str());
				ImGui::EndTooltip();
			}

			ImGui::Checkbox(get_localized_string(LANG_0730).c_str(), &g_settings.sameDirForEnt);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0731).c_str());
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			if (ImGui::Checkbox(get_localized_string(LANG_0732).c_str(), &g_settings.save_windows))
			{
				imgui_io->IniFilename = !g_settings.save_windows ? NULL : iniPath.c_str();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0733).c_str());
				ImGui::EndTooltip();
			}

			ImGui::Checkbox(get_localized_string(LANG_0734).c_str(), &g_settings.defaultIsEmpty);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0735).c_str());
				ImGui::TextUnformatted(get_localized_string(LANG_0736).c_str());
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			ImGui::Checkbox(get_localized_string(LANG_0737).c_str(), &g_settings.start_at_entity);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0738).c_str());
				ImGui::EndTooltip();
			}
			ImGui::Separator();
			if (ImGui::BeginCombo("##lang", langForSelect.c_str()))
			{
				for (const auto& s : g_settings.languages)
				{
					if (ImGui::Selectable(s.c_str(), s == langForSelect))
					{
						langForSelect = s;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::Separator();
			if (ImGui::Button(get_localized_string(LANG_0739).c_str()))
			{
				g_settings.reset();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0740).c_str());
				ImGui::EndTooltip();
			}
		}
		else if (settingsTab == 1)
		{
			for (int i = 0; i < g_settings.fgdPaths.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth * 0.20f);
				ImGui::Checkbox((std::string("##enablefgd") + std::to_string(i)).c_str(), &g_settings.fgdPaths[i].enabled);

				ImGui::SameLine();

				ImGui::SetNextItemWidth(pathWidth * 0.80f);
				ImGui::InputText(("##fgd" + std::to_string(i)).c_str(), &g_settings.fgdPaths[i].path);

				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				if (ImGui::Button(("...##fgdOpen" + std::to_string(i)).c_str()))
				{
					fgdSelected = i;
					ifd::FileDialog::Instance().Open("fgdOpen", "Select fgd path", "fgd file (*.fgd){.fgd},.*", false, g_settings.lastdir);
				}

				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##del_fgd" + std::to_string(i)).c_str()))
				{
					g_settings.fgdPaths.erase(g_settings.fgdPaths.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0741).c_str()))
			{
				g_settings.fgdPaths.push_back({ std::string(),true });
			}
		}
		else if (settingsTab == 2)
		{
			for (int i = 0; i < g_settings.resPaths.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth * 0.20f);
				ImGui::Checkbox((std::string("##enableres") + std::to_string(i)).c_str(), &g_settings.resPaths[i].enabled);

				ImGui::SameLine();

				ImGui::SetNextItemWidth(pathWidth * 0.80f);
				ImGui::InputText(("##res" + std::to_string(i)).c_str(), &g_settings.resPaths[i].path);

				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				if (ImGui::Button(("...##resOpen" + std::to_string(i)).c_str()))
				{
					resSelected = i;
					ifd::FileDialog::Instance().Open("resOpen", "Select fgd path", std::string(), false, g_settings.lastdir);
				}

				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##del_res" + std::to_string(i)).c_str()))
				{
					g_settings.resPaths.erase(g_settings.resPaths.begin() + i);
				}
				ImGui::PopStyleColor(3);

			}

			if (ImGui::Button(get_localized_string(LANG_0742).c_str()))
			{
				g_settings.resPaths.push_back({ std::string(), true });
			}
		}
		else if (settingsTab == 3)
		{
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox(get_localized_string(LANG_0743).c_str(), &g_settings.stripWad);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0744).c_str());
				ImGui::EndTooltip();
			}
			ImGui::SameLine();

			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox(get_localized_string(LANG_0745).c_str(), &g_settings.mark_unused_texinfos);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0746).c_str());
				ImGui::EndTooltip();
			}
			ImGui::Separator();

			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox(get_localized_string(LANG_0747).c_str(), &g_settings.merge_verts);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0748).c_str());
				ImGui::EndTooltip();
			}
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::Text(get_localized_string(LANG_0749).c_str());

			for (int i = 0; i < g_settings.conditionalPointEntTriggers.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##pointent" + std::to_string(i)).c_str(), &g_settings.conditionalPointEntTriggers[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##pointent" + std::to_string(i)).c_str()))
				{
					g_settings.conditionalPointEntTriggers.erase(g_settings.conditionalPointEntTriggers.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0750).c_str()))
			{
				g_settings.conditionalPointEntTriggers.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0751).c_str());

			for (int i = 0; i < g_settings.entsThatNeverNeedAnyHulls.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entnohull" + std::to_string(i)).c_str(), &g_settings.entsThatNeverNeedAnyHulls[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##entnohull" + std::to_string(i)).c_str()))
				{
					g_settings.entsThatNeverNeedAnyHulls.erase(g_settings.entsThatNeverNeedAnyHulls.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0752).c_str()))
			{
				g_settings.entsThatNeverNeedAnyHulls.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0753).c_str());

			for (int i = 0; i < g_settings.entsThatNeverNeedCollision.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entnocoll" + std::to_string(i)).c_str(), &g_settings.entsThatNeverNeedCollision[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##entnocoll" + std::to_string(i)).c_str()))
				{
					g_settings.entsThatNeverNeedCollision.erase(g_settings.entsThatNeverNeedCollision.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0754).c_str()))
			{
				g_settings.entsThatNeverNeedCollision.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0755).c_str());

			for (int i = 0; i < g_settings.passableEnts.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entpass" + std::to_string(i)).c_str(), &g_settings.passableEnts[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##entpass" + std::to_string(i)).c_str()))
				{
					g_settings.passableEnts.erase(g_settings.passableEnts.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0756).c_str()))
			{
				g_settings.passableEnts.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0757).c_str());

			for (int i = 0; i < g_settings.playerOnlyTriggers.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entpltrigg" + std::to_string(i)).c_str(), &g_settings.playerOnlyTriggers[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##entpltrigg" + std::to_string(i)).c_str()))
				{
					g_settings.playerOnlyTriggers.erase(g_settings.playerOnlyTriggers.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0758).c_str()))
			{
				g_settings.playerOnlyTriggers.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0759).c_str());

			for (int i = 0; i < g_settings.monsterOnlyTriggers.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##entmonsterrigg" + std::to_string(i)).c_str(), &g_settings.monsterOnlyTriggers[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##entmonsterrigg" + std::to_string(i)).c_str()))
				{
					g_settings.monsterOnlyTriggers.erase(g_settings.monsterOnlyTriggers.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0760).c_str()))
			{
				g_settings.monsterOnlyTriggers.emplace_back(std::string());
			}
		}
		else if (settingsTab == 4)
		{
			ImGui::SetNextItemWidth(pathWidth / 2);
			static unsigned int vis_data_count = MAX_MAP_VISDATA / (1024 * 1024);
			static unsigned int light_data_count = MAX_MAP_LIGHTDATA / (1024 * 1024);

			ImGui::DragFloat(get_localized_string(LANG_0761).c_str(), &FLT_MAX_COORD, 64.f, 512.f, 2147483647.f, "%.0f");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0762).c_str(), (int*)&MAX_MAP_MODELS, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0763).c_str(), (int*)&MAX_MAP_ENTS, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0764).c_str(), (int*)&MAX_MAP_TEXTURES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0765).c_str(), (int*)&MAX_MAP_NODES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0766).c_str(), (int*)&MAX_MAP_CLIPNODES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0767).c_str(), (int*)&MAX_MAP_LEAVES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			if (ImGui::DragInt(get_localized_string(LANG_0768).c_str(), (int*)&vis_data_count, 4, 128, 2147483647, get_localized_string(LANG_0769).c_str()))
			{
				MAX_MAP_VISDATA = vis_data_count * (1024 * 1024);
			}
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0770).c_str(), (int*)&MAX_MAP_EDGES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0771).c_str(), (int*)&MAX_MAP_SURFEDGES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			if (ImGui::DragInt(get_localized_string(LANG_0772).c_str(), (int*)&light_data_count, 4, 128, 2147483647, get_localized_string(LANG_0769).c_str()))
			{
				MAX_MAP_LIGHTDATA = light_data_count * (1024 * 1024);
			}
			ImGui::SetNextItemWidth(pathWidth / 2);
			if (ImGui::DragInt(get_localized_string(LANG_0773).c_str(), (int*)&MAX_TEXTURE_DIMENSION, 4, 32, 1048576, "%u"))
			{
				MAX_TEXTURE_SIZE = ((MAX_TEXTURE_DIMENSION * MAX_TEXTURE_DIMENSION * 2 * 3) / 2);
			}
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt(get_localized_string(LANG_0774).c_str(), (int*)&TEXTURE_STEP, 4, 4, 512, "%u");
		}
		else if (settingsTab == 5)
		{
			ImGui::Text(get_localized_string(LANG_0775).c_str());
			ImGui::Checkbox(get_localized_string(LANG_1115).c_str(), &g_settings.vsync);
			ImGui::DragFloat(get_localized_string(LANG_0776).c_str(), &app->fov, 0.1f, 1.0f, 150.0f, get_localized_string(LANG_0777).c_str());
			ImGui::DragFloat(get_localized_string(LANG_0778).c_str(), &app->zFar, 10.0f, -FLT_MAX_COORD, FLT_MAX_COORD, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::Separator();

			bool renderTextures = g_render_flags & RENDER_TEXTURES;
			bool renderLightmaps = g_render_flags & RENDER_LIGHTMAPS;
			bool renderWireframe = g_render_flags & RENDER_WIREFRAME;
			bool renderEntities = g_render_flags & RENDER_ENTS;
			bool renderSpecial = g_render_flags & RENDER_SPECIAL;
			bool renderSpecialEnts = g_render_flags & RENDER_SPECIAL_ENTS;
			bool renderPointEnts = g_render_flags & RENDER_POINT_ENTS;
			bool renderOrigin = g_render_flags & RENDER_ORIGIN;
			bool renderWorldClipnodes = g_render_flags & RENDER_WORLD_CLIPNODES;
			bool renderEntClipnodes = g_render_flags & RENDER_ENT_CLIPNODES;
			bool renderEntConnections = g_render_flags & RENDER_ENT_CONNECTIONS;
			bool transparentNodes = g_render_flags & RENDER_TRANSPARENT;
			bool renderModels = g_render_flags & RENDER_MODELS;
			bool renderAnimatedModels = g_render_flags & RENDER_MODELS_ANIMATED;

			ImGui::Text(get_localized_string(LANG_0779).c_str());

			ImGui::Columns(2, 0, false);

			if (ImGui::Checkbox(get_localized_string(LANG_0780).c_str(), &renderTextures))
			{
				g_render_flags ^= RENDER_TEXTURES;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0781).c_str(), &renderLightmaps))
			{
				g_render_flags ^= RENDER_LIGHTMAPS;
				for (int i = 0; i < app->mapRenderers.size(); i++)
					app->mapRenderers[i]->updateModelShaders();
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0782).c_str(), &renderWireframe))
			{
				g_render_flags ^= RENDER_WIREFRAME;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_1116).c_str(), &renderOrigin))
			{
				g_render_flags ^= RENDER_ORIGIN;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0783).c_str(), &renderEntConnections))
			{
				g_render_flags ^= RENDER_ENT_CONNECTIONS;
				if (g_render_flags & RENDER_ENT_CONNECTIONS)
				{
					app->updateEntConnections();
				}
			}

			if (ImGui::Checkbox(get_localized_string(LANG_0784).c_str(), &renderPointEnts))
			{
				g_render_flags ^= RENDER_POINT_ENTS;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0785).c_str(), &renderEntities))
			{
				g_render_flags ^= RENDER_ENTS;
			}

			ImGui::NextColumn();
			if (ImGui::Checkbox(get_localized_string(LANG_0786).c_str(), &renderSpecialEnts))
			{
				g_render_flags ^= RENDER_SPECIAL_ENTS;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0787).c_str(), &renderSpecial))
			{
				g_render_flags ^= RENDER_SPECIAL;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0788).c_str(), &renderModels))
			{
				g_render_flags ^= RENDER_MODELS;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0789).c_str(), &renderAnimatedModels))
			{
				g_render_flags ^= RENDER_MODELS_ANIMATED;
			}

			if (ImGui::Checkbox(get_localized_string(LANG_0790).c_str(), &renderWorldClipnodes))
			{
				g_render_flags ^= RENDER_WORLD_CLIPNODES;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0791).c_str(), &renderEntClipnodes))
			{
				g_render_flags ^= RENDER_ENT_CLIPNODES;
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0792).c_str(), &transparentNodes))
			{
				g_render_flags ^= RENDER_TRANSPARENT;
				for (int i = 0; i < app->mapRenderers.size(); i++)
				{
					app->mapRenderers[i]->updateClipnodeOpacity(transparentNodes ? 128 : 255);
				}
			}

			ImGui::Columns(1);

			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0793).c_str());

			for (int i = 0; i < g_settings.transparentTextures.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##transTex" + std::to_string(i)).c_str(), &g_settings.transparentTextures[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##transTex" + std::to_string(i)).c_str()))
				{
					g_settings.transparentTextures.erase(g_settings.transparentTextures.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0794).c_str()))
			{
				g_settings.transparentTextures.emplace_back(std::string());
			}

			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0795).c_str());

			for (int i = 0; i < g_settings.transparentEntities.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##transEnt" + std::to_string(i)).c_str(), &g_settings.transparentEntities[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##transEnt" + std::to_string(i)).c_str()))
				{
					g_settings.transparentEntities.erase(g_settings.transparentEntities.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0796).c_str()))
			{
				g_settings.transparentEntities.emplace_back(std::string());
			}


			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0797).c_str());

			for (int i = 0; i < g_settings.entsNegativePitchPrefix.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth);
				ImGui::InputText(("##invPitch" + std::to_string(i)).c_str(), &g_settings.entsNegativePitchPrefix[i]);
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
				if (ImGui::Button((" X ##invPitch" + std::to_string(i)).c_str()))
				{
					g_settings.entsNegativePitchPrefix.erase(g_settings.entsNegativePitchPrefix.begin() + i);
				}
				ImGui::PopStyleColor(3);
			}

			if (ImGui::Button(get_localized_string(LANG_0798).c_str()))
			{
				g_settings.entsNegativePitchPrefix.emplace_back(std::string());
			}
		}
		else if (settingsTab == 6)
		{
			ImGui::DragFloat(get_localized_string(LANG_0799).c_str(), &app->moveSpeed, 1.0f, 100.0f, 1000.0f, "%.1f");
			ImGui::DragFloat(get_localized_string(LANG_0800).c_str(), &app->rotationSpeed, 0.1f, 0.1f, 100.0f, "%.1f");
		}

		ImGui::EndChild();
		ImGui::EndChild();

		ImGui::EndGroup();
	}
	ImGui::End();


	if (oldShowSettings && !showSettingsWidget || apply_settings_pressed)
	{
		g_settings.selected_lang = langForSelect;
		set_localize_lang(g_settings.selected_lang);
		g_settings.save();
		if (!app->reloading)
		{
			app->reloading = true;
			app->loadFgds();
			app->postLoadFgds();
			for (int i = 0; i < app->mapRenderers.size(); i++)
			{
				BspRenderer* mapRender = app->mapRenderers[i];
				mapRender->reload();
			}
			app->reloading = false;
		}
		oldShowSettings = showSettingsWidget = apply_settings_pressed;
	}
}

void Gui::drawHelp()
{
	ImGui::SetNextWindowSize(ImVec2(600.f, 400.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_localized_string(LANG_1117).c_str(), &showHelpWidget))
	{

		if (ImGui::BeginTabBar(get_localized_string(LANG_1118).c_str()))
		{
			if (ImGui::BeginTabItem(get_localized_string(LANG_0801).c_str()))
			{
				ImGui::Dummy(ImVec2(0, 10));

				// user guide from the demo
				ImGui::BulletText(get_localized_string(LANG_0802).c_str());
				ImGui::BulletText(get_localized_string(LANG_0803).c_str());
				ImGui::Indent();
				ImGui::BulletText(get_localized_string(LANG_0804).c_str());
				ImGui::BulletText(get_localized_string(LANG_0805).c_str());
				ImGui::Unindent();
				ImGui::BulletText(get_localized_string(LANG_0806).c_str());
				ImGui::Indent();
				ImGui::BulletText(get_localized_string(LANG_0807).c_str());
				ImGui::BulletText(get_localized_string(LANG_0808).c_str());
				ImGui::BulletText(get_localized_string(LANG_0809).c_str());
				ImGui::BulletText(get_localized_string(LANG_0810).c_str());
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(get_localized_string(LANG_0811).c_str()))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGui::BulletText(get_localized_string(LANG_0812).c_str());
				ImGui::BulletText(get_localized_string(LANG_0813).c_str());
				ImGui::BulletText(get_localized_string(LANG_0814).c_str());
				ImGui::BulletText(get_localized_string(LANG_0815).c_str());
				ImGui::Indent();
				ImGui::BulletText(get_localized_string(LANG_0816).c_str());
				ImGui::BulletText(get_localized_string(LANG_0817).c_str());
				ImGui::Unindent();
				ImGui::BulletText(get_localized_string(LANG_0818).c_str());
				ImGui::Indent();
				ImGui::BulletText(get_localized_string(LANG_0819).c_str());
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(get_localized_string(LANG_0820).c_str()))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGui::BulletText(get_localized_string(LANG_0821).c_str());
				ImGui::Unindent();

				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void Gui::drawAbout()
{
	ImGui::SetNextWindowSize(ImVec2(550.f, 140.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_localized_string(LANG_1119).c_str(), &showAboutWidget))
	{
		ImGui::InputText(get_localized_string(LANG_0822).c_str(), &g_version_string, ImGuiInputTextFlags_ReadOnly);

		static char author[] = "w00tguy(bspguy), karaulov(newbspguy)";
		ImGui::InputText(get_localized_string(LANG_0823).c_str(), author, strlen(author), ImGuiInputTextFlags_ReadOnly);

		static char url[] = "https://github.com/wootguy/bspguy";
		ImGui::InputText(get_localized_string(LANG_0824).c_str(), url, strlen(url), ImGuiInputTextFlags_ReadOnly);

		static char url2[] = "https://github.com/UnrealKaraulov/newbspguy";
		ImGui::InputText(get_localized_string(LANG_0824).c_str(), url, strlen(url), ImGuiInputTextFlags_ReadOnly);
	}

	ImGui::End();
}

void Gui::drawMergeWindow()
{
	ImGui::SetNextWindowSize(ImVec2(500.f, 240.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(500.f, 240.f), ImVec2(500.f, 240.f));
	static std::string outPath;
	static std::vector<std::string> inPaths;
	static bool DeleteUnusedInfo = true;
	static bool Optimize = false;
	static bool DeleteHull2 = false;
	static bool NoRipent = false;
	static bool NoScript = true;


	if (ImGui::Begin(get_localized_string(LANG_0825).c_str(), &showMergeMapWidget))
	{

		if (inPaths.size() < 2)
		{
			inPaths.push_back(std::string());
			inPaths.push_back(std::string());
		}

		ImGui::InputText(get_localized_string(LANG_0826).c_str(), &inPaths[0]);
		ImGui::InputText(get_localized_string(LANG_0827).c_str(), &inPaths[1]);
		int p = 1;
		while (inPaths[p].size())
		{
			p++;
			if (inPaths.size() < p)
				inPaths.push_back(std::string());
			ImGui::InputText(get_localized_string(LANG_1120).c_str(), &inPaths[p]);
		}

		ImGui::InputText(get_localized_string(LANG_0828).c_str(), &outPath);
		ImGui::Checkbox(get_localized_string(LANG_0829).c_str(), &DeleteUnusedInfo);
		ImGui::Checkbox(get_localized_string(LANG_1121).c_str(), &Optimize);
		ImGui::Checkbox(get_localized_string(LANG_0830).c_str(), &DeleteHull2);
		ImGui::Checkbox(get_localized_string(LANG_0831).c_str(), &NoRipent);
		ImGui::Checkbox(get_localized_string(LANG_0832).c_str(), &NoScript);

		if (ImGui::Button(get_localized_string(LANG_1122).c_str(), ImVec2(120, 0)))
		{
			std::vector<Bsp*> maps;
			for (int i = 1; i < 16; i++)
			{
				if (i == 0 || inPaths[i - 1].size())
				{
					if (fileExists(inPaths[i - 1]))
					{
						Bsp* tmpMap = new Bsp(inPaths[i - 1]);
						if (tmpMap->bsp_valid)
						{
							maps.push_back(tmpMap);
						}
						else
						{
							delete tmpMap;
							continue;
						}
					}
				}
				else
					break;
			}
			if (maps.size() < 2)
			{
				for (auto& map : maps)
					delete map;
				maps.clear();
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1056));
			}
			else
			{
				for (int i = 0; i < maps.size(); i++)
				{
					print_log(get_localized_string(LANG_1057), maps[i]->bsp_name);
					if (DeleteUnusedInfo)
					{
						print_log(get_localized_string(LANG_1058));
						STRUCTCOUNT removed = maps[i]->remove_unused_model_structures();
						g_progress.clear();
						removed.print_delete_stats(2);
					}

					if (DeleteHull2 || (Optimize && !maps[i]->has_hull2_ents()))
					{
						print_log(get_localized_string(LANG_1059));
						maps[i]->delete_hull(2, 1);
						maps[i]->remove_unused_model_structures().print_delete_stats(2);
					}

					if (Optimize)
					{
						print_log(get_localized_string(LANG_1060));
						maps[i]->delete_unused_hulls().print_delete_stats(2);
					}

					print_log("\n");
				}
				BspMerger merger;
				Bsp* result = merger.merge(maps, vec3(), outPath, NoRipent, NoScript);

				print_log("\n");
				if (result->isValid()) result->write(outPath);
				print_log("\n");
				result->print_info(false, 0, 0);

				app->clearMaps();

				fixupPath(outPath, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);

				if (fileExists(outPath))
				{
					result = new Bsp(outPath);
					app->addMap(result);
				}
				else
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0398));
					app->addMap(new Bsp());
				}

				for (auto& map : maps)
					delete map;
				maps.clear();
			}
			showMergeMapWidget = false;
		}
	}

	ImGui::End();
}

void Gui::drawImportMapWidget()
{
	ImGui::SetNextWindowSize(ImVec2(500.f, 140.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(500.f, 140.f), ImVec2(500.f, 140.f));
	static std::string mapPath;
	const char* title = "Import .bsp model as func_breakable entity";

	if (showImportMapWidget_Type == SHOW_IMPORT_OPEN)
	{
		title = "Open map";
	}
	else if (showImportMapWidget_Type == SHOW_IMPORT_ADD_NEW)
	{
		title = "Add map to renderer";
	}
	else if (showImportMapWidget_Type == SHOW_IMPORT_MODEL_BSP)
	{
		title = "Copy BSP model to current map";
	}
	else if (showImportMapWidget_Type == SHOW_IMPORT_MODEL_ENTITY)
	{
		title = "Create func_breakable with bsp model path";
	}

	if (ImGui::Begin(title, &showImportMapWidget))
	{
		if (ifd::FileDialog::Instance().IsDone("BspOpenDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				mapPath = res.string();
				g_settings.lastdir = res.parent_path().string();
			}
			ifd::FileDialog::Instance().Close();
		}


		ImGui::InputText(get_localized_string(LANG_0833).c_str(), &mapPath);
		ImGui::SameLine();

		if (ImGui::Button(get_localized_string(LANG_0834).c_str()))
		{
			ifd::FileDialog::Instance().Open("BspOpenDialog", "Opep bsp model", "BSP file (*.bsp){.bsp},.*", false, g_settings.lastdir);
		}

		if (ImGui::Button(get_localized_string(LANG_0835).c_str(), ImVec2(120, 0)))
		{
			fixupPath(mapPath, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);
			if (fileExists(mapPath))
			{
				print_log(get_localized_string(LANG_0399), mapPath);
				showImportMapWidget = false;
				if (showImportMapWidget_Type == SHOW_IMPORT_ADD_NEW)
				{
					app->addMap(new Bsp(mapPath));
				}
				else if (showImportMapWidget_Type == SHOW_IMPORT_OPEN)
				{
					app->clearMaps();
					app->addMap(new Bsp(mapPath));
				}
				else if (showImportMapWidget_Type == SHOW_IMPORT_MODEL_BSP)
				{
					Bsp* bspModel = new Bsp(mapPath);
					BspRenderer* mapRenderer = new BspRenderer(bspModel, app->pointEntRenderer);
					Bsp* map = app->getSelectedMap();

					std::vector<BSPPLANE> newPlanes;
					std::vector<vec3> newVerts;
					std::vector<BSPEDGE32> newEdges;
					std::vector<int> newSurfedges;
					std::vector<BSPTEXTUREINFO> newTexinfo;
					std::vector<BSPFACE32> newFaces;
					std::vector<COLOR3> newLightmaps;
					std::vector<BSPNODE32> newNodes;
					std::vector<BSPCLIPNODE32> newClipnodes;

					STRUCTREMAP* remap = new STRUCTREMAP(map);

					bspModel->copy_bsp_model(0, map, *remap, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces, newLightmaps, newNodes, newClipnodes);

					if (newClipnodes.size())
					{
						map->append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE32) * newClipnodes.size());
					}
					if (newEdges.size())
					{
						map->append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE32) * newEdges.size());
					}
					if (newFaces.size())
					{
						map->append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE32) * newFaces.size());
					}
					if (newNodes.size())
					{
						map->append_lump(LUMP_NODES, &newNodes[0], sizeof(BSPNODE32) * newNodes.size());
					}
					if (newPlanes.size())
					{
						map->append_lump(LUMP_PLANES, &newPlanes[0], sizeof(BSPPLANE) * newPlanes.size());
					}
					if (newSurfedges.size())
					{
						map->append_lump(LUMP_SURFEDGES, &newSurfedges[0], sizeof(int) * newSurfedges.size());
					}
					if (newTexinfo.size())
					{
						for (auto& texinfo : newTexinfo)
						{
							if (texinfo.iMiptex < 0 || texinfo.iMiptex >= map->textureCount)
								continue;
							int newMiptex = -1;
							int texOffset = ((int*)bspModel->textures)[texinfo.iMiptex + 1];
							if (texOffset < 0)
								continue;
							BSPMIPTEX& tex = *((BSPMIPTEX*)(bspModel->textures + texOffset));
							for (int i = 0; i < map->textureCount; i++)
							{
								int tex2Offset = ((int*)map->textures)[i + 1];
								if (tex2Offset >= 0)
								{
									BSPMIPTEX& tex2 = *((BSPMIPTEX*)(map->textures + tex2Offset));
									if (strcasecmp(tex.szName, tex2.szName) == 0)
									{
										newMiptex = i;
										break;
									}
								}
							}
							if (newMiptex < 0 && bspModel->getBspRender() && bspModel->getBspRender()->wads.size())
							{
								for (auto& s : bspModel->getBspRender()->wads)
								{
									if (s->hasTexture(tex.szName))
									{
										WADTEX* wadTex = s->readTexture(tex.szName);
										COLOR3* imageData = ConvertWadTexToRGB(wadTex);

										texinfo.iMiptex = map->add_texture(tex.szName, (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight);

										if (texinfo.iMiptex == -1)
											texinfo.iMiptex = 0;

										delete[] imageData;
										delete wadTex;
										break;
									}
								}
							}
							else
							{
								if (newMiptex == -1)
									newMiptex = 0;
								texinfo.iMiptex = newMiptex;
							}
						}
						map->append_lump(LUMP_TEXINFO, &newTexinfo[0], sizeof(BSPTEXTUREINFO) * newTexinfo.size());
					}
					if (newVerts.size())
					{
						map->append_lump(LUMP_VERTICES, &newVerts[0], sizeof(vec3) * newVerts.size());
					}
					if (newLightmaps.size())
					{
						map->append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());
					}

					int newModelIdx = map->create_model();
					BSPMODEL& oldModel = bspModel->models[0];
					BSPMODEL& newModel = map->models[newModelIdx];
					memcpy(&newModel, &oldModel, sizeof(BSPMODEL));

					newModel.iFirstFace = (*remap).faces[oldModel.iFirstFace];
					newModel.iHeadnodes[0] = oldModel.iHeadnodes[0] < 0 ? -1 : (*remap).nodes[oldModel.iHeadnodes[0]];

					for (int i = 1; i < MAX_MAP_HULLS; i++)
					{
						newModel.iHeadnodes[i] = oldModel.iHeadnodes[i] < 0 ? -1 : (*remap).clipnodes[oldModel.iHeadnodes[i]];
					}

					newModel.nVisLeafs = 0;

					app->deselectObject();

					map->ents.push_back(new Entity("func_wall"));
					map->ents[map->ents.size() - 1]->setOrAddKeyvalue("model", "*" + std::to_string(newModelIdx));
					map->ents[map->ents.size() - 1]->setOrAddKeyvalue("origin", "0 0 0");
					map->update_ent_lump();
					app->updateEnts();

					map->getBspRender()->reload();
					delete mapRenderer;
				}
				else if (showImportMapWidget_Type == SHOW_IMPORT_MODEL_ENTITY)
				{
					Bsp* map = app->getSelectedMap();
					if (map)
					{
						Bsp* model = new Bsp(mapPath);
						if (!model->ents.size())
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0400));
						}
						else
						{
							print_log(get_localized_string(LANG_0401));
							Entity* tmpEnt = new Entity("func_breakable");
							tmpEnt->setOrAddKeyvalue("gibmodel", std::string("models/") + basename(mapPath));
							tmpEnt->setOrAddKeyvalue("model", std::string("models/") + basename(mapPath));
							tmpEnt->setOrAddKeyvalue("spawnflags", "1");
							tmpEnt->setOrAddKeyvalue("origin", cameraOrigin.toKeyvalueString());
							map->ents.push_back(tmpEnt);
							map->update_ent_lump();
							print_log(get_localized_string(LANG_0402), std::string("models/") + basename(mapPath));
							app->updateEnts();
							app->reloadBspModels();
						}
						delete model;
					}
				}
			}
			else
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0403));
			}
		}
	}
	ImGui::End();
}

void Gui::drawLimits()
{
	ImGui::SetNextWindowSize(ImVec2(550.f, 630.f), ImGuiCond_FirstUseEver);

	Bsp* map = app->getSelectedMap();
	std::string title = map ? "Limits - " + map->bsp_name : "Limits";

	if (ImGui::Begin((title + "###limits").c_str(), &showLimitsWidget))
	{

		if (!map)
		{
			ImGui::Text(get_localized_string(LANG_1123).c_str());
		}
		else
		{
			if (ImGui::BeginTabBar(get_localized_string(LANG_1166).c_str()))
			{
				if (ImGui::BeginTabItem(get_localized_string(LANG_0836).c_str()))
				{

					if (!loadedStats)
					{
						stats.clear();
						stats.push_back(calcStat("models", map->modelCount, MAX_MAP_MODELS, false));
						stats.push_back(calcStat("planes", map->planeCount, MAX_MAP_PLANES, false));
						stats.push_back(calcStat("vertexes", map->vertCount, MAX_MAP_VERTS, false));
						stats.push_back(calcStat("nodes", map->nodeCount, MAX_MAP_NODES, false));
						stats.push_back(calcStat("texinfos", map->texinfoCount, MAX_MAP_TEXINFOS, false));
						stats.push_back(calcStat("faces", map->faceCount, MAX_MAP_FACES, false));
						stats.push_back(calcStat("clipnodes", map->clipnodeCount, map->is_32bit_clipnodes ? INT_MAX : MAX_MAP_CLIPNODES, false));
						stats.push_back(calcStat("leaves", map->leafCount, MAX_MAP_LEAVES, false));
						stats.push_back(calcStat("marksurfaces", map->marksurfCount, MAX_MAP_MARKSURFS, false));
						stats.push_back(calcStat("surfedges", map->surfedgeCount, MAX_MAP_SURFEDGES, false));
						stats.push_back(calcStat("edges", map->edgeCount, MAX_MAP_EDGES, false));
						stats.push_back(calcStat("textures", map->textureCount, MAX_MAP_TEXTURES, false));
						stats.push_back(calcStat("texturedata", map->textureDataLength, INT_MAX, true));
						stats.push_back(calcStat("lightdata", map->lightDataLength, MAX_MAP_LIGHTDATA, true));
						stats.push_back(calcStat("visdata", map->visDataLength, MAX_MAP_VISDATA, true));
						stats.push_back(calcStat("entities", (unsigned int)map->ents.size(), MAX_MAP_ENTS, false));
						loadedStats = true;
					}

					ImGui::BeginChild(get_localized_string(LANG_0837).c_str());
					ImGui::Dummy(ImVec2(0, 10));
					ImGui::PushFont(consoleFontLarge);

					float midWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, "    Current / Max    ").x;
					float otherWidth = (ImGui::GetWindowWidth() - midWidth) / 2;
					ImGui::Columns(3);
					ImGui::SetColumnWidth(0, otherWidth);
					ImGui::SetColumnWidth(1, midWidth);
					ImGui::SetColumnWidth(2, otherWidth);

					ImGui::Text(get_localized_string(LANG_0838).c_str()); ImGui::NextColumn();
					ImGui::Text(get_localized_string(LANG_0839).c_str()); ImGui::NextColumn();
					ImGui::Text(get_localized_string(LANG_0840).c_str()); ImGui::NextColumn();

					ImGui::Columns(1);
					ImGui::Separator();
					ImGui::BeginChild(get_localized_string(LANG_0841).c_str());
					ImGui::Columns(3);
					ImGui::SetColumnWidth(0, otherWidth);
					ImGui::SetColumnWidth(1, midWidth);
					ImGui::SetColumnWidth(2, otherWidth);

					for (int i = 0; i < stats.size(); i++)
					{
						ImGui::TextColored(stats[i].color, stats[i].name.c_str()); ImGui::NextColumn();

						std::string val = stats[i].val + " / " + stats[i].max;
						ImGui::TextColored(stats[i].color, val.c_str());
						ImGui::NextColumn();

						ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.5f, 0.4f, 0, 1));
						ImGui::ProgressBar(stats[i].progress, ImVec2(-1, 0), stats[i].fullness.c_str());
						ImGui::PopStyleColor(1);
						ImGui::NextColumn();
					}

					ImGui::Columns(1);
					ImGui::EndChild();
					ImGui::PopFont();
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_1177).c_str()))
				{
					drawLimitTab(map, SORT_CLIPNODES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_0842).c_str()))
				{
					drawLimitTab(map, SORT_NODES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_0843).c_str()))
				{
					drawLimitTab(map, SORT_FACES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_0844).c_str()))
				{
					drawLimitTab(map, SORT_VERTS);
					ImGui::EndTabItem();
				}
			}

			ImGui::EndTabBar();
		}
	}

	ImGui::End();
}

void Gui::drawLimitTab(Bsp* map, int sortMode)
{

	int maxCount = 0;
	const char* countName = "None";
	switch (sortMode)
	{
	case SORT_VERTS:		maxCount = map->vertCount; countName = "Vertexes";  break;
	case SORT_NODES:		maxCount = map->nodeCount; countName = "Nodes";  break;
	case SORT_CLIPNODES:	maxCount = map->clipnodeCount; countName = "Clipnodes";  break;
	case SORT_FACES:		maxCount = map->faceCount; countName = "Faces";  break;
	}

	if (!loadedLimit[sortMode])
	{
		std::vector<STRUCTUSAGE*> modelInfos = map->get_sorted_model_infos(sortMode);

		limitModels[sortMode].clear();
		for (int i = 0; i < modelInfos.size(); i++)
		{
			int val = 0;

			switch (sortMode)
			{
			case SORT_VERTS:		val = modelInfos[i]->sum.verts; break;
			case SORT_NODES:		val = modelInfos[i]->sum.nodes; break;
			case SORT_CLIPNODES:	val = modelInfos[i]->sum.clipnodes; break;
			case SORT_FACES:		val = modelInfos[i]->sum.faces; break;
			}

			ModelInfo stat = calcModelStat(map, modelInfos[i], val, maxCount, false);
			limitModels[sortMode].push_back(stat);
			delete modelInfos[i];
		}
		loadedLimit[sortMode] = true;
	}
	std::vector<ModelInfo>& modelInfos = limitModels[sortMode];

	ImGui::BeginChild(get_localized_string(LANG_1124).c_str());
	ImGui::Dummy(ImVec2(0, 10));
	ImGui::PushFont(consoleFontLarge);

	float valWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.2f, FLT_MAX, FLT_MAX, " Clipnodes ").x;
	float usageWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.2f, FLT_MAX, FLT_MAX, "  Usage   ").x;
	float modelWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.2f, FLT_MAX, FLT_MAX, " Model ").x;
	float bigWidth = ImGui::GetWindowWidth() - (valWidth + usageWidth + modelWidth);
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	ImGui::Text(get_localized_string(LANG_0845).c_str()); ImGui::NextColumn();
	ImGui::Text(get_localized_string(LANG_0846).c_str()); ImGui::NextColumn();
	ImGui::Text(countName); ImGui::NextColumn();
	ImGui::Text(get_localized_string(LANG_0847).c_str()); ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::Separator();
	ImGui::BeginChild(get_localized_string(LANG_1125).c_str());
	ImGui::Columns(4);
	ImGui::SetColumnWidth(0, bigWidth);
	ImGui::SetColumnWidth(1, modelWidth);
	ImGui::SetColumnWidth(2, valWidth);
	ImGui::SetColumnWidth(3, usageWidth);

	int selected = app->pickInfo.GetSelectedEnt() >= 0;

	for (int i = 0; i < limitModels[sortMode].size(); i++)
	{

		if (modelInfos[i].val == "0")
		{
			break;
		}

		std::string cname = modelInfos[i].classname + "##" + "select" + std::to_string(i);
		int flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		if (ImGui::Selectable(cname.c_str(), selected == modelInfos[i].entIdx, flags))
		{
			selected = i;
			int entIdx = modelInfos[i].entIdx;
			if (entIdx < map->ents.size())
			{
				app->pickInfo.SetSelectedEnt(entIdx);
				// map should already be valid if limits are showing

				if (ImGui::IsMouseDoubleClicked(0))
				{
					app->goToEnt(map, entIdx);
				}
			}
		}
		ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(modelInfos[i].model.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(modelInfos[i].model.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(modelInfos[i].val.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(modelInfos[i].val.c_str()); ImGui::NextColumn();

		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth()
			- ImGui::CalcTextSize(modelInfos[i].usage.c_str()).x
			- ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
		ImGui::Text(modelInfos[i].usage.c_str()); ImGui::NextColumn();
	}


	ImGui::Columns(1);
	ImGui::EndChild();

	ImGui::PopFont();
	ImGui::EndChild();
}

void Gui::drawEntityReport()
{
	ImGui::SetNextWindowSize(ImVec2(550.f, 630.f), ImGuiCond_FirstUseEver);
	Bsp* map = app->getSelectedMap();

	std::string title = map ? "Entity Report - " + map->bsp_name : "Entity Report";

	if (ImGui::Begin((title + "###entreport").c_str(), &showEntityReport))
	{
		if (!map)
		{
			ImGui::Text(get_localized_string(LANG_1167).c_str());
		}
		else
		{
			ImGui::BeginGroup();
			static float startFrom = 0.0f;
			static int MAX_FILTERS = 1;
			static std::vector<std::string> keyFilter = std::vector<std::string>();
			static std::vector<std::string> valueFilter = std::vector<std::string>();
			static int lastSelect = -1;
			static std::string classFilter = "(none)";
			static std::string flagsFilter = "(none)";
			static bool partialMatches = true;
			static std::vector<int> visibleEnts;
			static std::vector<bool> selectedItems;
			static bool selectAllItems = false;

			const ImGuiKeyChord expected_key_mod_flags = imgui_io->KeyMods;

			float footerHeight = ImGui::GetFrameHeightWithSpacing() * 5.f + 16.f;
			ImGui::BeginChild(get_localized_string(LANG_0848).c_str(), ImVec2(0.f, -footerHeight));

			if (filterNeeded)
			{
				visibleEnts.clear();
				while (keyFilter.size() < MAX_FILTERS)
					keyFilter.push_back(std::string());
				while (valueFilter.size() < MAX_FILTERS)
					valueFilter.push_back(std::string());

				for (int i = 1; i < map->ents.size(); i++)
				{
					Entity* ent = map->ents[i];
					std::string cname = ent->keyvalues["classname"];

					bool visible = true;

					if (!classFilter.empty() && classFilter != "(none)")
					{
						if (strcasecmp(cname.c_str(), classFilter.c_str()) != 0)
						{
							visible = false;
						}
					}

					if (!flagsFilter.empty() && flagsFilter != "(none)")
					{
						visible = false;
						FgdClass* fgdClass = app->fgd->getFgdClass(ent->keyvalues["classname"]);
						if (fgdClass)
						{
							for (int k = 0; k < 32; k++)
							{
								if (fgdClass->spawnFlagNames[k] == flagsFilter)
								{
									visible = true;
								}
							}
						}
					}

					for (int k = 0; k < MAX_FILTERS; k++)
					{
						if (keyFilter[k].size() && keyFilter[k][0] != '\0')
						{
							std::string searchKey = trimSpaces(toLowerCase(keyFilter[k]));

							bool foundKey = false;
							std::string actualKey;
							for (int c = 0; c < ent->keyOrder.size(); c++)
							{
								std::string key = toLowerCase(ent->keyOrder[c]);
								if (key == searchKey || (partialMatches && key.find(searchKey) != std::string::npos))
								{
									foundKey = true;
									actualKey = std::move(key);
									break;
								}
							}
							if (!foundKey)
							{
								visible = false;
								break;
							}

							std::string searchValue = trimSpaces(toLowerCase(valueFilter[k]));
							if (!searchValue.empty())
							{
								if ((partialMatches && ent->keyvalues[actualKey].find(searchValue) == std::string::npos) ||
									(!partialMatches && ent->keyvalues[actualKey] != searchValue))
								{
									visible = false;
									break;
								}
							}
						}
						else if (valueFilter[k].size() && valueFilter[k][0] != '\0')
						{
							std::string searchValue = trimSpaces(toLowerCase(valueFilter[k]));
							bool foundMatch = false;
							for (int c = 0; c < ent->keyOrder.size(); c++)
							{
								std::string val = toLowerCase(ent->keyvalues[ent->keyOrder[c]]);
								if (val == searchValue || (partialMatches && val.find(searchValue) != std::string::npos))
								{
									foundMatch = true;
									break;
								}
							}
							if (!foundMatch)
							{
								visible = false;
								break;
							}
						}
					}
					if (visible)
					{
						visibleEnts.push_back(i);
					}
				}

				selectedItems.clear();
				selectedItems.resize(visibleEnts.size());
				for (int k = 0; k < selectedItems.size(); k++)
				{
					if (selectAllItems)
					{
						selectedItems[k] = true;
						if (!app->pickInfo.IsSelectedEnt(visibleEnts[k]))
						{
							app->selectEnt(map, visibleEnts[k], true);
						}
					}
					else
					{
						selectedItems[k] = app->pickInfo.IsSelectedEnt(visibleEnts[k]);
					}
				}
				selectAllItems = false;
			}

			filterNeeded = false;

			ImGuiListClipper clipper;

			if (startFrom >= 0.0f)
			{
				ImGui::SetScrollY(startFrom);
				startFrom = -1.0f;
			}

			clipper.Begin((int)visibleEnts.size());
			static bool needhover = true;
			static bool isHovered = false;
			while (clipper.Step())
			{
				for (int line = clipper.DisplayStart; line < clipper.DisplayEnd && line < visibleEnts.size() && visibleEnts[line] < map->ents.size(); line++)
				{
					int i = line;
					Entity* ent = map->ents[visibleEnts[i]];
					std::string cname = "UNKNOWN_CLASSNAME";


					if (ent && ent->hasKey("classname") && !ent->keyvalues["classname"].empty())
					{
						cname = ent->keyvalues["classname"];
					}
					if (g_app->curRightMouse == GLFW_RELEASE)
						needhover = true;

					bool isSelectableSelected = false;
					if (!app->fgd || !app->fgd->getFgdClass(cname) || (ent && ent->hide))
					{
						if (!app->fgd || !app->fgd->getFgdClass(cname))
						{
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 0, 0, 255));
						}
						else
						{
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 255, 255, 255));
						}
						isSelectableSelected = ImGui::Selectable((cname + "##ent" + std::to_string(i)).c_str(), selectedItems[i], ImGuiSelectableFlags_AllowDoubleClick);

						isHovered = ImGui::IsItemHovered() && needhover;

						if (isHovered)
						{
							ImGui::BeginTooltip();
							if (!app->fgd || !app->fgd->getFgdClass(cname))
							{
								ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0404)), cname).c_str());
							}
							else
							{
								ImGui::Text(fmt::format("{}", "This entity is hidden on map, press 'unhide' to show it!").c_str());
							}
							ImGui::EndTooltip();
						}
						ImGui::PopStyleColor();
					}
					else
					{
						isSelectableSelected = ImGui::Selectable((cname + "##ent" + std::to_string(i)).c_str(), selectedItems[i], ImGuiSelectableFlags_AllowDoubleClick);

						isHovered = ImGui::IsItemHovered() && needhover;
					}
					bool isForceOpen = (isHovered && g_app->oldRightMouse == GLFW_RELEASE && g_app->curRightMouse == GLFW_PRESS);

					if (isSelectableSelected || isForceOpen)
					{
						if (isForceOpen)
						{
							needhover = false;
						}
						if (expected_key_mod_flags & ImGuiModFlags_Ctrl)
						{
							selectedItems[i] = !selectedItems[i];
							lastSelect = i;
							app->pickInfo.selectedEnts.clear();
							for (int k = 0; k < selectedItems.size(); k++)
							{
								if (selectedItems[k])
								{
									app->selectEnt(map, visibleEnts[k], true);
								}
							}
						}
						else if (expected_key_mod_flags & ImGuiModFlags_Shift)
						{
							if (lastSelect >= 0)
							{
								int begin = i > lastSelect ? lastSelect : i;
								int end = i > lastSelect ? i : lastSelect;
								for (int k = 0; k < selectedItems.size(); k++)
									selectedItems[k] = false;
								for (int k = begin; k < end; k++)
									selectedItems[k] = true;
								selectedItems[lastSelect] = true;
								selectedItems[i] = true;
							}


							app->pickInfo.selectedEnts.clear();
							for (int k = 0; k < selectedItems.size(); k++)
							{
								if (selectedItems[k])
								{
									app->selectEnt(map, visibleEnts[k], true);
								}
							}
						}
						else
						{
							for (int k = 0; k < selectedItems.size(); k++)
								selectedItems[k] = false;
							if (i < 0)
								i = 0;
							selectedItems[i] = true;
							lastSelect = i;
							app->pickInfo.selectedEnts.clear();
							app->selectEnt(map, visibleEnts[i], true);
							if (ImGui::IsMouseDoubleClicked(0) || app->pressed[GLFW_KEY_SPACE])
							{
								app->goToEnt(map, visibleEnts[i]);
							}
						}
						if (isForceOpen)
						{
							needhover = false;
							ImGui::OpenPopup(get_localized_string(LANG_1178).c_str());
						}
					}
					if (isHovered)
					{
						if (!app->pressed[GLFW_KEY_A] && app->oldPressed[GLFW_KEY_A] && app->anyCtrlPressed)
						{
							selectAllItems = true;
							filterNeeded = true;
						}
					}
				}
			}
			if (map && !map->is_mdl_model)
			{
				draw3dContextMenus();
			}

			clipper.End();

			ImGui::EndChild();

			ImGui::BeginChild(get_localized_string(LANG_0849).c_str());

			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, 8));

			static std::vector<std::string> usedClasses;
			static std::set<std::string> uniqueClasses;

			static bool comboWasOpen = false;

			ImGui::SetNextItemWidth(280);
			ImGui::Text(get_localized_string(LANG_0850).c_str());
			ImGui::SameLine(280);
			ImGui::Text(get_localized_string(LANG_0851).c_str());
			ImGui::SetNextItemWidth(270);
			if (ImGui::BeginCombo(get_localized_string(LANG_0852).c_str(), classFilter.c_str()))
			{
				if (!comboWasOpen)
				{
					comboWasOpen = true;

					usedClasses.clear();
					uniqueClasses.clear();
					usedClasses.push_back("(none)");

					for (int i = 1; i < map->ents.size(); i++)
					{
						Entity* ent = map->ents[i];
						std::string cname = ent->keyvalues["classname"];

						if (uniqueClasses.find(cname) == uniqueClasses.end())
						{
							usedClasses.push_back(cname);
							uniqueClasses.insert(cname);
						}
					}
					sort(usedClasses.begin(), usedClasses.end());

				}

				for (int k = 0; k < usedClasses.size(); k++)
				{
					bool selected = usedClasses[k] == classFilter;
					if (ImGui::Selectable(usedClasses[k].c_str(), selected))
					{
						classFilter = usedClasses[k];
						filterNeeded = true;
					}
				}

				ImGui::EndCombo();
			}
			else
			{
				comboWasOpen = false;
			}


			ImGui::SameLine();
			ImGui::SetNextItemWidth(270);
			if (ImGui::BeginCombo(get_localized_string(LANG_0853).c_str(), flagsFilter.c_str()))
			{
				if (app->fgd)
				{
					if (ImGui::Selectable(get_localized_string(LANG_0854).c_str(), false))
					{
						flagsFilter = "(none)";
						filterNeeded = true;
					}
					else
					{
						for (int i = 0; i < app->fgd->existsFlagNames.size(); i++)
						{
							bool selected = flagsFilter == app->fgd->existsFlagNames[i];
							if (ImGui::Selectable((app->fgd->existsFlagNames[i] +
								" ( bit " + std::to_string(app->fgd->existsFlagNamesBits[i]) + " )").c_str(), selected))
							{
								flagsFilter = app->fgd->existsFlagNames[i];
								filterNeeded = true;
							}
						}
					}
				}
				ImGui::EndCombo();
			}

			ImGui::Dummy(ImVec2(0, 8));
			ImGui::Text(get_localized_string(LANG_0855).c_str());

			ImGuiStyle& style = ImGui::GetStyle();
			float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
			float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.4f;
			inputWidth -= smallFont->CalcTextSizeA(fontSize, FLT_MAX, FLT_MAX, " = ").x;

			while (keyFilter.size() < MAX_FILTERS)
				keyFilter.push_back(std::string());
			while (valueFilter.size() < MAX_FILTERS)
				valueFilter.push_back(std::string());

			for (int i = 0; i < MAX_FILTERS; i++)
			{
				ImGui::SetNextItemWidth(inputWidth);
				if (ImGui::InputText(("##Key" + std::to_string(i)).c_str(), &keyFilter[i]))
				{
					filterNeeded = true;
				}

				ImGui::SameLine();
				ImGui::Text(" = "); ImGui::SameLine();
				ImGui::SetNextItemWidth(inputWidth);

				if (ImGui::InputText(("##Value" + std::to_string(i)).c_str(), &valueFilter[i]))
				{
					filterNeeded = true;
				}

				if (i == 0)
				{
					ImGui::SameLine();
					if (ImGui::Button(get_localized_string(LANG_0856).c_str(), ImVec2(100, 0)))
					{
						MAX_FILTERS++;
						break;
					}
				}

				if (i == 1)
				{
					ImGui::SameLine();
					if (ImGui::Button(get_localized_string(LANG_1168).c_str(), ImVec2(100, 0)))
					{
						if (MAX_FILTERS > 1)
							MAX_FILTERS--;
						break;
					}
				}
			}

			if (ImGui::Checkbox(get_localized_string(LANG_0857).c_str(), &partialMatches))
			{
				filterNeeded = true;
			}

			ImGui::SameLine();

			if (ImGui::Button(get_localized_string(LANG_0858).c_str()))
			{
				app->goToEnt(map, app->pickInfo.GetSelectedEnt());
			}

			ImGui::SameLine();

			if (ImGui::Button(get_localized_string(LANG_0859).c_str()))
			{
				startFrom = (app->pickInfo.GetSelectedEnt() - 8) * clipper.ItemsHeight;
				if (startFrom < 0.0f)
					startFrom = 0.0f;
			}

			ImGui::EndChild();

			ImGui::EndGroup();
		}
	}

	ImGui::End();
}


static bool ColorPicker(ImGuiIO* imgui_io, float* col, bool alphabar)
{
	const int    EDGE_SIZE = 200; // = int( ImGui::GetWindowWidth() * 0.75f );
	const ImVec2 SV_PICKER_SIZE = ImVec2(EDGE_SIZE, EDGE_SIZE);
	const float  SPACING = ImGui::GetStyle().ItemInnerSpacing.x;
	const float  HUE_PICKER_WIDTH = 20.f;
	const float  CROSSHAIR_SIZE = 7.0f;

	ImColor color(col[0], col[1], col[2]);
	bool value_changed = false;

	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	// setup

	ImVec2 picker_pos = ImGui::GetCursorScreenPos();

	float hue, saturation, value;
	ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, hue, saturation, value);

	// draw hue bar

	ImColor colors[] = { ImColor(255, 0, 0),
		ImColor(255, 255, 0),
		ImColor(0, 255, 0),
		ImColor(0, 255, 255),
		ImColor(0, 0, 255),
		ImColor(255, 0, 255),
		ImColor(255, 0, 0) };

	for (int i = 0; i < 6; ++i)
	{
		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING, picker_pos.y + i * (SV_PICKER_SIZE.y / 6)),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING + HUE_PICKER_WIDTH,
				picker_pos.y + (i + 1) * (SV_PICKER_SIZE.y / 6)),
			colors[i],
			colors[i],
			colors[i + 1],
			colors[i + 1]);
	}

	draw_list->AddLine(
		ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING - 2, picker_pos.y + hue * SV_PICKER_SIZE.y),
		ImVec2(picker_pos.x + SV_PICKER_SIZE.x + SPACING + 2 + HUE_PICKER_WIDTH, picker_pos.y + hue * SV_PICKER_SIZE.y),
		ImColor(255, 255, 255));

	// draw alpha bar

	if (alphabar)
	{
		float alpha = col[3];

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * SPACING + HUE_PICKER_WIDTH, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * SPACING + 2 * HUE_PICKER_WIDTH, picker_pos.y + SV_PICKER_SIZE.y),
			ImColor(0, 0, 0), ImColor(0, 0, 0), ImColor(255, 255, 255), ImColor(255, 255, 255));

		draw_list->AddLine(
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * (SPACING - 2) + HUE_PICKER_WIDTH, picker_pos.y + alpha * SV_PICKER_SIZE.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x + 2 * (SPACING + 2) + 2 * HUE_PICKER_WIDTH, picker_pos.y + alpha * SV_PICKER_SIZE.y),
			ImColor(255.f - alpha, 255.f, 255.f));
	}

	// draw color matrix

	{
		const ImU32 c_oColorBlack = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, 1.f));
		const ImU32 c_oColorBlackTransparent = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.f, 0.f, 0.f));
		const ImU32 c_oColorWhite = ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, 1.f));

		ImVec4 cHueValue(1, 1, 1, 1);
		ImGui::ColorConvertHSVtoRGB(hue, 1, 1, cHueValue.x, cHueValue.y, cHueValue.z);
		ImU32 oHueColor = ImGui::ColorConvertFloat4ToU32(cHueValue);

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x, picker_pos.y + SV_PICKER_SIZE.y),
			c_oColorWhite,
			oHueColor,
			oHueColor,
			c_oColorWhite
		);

		draw_list->AddRectFilledMultiColor(
			ImVec2(picker_pos.x, picker_pos.y),
			ImVec2(picker_pos.x + SV_PICKER_SIZE.x, picker_pos.y + SV_PICKER_SIZE.y),
			c_oColorBlackTransparent,
			c_oColorBlackTransparent,
			c_oColorBlack,
			c_oColorBlack
		);
	}

	// draw cross-hair

	float x = saturation * SV_PICKER_SIZE.x;
	float y = (1 - value) * SV_PICKER_SIZE.y;
	ImVec2 p(picker_pos.x + x, picker_pos.y + y);
	draw_list->AddLine(ImVec2(p.x - CROSSHAIR_SIZE, p.y), ImVec2(p.x - 2, p.y), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x + CROSSHAIR_SIZE, p.y), ImVec2(p.x + 2, p.y), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x, p.y + CROSSHAIR_SIZE), ImVec2(p.x, p.y + 2), ImColor(255, 255, 255));
	draw_list->AddLine(ImVec2(p.x, p.y - CROSSHAIR_SIZE), ImVec2(p.x, p.y - 2), ImColor(255, 255, 255));

	// color matrix logic

	ImGui::InvisibleButton(get_localized_string(LANG_0860).c_str(), SV_PICKER_SIZE);

	if (ImGui::IsItemActive() && imgui_io->MouseDown[0])
	{
		ImVec2 mouse_pos_in_canvas = ImVec2(
			imgui_io->MousePos.x - picker_pos.x, imgui_io->MousePos.y - picker_pos.y);

		/**/ if (mouse_pos_in_canvas.x < 0) mouse_pos_in_canvas.x = 0;
		else if (mouse_pos_in_canvas.x >= SV_PICKER_SIZE.x - 1) mouse_pos_in_canvas.x = SV_PICKER_SIZE.x - 1;

		/**/ if (mouse_pos_in_canvas.y < 0) mouse_pos_in_canvas.y = 0;
		else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1) mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

		value = 1 - (mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1));
		saturation = mouse_pos_in_canvas.x / (SV_PICKER_SIZE.x - 1);
		value_changed = true;
	}

	// hue bar logic

	ImGui::SetCursorScreenPos(ImVec2(picker_pos.x + SPACING + SV_PICKER_SIZE.x, picker_pos.y));
	ImGui::InvisibleButton(get_localized_string(LANG_0861).c_str(), ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

	if (imgui_io->MouseDown[0] && (ImGui::IsItemHovered() || ImGui::IsItemActive()))
	{
		ImVec2 mouse_pos_in_canvas = ImVec2(
			imgui_io->MousePos.x - picker_pos.x, imgui_io->MousePos.y - picker_pos.y);

		/**/ if (mouse_pos_in_canvas.y < 0) mouse_pos_in_canvas.y = 0;
		else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1) mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

		hue = mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1);
		value_changed = true;
	}

	// alpha bar logic

	if (alphabar)
	{

		ImGui::SetCursorScreenPos(ImVec2(picker_pos.x + SPACING * 2 + HUE_PICKER_WIDTH + SV_PICKER_SIZE.x, picker_pos.y));
		ImGui::InvisibleButton(get_localized_string(LANG_0862).c_str(), ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

		if (imgui_io->MouseDown[0] && (ImGui::IsItemHovered() || ImGui::IsItemActive()))
		{
			ImVec2 mouse_pos_in_canvas = ImVec2(
				imgui_io->MousePos.x - picker_pos.x, imgui_io->MousePos.y - picker_pos.y);

			/**/ if (mouse_pos_in_canvas.y < 0) mouse_pos_in_canvas.y = 0;
			else if (mouse_pos_in_canvas.y >= SV_PICKER_SIZE.y - 1) mouse_pos_in_canvas.y = SV_PICKER_SIZE.y - 1;

			float alpha = mouse_pos_in_canvas.y / (SV_PICKER_SIZE.y - 1);
			col[3] = alpha;
			value_changed = true;
		}

	}

	// R,G,B or H,S,V color editor

	color = ImColor::HSV(hue >= 1.f ? hue - 10.f * (float)1e-6 : hue, saturation > 0.f ? saturation : 10.f * (float)1e-6, value > 0.f ? value : (float)1e-6);
	col[0] = color.Value.x;
	col[1] = color.Value.y;
	col[2] = color.Value.z;

	bool widget_used;
	ImGui::PushItemWidth((alphabar ? SPACING + HUE_PICKER_WIDTH : 0) +
		SV_PICKER_SIZE.x + SPACING + HUE_PICKER_WIDTH - 2 * ImGui::GetStyle().FramePadding.x);
	widget_used = alphabar ? ImGui::ColorEdit4("", col) : ImGui::ColorEdit3("", col);
	ImGui::PopItemWidth();

	// try to cancel hue wrap (after ColorEdit), if any
	{
		float new_hue, new_sat, new_val;
		ImGui::ColorConvertRGBtoHSV(col[0], col[1], col[2], new_hue, new_sat, new_val);
		if (new_hue <= 0 && hue > 0)
		{
			if (new_val <= 0 && value != new_val)
			{
				color = ImColor::HSV(hue, saturation, new_val <= 0 ? value * 0.5f : new_val);
				col[0] = color.Value.x;
				col[1] = color.Value.y;
				col[2] = color.Value.z;
			}
			else
				if (new_sat <= 0)
				{
					color = ImColor::HSV(hue, new_sat <= 0 ? saturation * 0.5f : new_sat, new_val);
					col[0] = color.Value.x;
					col[1] = color.Value.y;
					col[2] = color.Value.z;
				}
		}
	}
	return value_changed || widget_used;
}

bool ColorPicker3(ImGuiIO* imgui_io, float col[3])
{
	return ColorPicker(imgui_io, col, false);
}

bool ColorPicker4(ImGuiIO* imgui_io, float col[4])
{
	return ColorPicker(imgui_io, col, true);
}

int ArrayXYtoId(int w, int x, int y)
{
	return x + (y * w);
}

std::vector<COLOR3> colordata;


int LMapMaxWidth = 512;

void DrawImageAtOneBigLightMap(COLOR3* img, int w, int h, int x, int y)
{
	for (int x1 = 0; x1 < w; x1++)
	{
		for (int y1 = 0; y1 < h; y1++)
		{
			int offset = ArrayXYtoId(w, x1, y1);
			int offset2 = ArrayXYtoId(LMapMaxWidth, x + x1, y + y1);

			while (offset2 >= colordata.size())
			{
				colordata.emplace_back(COLOR3(0, 0, 255));
			}
			colordata[offset2] = img[offset];
		}
	}
}

void DrawOneBigLightMapAtImage(COLOR3* img, int w, int h, int x, int y)
{
	for (int x1 = 0; x1 < w; x1++)
	{
		for (int y1 = 0; y1 < h; y1++)
		{
			int offset = ArrayXYtoId(w, x1, y1);
			int offset2 = ArrayXYtoId(LMapMaxWidth, x + x1, y + y1);

			img[offset] = colordata[offset2];
		}
	}
}

std::vector<int> faces_to_export;

void ImportOneBigLightmapFile(Bsp* map)
{
	if (!faces_to_export.size())
	{
		print_log(get_localized_string(LANG_0405), map->faceCount);
		for (int faceIdx = 0; faceIdx < map->faceCount; faceIdx++)
		{
			faces_to_export.push_back(faceIdx);
		}
	}

	for (int lightId = 0; lightId < MAXLIGHTMAPS; lightId++)
	{
		colordata = std::vector<COLOR3>();
		int current_x = 0;
		int current_y = 0;
		int max_y_found = 0;
		//print_log(get_localized_string(LANG_0406),lightId);
		std::string filename = fmt::format(fmt::runtime(get_localized_string(LANG_0407)), g_working_dir.c_str(), get_localized_string(LANG_0408), lightId);
		unsigned char* image_bytes;
		unsigned int w2, h2;
		auto error = lodepng_decode24_file(&image_bytes, &w2, &h2, filename.c_str());

		if (error == 0 && image_bytes)
		{
			/*for (int i = 0; i < 100; i++)
			{
				print_log("{}/", image_bytes[i]);
			}*/
			colordata.clear();
			colordata.resize(w2 * h2);
			memcpy(&colordata[0], image_bytes, w2 * h2 * sizeof(COLOR3));
			free(image_bytes);
			for (int faceIdx : faces_to_export)
			{
				int size[2];
				GetFaceLightmapSize(map, faceIdx, size);

				int sizeX = size[0], sizeY = size[1];

				if (map->faces[faceIdx].nLightmapOffset < 0 || map->faces[faceIdx].nStyles[lightId] == 255)
					continue;

				int lightmapSz = sizeX * sizeY * sizeof(COLOR3);

				int offset = map->faces[faceIdx].nLightmapOffset + lightId * lightmapSz;

				if (sizeY > max_y_found)
					max_y_found = sizeY;

				if (current_x + sizeX + 1 > LMapMaxWidth)
				{
					current_y += max_y_found + 1;
					max_y_found = sizeY;
					current_x = 0;
				}

				unsigned char* lightmapData = new unsigned char[lightmapSz];

				DrawOneBigLightMapAtImage((COLOR3*)(lightmapData), sizeX, sizeY, current_x, current_y);
				memcpy((unsigned char*)(map->lightdata + offset), lightmapData, lightmapSz);

				delete[] lightmapData;

				current_x += sizeX + 1;
			}
		}
	}
}

float RandomFloat(float a, float b)
{
	float random = ((float)rand()) / (float)RAND_MAX;
	float diff = b - a;
	float r = random * diff;
	return a + r;
}

std::map<float, float> mapx;
std::map<float, float> mapy;
std::map<float, float> mapz;

void Gui::ExportOneBigLightmap(Bsp* map)
{
	std::string filename;

	faces_to_export.clear();

	if (app->pickInfo.selectedFaces.size() > 1)
	{
		print_log(get_localized_string(LANG_0409), (unsigned int)app->pickInfo.selectedFaces.size());
		faces_to_export = app->pickInfo.selectedFaces;
	}
	else
	{
		print_log(get_localized_string(LANG_0410), map->faceCount);
		for (int faceIdx = 0; faceIdx < map->faceCount; faceIdx++)
		{
			faces_to_export.push_back(faceIdx);
		}
	}

	/*std::vector<vec3> verts;
	for (int i = 0; i < map->vertCount; i++)
	{
		verts.push_back(map->verts[i]);
	}
	std::reverse(verts.begin(), verts.end());
	for (int i = 0; i < map->vertCount; i++)
	{
		map->verts[i] = verts[i];
	}*/
	/*for (int i = 0; i < map->vertCount; i++)
	{
		vec3* vector = &map->verts[i];
		vector->y *= -1;
		vector->x *= -1;
		/*if (mapz.find(vector->z) == mapz.end())
			mapz[vector->z] = RandomFloat(-100, 100);
		vector->z -= mapz[vector->z];*/

		/*if (mapx.find(vector->x) == mapx.end())
			mapx[vector->x] = RandomFloat(-50, 50);
		vector->x += mapx[vector->x];

		if (mapy.find(vector->y) == mapy.end())
			mapy[vector->y] = RandomFloat(-50, 50);
		vector->y -= mapy[vector->y];


		/*vector->x *= static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
		vector->y *= static_cast <float> (rand()) / static_cast <float> (RAND_MAX);*/
		/* }

		map->update_lump_pointers();*/


	for (int lightId = 0; lightId < MAXLIGHTMAPS; lightId++)
	{
		colordata = std::vector<COLOR3>();
		int current_x = 0;
		int current_y = 0;
		int max_y_found = 0;

		bool found_any_lightmap = false;

		//print_log(get_localized_string(LANG_0411),lightId);
		for (int faceIdx : faces_to_export)
		{
			int size[2];
			GetFaceLightmapSize(map, faceIdx, size);

			int sizeX = size[0], sizeY = size[1];

			if (map->faces[faceIdx].nLightmapOffset < 0 || map->faces[faceIdx].nStyles[lightId] == 255)
				continue;

			int lightmapSz = sizeX * sizeY * sizeof(COLOR3);

			int offset = map->faces[faceIdx].nLightmapOffset + lightId * lightmapSz;

			if (sizeY > max_y_found)
				max_y_found = sizeY;

			if (current_x + sizeX + 1 > LMapMaxWidth)
			{
				current_y += max_y_found + 1;
				max_y_found = sizeY;
				current_x = 0;
			}

			DrawImageAtOneBigLightMap((COLOR3*)(map->lightdata + offset), sizeX, sizeY, current_x, current_y);

			current_x += sizeX + 1;

			found_any_lightmap = true;
		}

		if (found_any_lightmap)
		{
			filename = fmt::format(fmt::runtime(get_localized_string(LANG_1061)), g_working_dir.c_str(), get_localized_string(LANG_1062), lightId);
			print_log(get_localized_string(LANG_0412), filename);
			lodepng_encode24_file(filename.c_str(), (const unsigned char*)colordata.data(), LMapMaxWidth, current_y + max_y_found);
		}
	}

}

void ExportLightmap(BSPFACE32 face, int faceIdx, Bsp* map)
{
	int size[2];
	GetFaceLightmapSize(map, faceIdx, size);
	std::string filename;

	for (int i = 0; i < MAXLIGHTMAPS; i++)
	{
		if (face.nStyles[i] == 255)
			continue;
		int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
		int offset = face.nLightmapOffset + i * lightmapSz;
		filename = fmt::format(fmt::runtime(get_localized_string(LANG_0413)), g_working_dir.c_str(), get_localized_string(LANG_0408), faceIdx, i);
		print_log(get_localized_string(LANG_0414), filename);
		lodepng_encode24_file(filename.c_str(), (unsigned char*)(map->lightdata + offset), size[0], size[1]);
	}
}

void ImportLightmap(BSPFACE32 face, int faceIdx, Bsp* map)
{
	std::string filename;
	int size[2];
	GetFaceLightmapSize(map, faceIdx, size);
	for (int i = 0; i < MAXLIGHTMAPS; i++)
	{
		if (face.nStyles[i] == 255)
			continue;
		int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
		int offset = face.nLightmapOffset + i * lightmapSz;
		filename = fmt::format(fmt::runtime(get_localized_string(LANG_1063)), g_working_dir.c_str(), get_localized_string(LANG_1062), faceIdx, i);
		unsigned int w = size[0], h = size[1];
		unsigned int w2 = 0, h2 = 0;
		print_log(get_localized_string(LANG_0415), filename);
		unsigned char* image_bytes = NULL;
		auto error = lodepng_decode24_file(&image_bytes, &w2, &h2, filename.c_str());
		if (error == 0 && image_bytes)
		{
			if (w == w2 && h == h2)
			{
				memcpy((unsigned char*)(map->lightdata + offset), image_bytes, lightmapSz);
			}
			else
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0416), w, h);
			}
			free(image_bytes);
		}
		else
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0417));
		}
	}
}

void Gui::drawLightMapTool()
{
	static float colourPatch[3];
	static Texture* currentlightMap[MAXLIGHTMAPS] = { NULL };
	static float windowWidth = 570;
	static float windowHeight = 600;
	static int lightmaps = 0;
	static bool needPickColor = false;
	const char* light_names[] =
	{
		"OFF",
		"Main light",
		"Light 1",
		"Light 2",
		"Light 3"
	};
	static int type = 0;

	ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, windowHeight), ImVec2(windowWidth, windowHeight));

	const char* lightToolTitle = "LightMap Editor";


	if (ImGui::Begin(lightToolTitle, &showLightmapEditorWidget))
	{
		if (needPickColor)
		{
			ImGui::TextDisabled(get_localized_string(LANG_0863).c_str());
		}
		Bsp* map = app->getSelectedMap();
		if (map && app->pickInfo.selectedFaces.size())
		{
			int faceIdx = app->pickInfo.selectedFaces[0];
			BSPFACE32& face = map->faces[faceIdx];
			int size[2];
			GetFaceLightmapSize(map, faceIdx, size);
			if (showLightmapEditorUpdate)
			{
				lightmaps = 0;
				{
					for (int i = 0; i < MAXLIGHTMAPS; i++)
					{
						if (currentlightMap[i])
							delete currentlightMap[i];
						currentlightMap[i] = NULL;
					}
					for (int i = 0; i < MAXLIGHTMAPS; i++)
					{
						if (face.nStyles[i] == 255)
							continue;
						currentlightMap[i] = new Texture(size[0], size[1], "LIGHTMAP");
						int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
						int offset = face.nLightmapOffset + i * lightmapSz;
						memcpy(currentlightMap[i]->data, map->lightdata + offset, lightmapSz);
						currentlightMap[i]->upload(GL_RGB, true);
						lightmaps++;
						//print_log(get_localized_string(LANG_0418),i,offset);
					}
				}

				windowWidth = lightmaps > 1 ? 550.f : 250.f;
				showLightmapEditorUpdate = false;
			}
			ImVec2 imgSize = ImVec2(200, 200);
			for (int i = 0; i < lightmaps; i++)
			{
				if (i == 0)
				{
					ImGui::Separator();
					ImGui::Dummy(ImVec2(50, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[1]);
					ImGui::SameLine();
					ImGui::Dummy(ImVec2(120, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[2]);
				}

				if (i == 2)
				{
					ImGui::Separator();
					ImGui::Dummy(ImVec2(50, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[3]);
					ImGui::SameLine();
					ImGui::Dummy(ImVec2(150, 5.0f));
					ImGui::SameLine();
					ImGui::TextDisabled(light_names[4]);
				}

				if (i == 1 || i > 2)
				{
					ImGui::SameLine();
				}
				else if (i == 2)
				{
					ImGui::Separator();
				}

				if (!currentlightMap[i])
				{
					ImGui::Dummy(ImVec2(200, 200));
					continue;
				}

				if (ImGui::ImageButton((std::to_string(i) + "_lightmap").c_str(), (ImTextureID)(long long)currentlightMap[i]->id, imgSize, ImVec2(0, 0), ImVec2(1, 1)))
				{
					float itemwidth = ImGui::GetItemRectMax().x - ImGui::GetItemRectMin().x;
					float itemheight = ImGui::GetItemRectMax().y - ImGui::GetItemRectMin().y;

					float mousex = ImGui::GetItemRectMax().x - ImGui::GetMousePos().x;
					float mousey = ImGui::GetItemRectMax().y - ImGui::GetMousePos().y;

					int imagex = (int)round((currentlightMap[i]->width - ((currentlightMap[i]->width / itemwidth) * mousex)) - 0.5f);
					int imagey = (int)round((currentlightMap[i]->height - ((currentlightMap[i]->height / itemheight) * mousey)) - 0.5f);

					if (imagex < 0)
					{
						imagex = 0;
					}
					if (imagey < 0)
					{
						imagey = 0;
					}
					if (imagex > currentlightMap[i]->width)
					{
						imagex = currentlightMap[i]->width;
					}
					if (imagey > currentlightMap[i]->height)
					{
						imagey = currentlightMap[i]->height;
					}

					int offset = ArrayXYtoId(currentlightMap[i]->width, imagex, imagey);
					if (offset >= currentlightMap[i]->width * currentlightMap[i]->height * sizeof(COLOR3))
						offset = (currentlightMap[i]->width * currentlightMap[i]->height * sizeof(COLOR3)) - 1;
					if (offset < 0)
						offset = 0;

					COLOR3* lighdata = (COLOR3*)currentlightMap[i]->data;

					if (needPickColor)
					{
						colourPatch[0] = lighdata[offset].r / 255.f;
						colourPatch[1] = lighdata[offset].g / 255.f;
						colourPatch[2] = lighdata[offset].b / 255.f;
						needPickColor = false;
					}
					else
					{
						lighdata[offset] = COLOR3((unsigned char)(colourPatch[0] * 255.f),
							(unsigned char)(colourPatch[1] * 255.f), (unsigned char)(colourPatch[2] * 255.f));
						currentlightMap[i]->upload(GL_RGB, true);
					}
				}
			}
			ImGui::Separator();
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0419)), size[0], size[1]).c_str());
			ImGui::Separator();
			ColorPicker3(imgui_io, colourPatch);
			ImGui::SetNextItemWidth(100.f);
			if (ImGui::Button(get_localized_string(LANG_0864).c_str(), ImVec2(120, 0)))
			{
				needPickColor = true;
			}
			ImGui::Separator();
			ImGui::SetNextItemWidth(100.f);
			ImGui::Combo(get_localized_string(LANG_0865).c_str(), &type, light_names, IM_ARRAYSIZE(light_names));
			map->getBspRender()->showLightFlag = type - 1;
			ImGui::Separator();
			if (ImGui::Button(get_localized_string(LANG_1126).c_str(), ImVec2(120, 0)))
			{
				for (int i = 0; i < MAXLIGHTMAPS; i++)
				{
					if (face.nStyles[i] == 255 || !currentlightMap[i])
						continue;
					int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
					int offset = face.nLightmapOffset + i * lightmapSz;
					memcpy(map->lightdata + offset, currentlightMap[i]->data, lightmapSz);
				}
				map->getBspRender()->reloadLightmaps();
			}
			ImGui::SameLine();

			if (ImGui::Button(get_localized_string(LANG_1127).c_str(), ImVec2(120, 0)))
			{
				showLightmapEditorUpdate = true;
			}

			ImGui::Separator();
			if (ImGui::Button(get_localized_string(LANG_1128).c_str(), ImVec2(120, 0)))
			{
				print_log(get_localized_string(LANG_0420));
				createDir(g_working_dir);
				ExportLightmap(face, faceIdx, map);
			}
			ImGui::SameLine();
			if (ImGui::Button(get_localized_string(LANG_1129).c_str(), ImVec2(120, 0)))
			{
				print_log(get_localized_string(LANG_0421));
				ImportLightmap(face, faceIdx, map);
				showLightmapEditorUpdate = true;
				map->getBspRender()->reloadLightmaps();
			}
			ImGui::Separator();

			ImGui::Text(get_localized_string(LANG_0866).c_str());
			ImGui::Separator();
			if (ImGui::Button(get_localized_string(LANG_0867).c_str(), ImVec2(125, 0)))
			{
				print_log(get_localized_string(LANG_1064));
				createDir(g_working_dir);

				//for (int z = 0; z < map->faceCount; z++)
				//{
				//	lightmaps = 0;
				//	ExportLightmaps(map->faces[z], z, map);
				//}

				ExportOneBigLightmap(map);
			}
			ImGui::SameLine();
			if (ImGui::Button(get_localized_string(LANG_0868).c_str(), ImVec2(125, 0)))
			{
				print_log(get_localized_string(LANG_1065));

				//for (int z = 0; z < map->faceCount; z++)
				//{
				//	lightmaps = 0;
				//	ImportLightmaps(map->faces[z], z, map);
				//}

				ImportOneBigLightmapFile(map);
				map->getBspRender()->reloadLightmaps();
			}
		}
		else
		{
			ImGui::Text(get_localized_string(LANG_0869).c_str());
		}
	}
	ImGui::End();
}
void Gui::drawFaceEditorWidget()
{
	ImGui::SetNextWindowSize(ImVec2(300.f, 570.f), ImGuiCond_FirstUseEver);
	//ImGui::SetNextWindowSize(ImVec2(400, 600));
	if (ImGui::Begin(get_localized_string(LANG_0870).c_str(), &showFaceEditWidget))
	{
		static float scaleX, scaleY, shiftX, shiftY;
		static int lmSize[2];
		static float rotateX, rotateY;
		static bool lockRotate = true;
		static int bestplane;
		static bool isSpecial;
		static float width, height;
		static std::vector<vec3> edgeVerts;
		static ImTextureID textureId = NULL; // OpenGL ID
		static char textureName[MAXTEXTURENAME];
		static char textureName2[MAXTEXTURENAME];
		static int lastPickCount = -1;
		static bool validTexture = true;
		static bool scaledX = false;
		static bool scaledY = false;
		static bool shiftedX = false;
		static bool shiftedY = false;
		static bool textureChanged = false;
		static bool toggledFlags = false;
		static bool updatedTexVec = false;
		static bool updatedFaceVec = false;
		static bool mergeFaceVec = false;

		unsigned int targetLumps = EDIT_MODEL_LUMPS;

		const char* targetEditName = "Edit face";

		static float verts_merge_epsilon = 1.0f;

		static int tmpStyles[4] = { 255,255,255,255 };
		static bool stylesChanged = false;

		Bsp* map = app->getSelectedMap();
		if (!map || app->pickMode != PICK_FACE || app->pickInfo.selectedFaces.empty())
		{
			ImGui::Text(get_localized_string(LANG_1130).c_str());
			ImGui::End();
			return;
		}
		BspRenderer* mapRenderer = map->getBspRender();
		if (!mapRenderer || !mapRenderer->texturesLoaded)
		{
			ImGui::Text(get_localized_string(LANG_0871).c_str());
			ImGui::End();
			return;
		}

		if (lastPickCount != pickCount && app->pickMode == PICK_FACE)
		{
			edgeVerts.clear();
			if (app->pickInfo.selectedFaces.size())
			{
				int faceIdx = app->pickInfo.selectedFaces[0];
				if (faceIdx >= 0)
				{
					BSPFACE32& face = map->faces[faceIdx];
					BSPPLANE& plane = map->planes[face.iPlane];
					BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
					width = height = 0;

					if (texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount)
					{
						int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
						if (texOffset >= 0)
						{
							BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
							width = tex.nWidth * 1.0f;
							height = tex.nHeight * 1.0f;
							memcpy(textureName, tex.szName, MAXTEXTURENAME);
						}
						else
						{
							textureName[0] = '\0';
						}
					}
					else
					{
						textureName[0] = '\0';
					}

					int miptex = texinfo.iMiptex;

					vec3 xv, yv;
					bestplane = TextureAxisFromPlane(plane, xv, yv);

					rotateX = AngleFromTextureAxis(texinfo.vS, true, bestplane);
					rotateY = AngleFromTextureAxis(texinfo.vT, false, bestplane);

					scaleX = 1.0f / texinfo.vS.length();
					scaleY = 1.0f / texinfo.vT.length();

					shiftX = texinfo.shiftS;
					shiftY = texinfo.shiftT;

					isSpecial = texinfo.nFlags & TEX_SPECIAL;

					textureId = (void*)(uint64_t)mapRenderer->getFaceTextureId(faceIdx);
					validTexture = true;

					for (int i = 0; i < MAXLIGHTMAPS; i++)
					{
						tmpStyles[i] = face.nStyles[i];
					}

					// show default values if not all faces share the same values
					for (int i = 1; i < app->pickInfo.selectedFaces.size(); i++)
					{
						int faceIdx2 = app->pickInfo.selectedFaces[i];
						BSPFACE32& face2 = map->faces[faceIdx2];
						BSPTEXTUREINFO& texinfo2 = map->texinfos[face2.iTextureInfo];

						if (scaleX != 1.0f / texinfo2.vS.length()) scaleX = 1.0f;
						if (scaleY != 1.0f / texinfo2.vT.length()) scaleY = 1.0f;

						if (shiftX != texinfo2.shiftS) shiftX = 0;
						if (shiftY != texinfo2.shiftT) shiftY = 0;

						if (isSpecial != (texinfo2.nFlags & TEX_SPECIAL)) isSpecial = false;
						if (texinfo2.iMiptex != miptex)
						{
							validTexture = false;
							textureId = NULL;
							width = 0.f;
							height = 0.f;
							textureName[0] = '\0';
						}
					}

					GetFaceLightmapSize(map, faceIdx, lmSize);

					for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
					{
						int edgeIdx = map->surfedges[e];
						BSPEDGE32 edge = map->edges[abs(edgeIdx)];
						vec3 v = edgeIdx >= 0 ? map->verts[edge.iVertex[1]] : map->verts[edge.iVertex[0]];
						edgeVerts.push_back(v);
					}
				}
			}
			else
			{
				scaleX = scaleY = shiftX = shiftY = width = height = 0.f;
				textureId = NULL;
				textureName[0] = '\0';
			}

			checkFaceErrors();
		}
		lastPickCount = pickCount;

		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.5f;


		ImGui::PushItemWidth(inputWidth);

		if (app->pickInfo.selectedFaces.size() == 1)
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0422)), lmSize[0], lmSize[1], lmSize[0] * lmSize[1]).c_str());


		ImGui::Text(get_localized_string(LANG_1169).c_str());

		ImGui::SameLine();
		ImGui::Text(get_localized_string(LANG_1170).c_str());
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(get_localized_string(LANG_0872).c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat(get_localized_string(LANG_0873).c_str(), &scaleX, 0.001f, 0, 0, "X: %.3f") && scaleX != 0)
		{
			scaledX = true;
		}
		ImGui::SameLine();
		if (ImGui::DragFloat(get_localized_string(LANG_0874).c_str(), &scaleY, 0.001f, 0, 0, "Y: %.3f") && scaleY != 0)
		{
			scaledY = true;
		}

		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text(get_localized_string(LANG_0875).c_str());

		ImGui::SameLine();
		ImGui::Text(get_localized_string(LANG_1179).c_str());
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(get_localized_string(LANG_0876).c_str());
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat(get_localized_string(LANG_0877).c_str(), &shiftX, 0.1f, 0, 0, "X: %.3f"))
		{
			shiftedX = true;
		}
		ImGui::SameLine();
		if (ImGui::DragFloat(get_localized_string(LANG_0878).c_str(), &shiftY, 0.1f, 0, 0, "Y: %.3f"))
		{
			shiftedY = true;
		}

		ImGui::PopItemWidth();

		inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.3f;
		ImGui::PushItemWidth(inputWidth);

		ImGui::Text(get_localized_string(LANG_0879).c_str());
		ImGui::SameLine();
		ImGui::TextDisabled(get_localized_string(LANG_0880).c_str());

		if (ImGui::DragFloat(get_localized_string(LANG_0881).c_str(), &rotateX, 0.01f, 0, 0, "X: %.3f"))
		{
			updatedTexVec = true;
			if (rotateX > 360.0f)
				rotateX = 360.0f;
			if (rotateX < -360.0f)
				rotateX = -360.0f;
			if (lockRotate)
				rotateY = rotateX - 180.0f;
		}

		ImGui::SameLine();

		if (ImGui::DragFloat(get_localized_string(LANG_0882).c_str(), &rotateY, 0.01f, 0, 0, "Y: %.3f"))
		{
			updatedTexVec = true;
			if (rotateY > 360.0f)
				rotateY = 360.0f;
			if (rotateY < -360.0f)
				rotateY = -360.0f;
			if (lockRotate)
				rotateX = rotateY + 180.0f;
		}

		ImGui::SameLine();

		ImGui::Checkbox(get_localized_string(LANG_0883).c_str(), &lockRotate);

		if (app->pickInfo.selectedFaces.size() == 1)
		{
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0884).c_str());
			if (ImGui::DragInt("# 1:", &tmpStyles[0], 1, 0, 255)) stylesChanged = true;
			ImGui::SameLine();
			if (ImGui::DragInt("# 2:", &tmpStyles[1], 1, 0, 255)) stylesChanged = true;
			if (ImGui::DragInt("# 3:", &tmpStyles[2], 1, 0, 255)) stylesChanged = true;
			ImGui::SameLine();
			if (ImGui::DragInt("# 4:", &tmpStyles[3], 1, 0, 255)) stylesChanged = true;
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0885).c_str());
			ImGui::SameLine();
			ImGui::TextDisabled(get_localized_string(LANG_0886).c_str());

			std::string tmplabel = "##unklabel";

			BSPFACE32 face = map->faces[app->pickInfo.selectedFaces[0]];
			int edgeIdx = 0;
			for (auto& v : edgeVerts)
			{
				edgeIdx++;
				tmplabel = fmt::format(fmt::runtime(get_localized_string(LANG_0423)), edgeIdx);
				if (ImGui::DragFloat(tmplabel.c_str(), &v.x, 0.1f, 0, 0, "T1: %.3f"))
				{
					updatedFaceVec = true;
				}

				tmplabel = fmt::format(fmt::runtime(get_localized_string(LANG_0424)), edgeIdx);
				ImGui::SameLine();
				if (ImGui::DragFloat(tmplabel.c_str(), &v.y, 0.1f, 0, 0, "T2: %.3f"))
				{
					updatedFaceVec = true;
				}

				tmplabel = fmt::format(fmt::runtime(get_localized_string(LANG_0425)), edgeIdx);
				ImGui::SameLine();
				if (ImGui::DragFloat(tmplabel.c_str(), &v.z, 0.1f, 0, 0, "T3: %.3f"))
				{
					updatedFaceVec = true;
				}
			}
		}

		if (app->pickInfo.selectedFaces.size() > 1)
		{
			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0887).c_str());
			ImGui::DragFloat(get_localized_string(LANG_0888).c_str(), &verts_merge_epsilon, 0.1f, 0.0f, 1000.0f);
			if (ImGui::Button(get_localized_string(LANG_0889).c_str()))
			{
				for (auto faceIdx : app->pickInfo.selectedFaces)
				{
					vec3 lastvec = vec3();
					BSPFACE32& face = map->faces[faceIdx];
					for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
					{
						int edgeIdx = map->surfedges[e];
						BSPEDGE32 edge = map->edges[abs(edgeIdx)];

						vec3& vec = edgeIdx >= 0 ? map->verts[edge.iVertex[1]] : map->verts[edge.iVertex[0]];

						for (int v = 0; v < map->vertCount; v++)
						{
							if (map->verts[v].z == vec.z && VectorCompare(map->verts[v], vec, verts_merge_epsilon))
							{
								if (vec != lastvec)
								{
									vec = map->verts[v];
									lastvec = vec;
									break;
								}
							}
						}
					}
				}
				mergeFaceVec = true;
			}
			ImGui::Separator();
		}

		ImGui::PopItemWidth();


		ImGui::Text(get_localized_string(LANG_1131).c_str());
		if (ImGui::Checkbox(get_localized_string(LANG_0890).c_str(), &isSpecial))
		{
			toggledFlags = true;
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Used with invisible faces to bypass the surface extent limit."
				"\nLightmaps may break in strange ways if this is used on a normal face.");
			ImGui::EndTooltip();
		}

		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text(get_localized_string(LANG_0891).c_str());
		ImGui::SetNextItemWidth(inputWidth);
		if (!validTexture)
		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		}

		if (ImGui::InputText(get_localized_string(LANG_0892).c_str(), textureName, MAXTEXTURENAME))
		{
			if (strcasecmp(textureName, textureName2) != 0)
			{
				textureChanged = true;
			}
			memcpy(textureName2, textureName, MAXTEXTURENAME);
		}

		if (refreshSelectedFaces)
		{
			textureChanged = true;
			refreshSelectedFaces = false;
			int texOffset = ((int*)map->textures)[copiedMiptex + 1];
			if (texOffset >= 0)
			{
				BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
				memcpy(textureName, tex.szName, MAXTEXTURENAME);
				textureName[15] = '\0';
			}
			else
			{
				textureName[0] = '\0';
			}
		}
		if (!validTexture)
		{
			ImGui::PopStyleColor();
		}
		ImGui::SameLine();
		ImGui::Text(get_localized_string(LANG_0893).c_str(), width, height);
		if (!ImGui::IsMouseDown(ImGuiMouseButton_::ImGuiMouseButton_Left) &&
			(updatedFaceVec || scaledX || scaledY || shiftedX || shiftedY || textureChanged || stylesChanged
				|| refreshSelectedFaces || toggledFlags || updatedTexVec || mergeFaceVec))
		{
			unsigned int newMiptex = 0;
			pickCount++;
			if (textureChanged)
			{
				validTexture = false;

				for (int i = 0; i < map->textureCount; i++)
				{
					int texOffset = ((int*)map->textures)[i + 1];
					if (texOffset >= 0)
					{
						BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
						if (strcasecmp(tex.szName, textureName) == 0)
						{
							validTexture = true;
							newMiptex = i;
							break;
						}
					}
				}
				if (!validTexture)
				{
					for (auto& s : mapRenderer->wads)
					{
						if (s->hasTexture(textureName))
						{
							WADTEX* wadTex = s->readTexture(textureName);
							COLOR3* imageData = ConvertWadTexToRGB(wadTex);

							validTexture = true;
							newMiptex = map->add_texture(textureName, (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight);

							mapRenderer->loadTextures();
							mapRenderer->reuploadTextures();

							delete[] imageData;
							delete wadTex;
						}
					}
				}
			}

			std::set<int> modelRefreshes;
			for (int i = 0; i < app->pickInfo.selectedFaces.size(); i++)
			{
				int faceIdx = app->pickInfo.selectedFaces[i];
				if (faceIdx < 0)
					continue;

				BSPFACE32& face = map->faces[faceIdx];
				BSPTEXTUREINFO* texinfo = map->get_unique_texinfo(faceIdx);

				if (shiftedX)
				{
					texinfo->shiftS = shiftX;
				}
				if (shiftedY)
				{
					texinfo->shiftT = shiftY;
				}

				if (updatedTexVec)
				{
					texinfo->vS = AxisFromTextureAngle(rotateX, true, bestplane);
					texinfo->vT = AxisFromTextureAngle(rotateY, false, bestplane);
					texinfo->vS = texinfo->vS.normalize(1.0f / scaleX);
					texinfo->vT = texinfo->vT.normalize(1.0f / scaleY);
				}

				if (stylesChanged)
				{
					for (int n = 0; n < MAXLIGHTMAPS; n++)
					{
						face.nStyles[n] = (unsigned char)tmpStyles[n];
					}
				}

				if (scaledX)
				{
					texinfo->vS = texinfo->vS.normalize(1.0f / scaleX);
				}
				if (scaledY)
				{
					texinfo->vT = texinfo->vT.normalize(1.0f / scaleY);
				}

				if (toggledFlags)
				{
					texinfo->nFlags = isSpecial ? TEX_SPECIAL : 0;
				}

				if ((textureChanged || toggledFlags || updatedFaceVec || stylesChanged) && validTexture)
				{
					int modelIdx = map->get_model_from_face(faceIdx);
					if (textureChanged)
						texinfo->iMiptex = newMiptex;
					if (modelIdx >= 0 && !modelRefreshes.count(modelIdx))
						modelRefreshes.insert(modelIdx);
				}

				mapRenderer->updateFaceUVs(faceIdx);
			}

			if (updatedFaceVec && app->pickInfo.selectedFaces.size() == 1)
			{
				int faceIdx = app->pickInfo.selectedFaces[0];
				int vecId = 0;
				for (int e = map->faces[faceIdx].iFirstEdge; e < map->faces[faceIdx].iFirstEdge + map->faces[faceIdx].nEdges; e++, vecId++)
				{
					int edgeIdx = map->surfedges[e];
					BSPEDGE32 edge = map->edges[abs(edgeIdx)];
					vec3& v = edgeIdx >= 0 ? map->verts[edge.iVertex[1]] : map->verts[edge.iVertex[0]];
					v = edgeVerts[vecId];
				}
			}

			if ((textureChanged || toggledFlags || updatedFaceVec || stylesChanged) && app->pickInfo.selectedFaces.size() && app->pickInfo.selectedFaces[0] >= 0)
			{
				textureId = (void*)(uint64_t)mapRenderer->getFaceTextureId(app->pickInfo.selectedFaces[0]);
				for (auto it = modelRefreshes.begin(); it != modelRefreshes.end(); it++)
				{
					mapRenderer->refreshModel(*it);
				}
				for (int i = 0; i < app->pickInfo.selectedFaces.size(); i++)
				{
					mapRenderer->highlightFace(app->pickInfo.selectedFaces[i], true);
				}
			}

			if (mergeFaceVec)
			{
				map->remove_unused_model_structures(CLEAN_VERTICES);
				map->getBspRender()->reload();
			}

			checkFaceErrors();

			if (updatedFaceVec)
			{
				targetLumps = FL_PLANES | FL_TEXTURES | FL_VERTICES | FL_NODES | FL_TEXINFO | FL_FACES | FL_LIGHTING | FL_CLIPNODES | FL_LEAVES | FL_EDGES | FL_SURFEDGES | FL_MODELS;
			}

			mergeFaceVec = updatedFaceVec = scaledX = scaledY = shiftedX = shiftedY =
				textureChanged = toggledFlags = updatedTexVec = stylesChanged = false;

			mapRenderer->updateLightmapInfos();
			mapRenderer->calcFaceMaths();
			app->updateModelVerts();

			reloadLimits();

			map->getBspRender()->pushModelUndoState(targetEditName, targetLumps);
		}

		refreshSelectedFaces = false;

		ImVec2 imgSize = ImVec2(inputWidth * 2 - 2, inputWidth * 2 - 2);
		if (ImGui::ImageButton(textureId, imgSize, ImVec2(0, 0), ImVec2(1, 1), 1))
		{
			showTextureBrowser = true;
		}
	}

	ImGui::End();
}

StatInfo Gui::calcStat(std::string name, unsigned int val, unsigned int max, bool isMem)
{
	StatInfo stat;
	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	ImVec4 color;

	if (val > max)
	{
		color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	}
	else if (percent >= 90)
	{
		color = ImVec4(1.0f, 0.5f, 0.0f, 1.0f);
	}
	else if (percent >= 75)
	{
		color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	}
	else
	{
		color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	std::string tmp;
	//std::string out;

	stat.name = std::move(name);

	if (isMem)
	{
		tmp = fmt::format("{:8.2f}", val / meg);
		stat.val = std::string(tmp);

		tmp = fmt::format("{:>5.2f}", max / meg);
		stat.max = std::string(tmp);
	}
	else
	{
		tmp = fmt::format("{:8}", val);
		stat.val = std::string(tmp);

		tmp = fmt::format("{:>8}", max);
		stat.max = std::string(tmp);
	}
	tmp = fmt::format("{:3.1f}%", percent);
	stat.fullness = std::string(tmp);
	stat.color = color;

	stat.progress = (float)val / (float)max;

	return stat;
}

ModelInfo Gui::calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, unsigned int val, unsigned int max, bool isMem)
{
	ModelInfo stat;

	std::string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	std::string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (int k = 0; k < map->ents.size(); k++)
	{
		if (map->ents[k]->getBspModelIdx() == modelInfo->modelIdx)
		{
			targetname = map->ents[k]->keyvalues["targetname"];
			classname = map->ents[k]->keyvalues["classname"];
			stat.entIdx = k;
		}
	}

	stat.classname = std::move(classname);
	stat.targetname = std::move(targetname);

	std::string tmp;

	const float meg = 1024 * 1024;
	float percent = (val / (float)max) * 100;

	if (isMem)
	{
		tmp = fmt::format("{:8.1f}", val / meg);
		stat.val = std::to_string(val);

		tmp = fmt::format("{:>5.1f}", max / meg);
		stat.usage = tmp;
	}
	else
	{
		stat.model = "*" + std::to_string(modelInfo->modelIdx);
		stat.val = std::to_string(val);
	}
	if (percent >= 0.1f)
	{
		tmp = fmt::format("{:6.1f}%", percent);
		stat.usage = std::string(tmp);
	}

	return stat;
}

void Gui::reloadLimits()
{
	for (int i = 0; i < SORT_MODES; i++)
	{
		loadedLimit[i] = false;
	}
	loadedStats = false;
}

void Gui::checkValidHulls()
{
	for (int i = 0; i < MAX_MAP_HULLS; i++)
	{
		anyHullValid[i] = false;
		for (int k = 0; k < app->mapRenderers.size() && !anyHullValid[i]; k++)
		{
			Bsp* map = app->mapRenderers[k]->map;

			for (int m = 0; m < map->modelCount; m++)
			{
				if (map->models[m].iHeadnodes[i] >= 0)
				{
					anyHullValid[i] = true;
					break;
				}
			}
		}
	}
}

void Gui::checkFaceErrors()
{
	lightmapTooLarge = badSurfaceExtents = false;

	Bsp* map = app->getSelectedMap();
	if (!map)
		return;


	for (int i = 0; i < app->pickInfo.selectedFaces.size(); i++)
	{
		int size[2];
		GetFaceLightmapSize(map, app->pickInfo.selectedFaces[i], size);
		if ((size[0] > MAX_SURFACE_EXTENT) || (size[1] > MAX_SURFACE_EXTENT) || size[0] < 0 || size[1] < 0)
		{
			//print_log(get_localized_string(LANG_0426),size[0],size[1]);
			size[0] = std::min(size[0], MAX_SURFACE_EXTENT);
			size[1] = std::min(size[1], MAX_SURFACE_EXTENT);
			badSurfaceExtents = true;
		}


		if (size[0] * size[1] > MAX_LUXELS)
		{
			lightmapTooLarge = true;
		}
	}
}

void Gui::refresh()
{
	reloadLimits();
	checkValidHulls();
}
