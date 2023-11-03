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
	iniPath = getConfigDir() + "imgui.ini";
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
		//glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
		return (void*)(uint64_t)tex;
		};
	ifd::FileDialog::Instance().DeleteTexture = [](void* tex) {
		GLuint texID = (GLuint)((uintptr_t)tex);
		glDeleteTextures(1, &texID);
		};

	loadFonts();

	imgui_io->ConfigWindowsMoveFromTitleBarOnly = true;

	clearLog();
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

void Gui::draw()
{
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

	if (app->pickMode == PICK_OBJECT)
	{
		if (contextMenuEnt != -1)
		{
			ImGui::OpenPopup("ent_context");
			contextMenuEnt = -1;
		}
		if (emptyContextMenu)
		{
			emptyContextMenu = 0;
			ImGui::OpenPopup("empty_context");
		}
	}
	else
	{
		if (contextMenuEnt != -1 || emptyContextMenu)
		{
			emptyContextMenu = 0;
			contextMenuEnt = -1;
			ImGui::OpenPopup("face_context");
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
		logf("No map selecetd\n");
		return;
	}
	else if (app->pickInfo.selectedFaces.size() == 0 || app->pickInfo.selectedFaces.size() > 1)
	{
		logf("No face selected\n");
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
		logf("No map selecetd\n");
		return;
	}
	else if (app->pickInfo.selectedFaces.size() == 0 || app->pickInfo.selectedFaces.size() > 1)
	{
		logf("No face selected\n");
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
		logf("No map selecetd\n");
		return;
	}
	else if (app->pickInfo.selectedFaces.size() == 0 || app->pickInfo.selectedFaces.size() > 1)
	{
		logf("No face selected\n");
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
		logf("WARNING: lightmap sizes don't match ({}x{} != {}{})",
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
	logf("Save current map to temporary file.\n");
	src_map->update_ent_lump();
	src_map->update_lump_pointers();
	src_map->write(src_map->bsp_path + ".tmp.bsp");

	logf("Load map for model export.\n");

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

	logf("Remove temporary file.\n");
	removeFile(src_map->bsp_path + ".tmp.bsp");

	vec3 modelOrigin = tmpMap->get_model_center(id);

	BSPMODEL tmpModel = src_map->models[id];

	while (tmpMap->modelCount < 2)
	{
		logf("Create missing model.\n");
		tmpMap->create_model();
	}

	tmpMap->models[1] = tmpModel;
	logf("Make first model empty(world bypass).\n");
	tmpMap->models[0] = tmpModel;
	tmpMap->models[0].nVisLeafs = 0;
	tmpMap->models[0].iHeadnodes[0] = tmpMap->models[0].iHeadnodes[1] =
		tmpMap->models[0].iHeadnodes[2] = tmpMap->models[0].iHeadnodes[3] = CONTENTS_EMPTY;

	for (int i = 1; i < tmpMap->ents.size(); i++)
	{
		delete tmpMap->ents[i];
	}
	logf("Add two ents, worldspawn and temporary func_wall.\n");

	Entity* tmpEnt = new Entity("worldspawn");
	Entity* tmpEnt2 = new Entity("func_wall");

	tmpEnt->setOrAddKeyvalue("compiler", g_version_string);
	tmpEnt->setOrAddKeyvalue("message", "bsp model");

	tmpEnt2->setOrAddKeyvalue("model", "*1");

	logf("Save two models, empty worldspawn and target model.\n");
	tmpMap->modelCount = 2;
	tmpMap->lumps[LUMP_MODELS] = (unsigned char*)tmpMap->models;
	tmpMap->bsp_header.lump[LUMP_MODELS].nLength = sizeof(BSPMODEL) * 2;
	tmpMap->ents.clear();

	tmpMap->ents.push_back(tmpEnt);
	tmpMap->ents.push_back(tmpEnt2);

	tmpMap->update_ent_lump();
	tmpMap->update_lump_pointers();

	logf("Remove all unused map data #1.\n");
	STRUCTCOUNT removed = tmpMap->remove_unused_model_structures(CLEAN_LIGHTMAP | CLEAN_PLANES | CLEAN_NODES | CLEAN_CLIPNODES | CLEAN_CLIPNODES_SOMETHING | CLEAN_LEAVES | CLEAN_FACES | CLEAN_SURFEDGES | CLEAN_TEXINFOS |
		CLEAN_EDGES | CLEAN_VERTICES | CLEAN_TEXTURES | CLEAN_VISDATA);
	if (!removed.allZero())
		removed.print_delete_stats(1);

	logf("Copy temporary model to worldspawn.\n");
	tmpMap->modelCount = 1;
	tmpMap->models[0] = tmpMap->models[1];
	tmpMap->lumps[LUMP_MODELS] = (unsigned char*)tmpMap->models;
	tmpMap->bsp_header.lump[LUMP_MODELS].nLength = sizeof(BSPMODEL);

	logf("Remove temporary func_wall.\n");
	tmpMap->ents.clear();
	tmpMap->ents.push_back(tmpEnt);
	tmpMap->update_ent_lump();
	tmpMap->update_lump_pointers();

	logf("Validate model...\n");

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

	logf("Remove unused wad files.\n");
	remove_unused_wad_files(src_map, tmpMap, ExportType);

	logf("Remove all unused map data #2.\n");
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
		removeFile(GetWorkDir() + src_map->bsp_name + "_model" + std::to_string(id) + ".bsp");
		tmpMap->write(GetWorkDir() + src_map->bsp_name + "_model" + std::to_string(id) + ".bsp");
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
		if (ImGui::BeginPopup("ent_context") || ImGui::BeginPopup("empty_context"))
		{
			if (ImGui::MenuItem("Center", ""))
			{
				app->transformedOrigin = app->getEntOrigin(map, ent);
				app->applyTransform(map);
				pickCount++; // force gui refresh
			}

			if (ent && ImGui::BeginMenu("Align"))
			{
				BSPMODEL& model = map->models[ent->getBspModelIdx()];

				if (ImGui::MenuItem("Top"))
				{
					app->transformedOrigin.z = app->oldOrigin.z + model.nMaxs.z;
					app->applyTransform(map);
					pickCount++;
				}
				if (ImGui::MenuItem("Bottom"))
				{
					app->transformedOrigin.z = app->oldOrigin.z + model.nMins.z;
					app->applyTransform(map);
					pickCount++;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Left"))
				{
					app->transformedOrigin.x = app->oldOrigin.x + model.nMins.x;
					app->applyTransform(map);
					pickCount++;
				}
				if (ImGui::MenuItem("Right"))
				{
					app->transformedOrigin.x = app->oldOrigin.x + model.nMaxs.x;
					app->applyTransform(map);
					pickCount++;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Back"))
				{
					app->transformedOrigin.y = app->oldOrigin.y + model.nMins.y;
					app->applyTransform(map);
					pickCount++;
				}
				if (ImGui::MenuItem("Front"))
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
		if (ImGui::BeginPopup("face_context"))
		{
			if (ImGui::MenuItem("Copy texture", "Ctrl+C"))
			{
				copyTexture();
			}

			if (ImGui::MenuItem("Paste texture", "Ctrl+V", false, copiedMiptex >= 0 && copiedMiptex < map->textureCount))
			{
				pasteTexture();
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Copy lightmap", "(WIP)"))
			{
				copyLightmap();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Only works for faces with matching sizes/extents,\nand the lightmap might get shifted.");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Paste lightmap", "", false, copiedLightmapFace >= 0 && copiedLightmapFace < map->faceCount))
			{
				pasteLightmap();
			}

			ImGui::EndPopup();
		}
	}
	else /*if (app->pickMode == PICK_OBJECT)*/
	{
		if (ImGui::BeginPopup("ent_context") && entIdx >= 0)
		{
			Entity* ent = map->ents[entIdx];
			int modelIdx = ent->getBspModelIdx();
			if (modelIdx < 0 && ent->isWorldSpawn())
				modelIdx = 0;

			if (modelIdx != 0 || !app->copiedEnts.empty())
			{
				if (modelIdx != 0)
				{
					if (ImGui::MenuItem("Cut", "Ctrl+X", false, app->pickInfo.selectedEnts.size()))
					{
						app->cutEnt();
					}
					if (ImGui::MenuItem("Copy", "Ctrl+C", false, app->pickInfo.selectedEnts.size()))
					{
						app->copyEnt();
					}
				}

				if (!app->copiedEnts.empty())
				{
					if (ImGui::MenuItem("Paste", "Ctrl+V", false))
					{
						app->pasteEnt(false);
					}
					if (ImGui::MenuItem("Paste at original origin", 0, false))
					{
						app->pasteEnt(true);
					}
				}

				if (modelIdx != 0)
				{
					if (ImGui::MenuItem("Delete", "Del"))
					{
						app->deleteEnts();
					}
				}
			}
			if (map->ents[entIdx]->hide)
			{
				if (ImGui::MenuItem("Unhide", "Ctrl+H"))
				{
					map->ents[entIdx]->hide = false;
					map->getBspRender()->preRenderEnts();
					app->updateEntConnections();
				}
			}
			else if (ImGui::MenuItem("Hide", "Ctrl+H"))
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
					if (ImGui::BeginMenu("Hulls"))
					{
						if (modelIdx > 0 || map->is_bsp_model)
						{
							if (ImGui::BeginMenu("Create Hull", !app->invalidSolid && app->isTransformableSolid))
							{
								if (ImGui::MenuItem("Clipnodes"))
								{
									map->regenerate_clipnodes(modelIdx, -1);
									checkValidHulls();
									logf("Regenerated hulls 1-3 on model {}\n", modelIdx);
								}

								ImGui::Separator();

								for (int i = 1; i < MAX_MAP_HULLS; i++)
								{
									if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str()))
									{
										map->regenerate_clipnodes(modelIdx, i);
										checkValidHulls();
										logf("Regenerated hull {} on model {}\n", i, modelIdx);
									}
								}
								ImGui::EndMenu();
							}

							if (ImGui::BeginMenu("Delete Hull", !app->isLoading))
							{
								if (ImGui::MenuItem("All Hulls"))
								{
									map->delete_hull(0, modelIdx, -1);
									map->delete_hull(1, modelIdx, -1);
									map->delete_hull(2, modelIdx, -1);
									map->delete_hull(3, modelIdx, -1);
									map->getBspRender()->refreshModel(modelIdx);
									checkValidHulls();
									logf("Deleted all hulls on model {}\n", modelIdx);
								}
								if (ImGui::MenuItem("Clipnodes"))
								{
									map->delete_hull(1, modelIdx, -1);
									map->delete_hull(2, modelIdx, -1);
									map->delete_hull(3, modelIdx, -1);
									map->getBspRender()->refreshModelClipnodes(modelIdx);
									checkValidHulls();
									logf("Deleted hulls 1-3 on model {}\n", modelIdx);
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
										logf("Deleted hull {} on model {}\n", i, modelIdx);
									}
								}

								ImGui::EndMenu();
							}

							if (ImGui::BeginMenu("Simplify Hull", !app->isLoading))
							{
								if (ImGui::MenuItem("Clipnodes"))
								{
									map->simplify_model_collision(modelIdx, 1);
									map->simplify_model_collision(modelIdx, 2);
									map->simplify_model_collision(modelIdx, 3);
									map->getBspRender()->refreshModelClipnodes(modelIdx);
									logf("Replaced hulls 1-3 on model {} with a box-shaped hull\n", modelIdx);
								}

								ImGui::Separator();

								for (int i = 1; i < MAX_MAP_HULLS; i++)
								{
									bool isHullValid = map->models[modelIdx].iHeadnodes[i] >= 0;

									if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), 0, false, isHullValid))
									{
										map->simplify_model_collision(modelIdx, 1);
										map->getBspRender()->refreshModelClipnodes(modelIdx);
										logf("Replaced hull {} on model {} with a box-shaped hull\n", i, modelIdx);
									}
								}

								ImGui::EndMenu();
							}

							bool canRedirect = map->models[modelIdx].iHeadnodes[1] != map->models[modelIdx].iHeadnodes[2] || map->models[modelIdx].iHeadnodes[1] != map->models[modelIdx].iHeadnodes[3];

							if (ImGui::BeginMenu("Redirect Hull", canRedirect && !app->isLoading && modelIdx >= 0))
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
												logf("Redirected hull {} to hull {} on model {}\n", i, k, modelIdx);
											}
										}

										ImGui::EndMenu();
									}
								}

								ImGui::EndMenu();
							}
						}
						if (ImGui::BeginMenu("Print Hull Tree", !app->isLoading && modelIdx >= 0))
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

					bool allowDuplicate = modelIdx >= 0 && app->pickInfo.selectedEnts.size() > 0;
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
						if (ImGui::MenuItem("Duplicate BSP model", 0, false, !app->isLoading && allowDuplicate))
						{
							logf("Execute 'duplicate' for {} models.\n", app->pickInfo.selectedEnts.size());
							for (auto& tmpEntIdx : app->pickInfo.selectedEnts)
							{
								DuplicateBspModelCommand* command = new DuplicateBspModelCommand("Duplicate BSP Model", tmpEntIdx);
								command->execute();
								map->getBspRender()->pushUndoCommand(command);
							}
						}
						if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
						{
							ImGui::BeginTooltip();
							ImGui::TextUnformatted("Create a copy of this BSP model and assign to this entity.\n\nThis lets you edit the model for this entity without affecting others.");
							ImGui::EndTooltip();
						}
					}
					if (ImGui::BeginMenu("Export BSP model", !app->isLoading && modelIdx >= 0))
					{
						if (ImGui::BeginMenu("With origin", !app->isLoading && modelIdx >= 0))
						{
							if (ImGui::MenuItem("With WAD", 0, false, !app->isLoading && modelIdx >= 0))
							{
								ExportModel(map, modelIdx, 0, false);
							}
							if (ImGui::MenuItem("With intenal textures[HL1]", 0, false, !app->isLoading && modelIdx >= 0))
							{
								ExportModel(map, modelIdx, 2, false);
							}
							if (ImGui::MenuItem("With intenal textures[QUAKE/HL1+XASH]", 0, false, !app->isLoading && modelIdx >= 0))
							{
								ExportModel(map, modelIdx, 1, false);
							}
							ImGui::EndMenu();
						}

						if (ImGui::BeginMenu("Without origin", !app->isLoading && modelIdx >= 0))
						{
							if (ImGui::MenuItem("With WAD", 0, false, !app->isLoading && modelIdx >= 0))
							{
								ExportModel(map, modelIdx, 0, true);
							}
							if (ImGui::MenuItem("With intenal textures[HL1]", 0, false, !app->isLoading && modelIdx >= 0))
							{
								ExportModel(map, modelIdx, 2, true);
							}
							if (ImGui::MenuItem("With intenal textures[QUAKE/HL1+XASH]", 0, false, !app->isLoading && modelIdx >= 0))
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
						ImGui::TextUnformatted("Create .bsp file with single model. It can be imported to another map.");
						ImGui::EndTooltip();
					}

				}
			}
			if (modelIdx > 0)
			{
				if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "ALT+G"))
				{
					if (!app->movingEnt)
						app->grabEnt();
					else
					{
						app->ungrabEnt();
					}
				}

				if (ImGui::MenuItem("Transform", "Ctrl+M"))
				{
					showTransformWidget = true;
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Properties", "Alt+Enter"))
			{
				showKeyvalueWidget = true;
			}


			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("empty_context"))
		{
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, app->copiedEnts.size()))
			{
				app->pasteEnt(false);
			}
			if (ImGui::MenuItem("Paste at original origin", 0, false, app->copiedEnts.size()))
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
			createDir(GetWorkDir());
			tmpWad->write(GetWorkDir() + map->bsp_name + ".wad", tmpWadTex);
		}
		else
		{
			retval = false;
			logf("Not found any textures in bsp file.");
		}
		tmpWadTex.clear();
		delete tmpWad;
	}
	else
	{
		retval = false;
		logf("No textures for export.\n");
	}
	return retval;
}

void ImportWad(Bsp* map, Renderer* app, std::string path)
{
	Wad* tmpWad = new Wad(path);

	if (!tmpWad->readInfo())
	{
		logf("Reading wad file failed!\n");
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
							logf("Already found in current map!\n");
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

	if (ImGui::BeginMenu("File", map))
	{
		if (ImGui::MenuItem("Save", NULL, false, !app->isLoading))
		{
			map->update_ent_lump();
			map->update_lump_pointers();
			map->write(map->bsp_path);
		}
		if (ImGui::BeginMenu("Save as", !app->isLoading))
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

			if (ImGui::MenuItem("Half Life", NULL, is_default_format))
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
						logf("Can't validate map\n");
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (is_default_format)
				{
					ImGui::TextUnformatted("Map already saved in default BSP30 format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to default BSP30 format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to default BSP30 format.");
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Blue Shift", NULL, old_is_blue_shift))
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
						logf("Can't validate map\n");
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				if (old_is_blue_shift)
				{
					ImGui::TextUnformatted("Map already saved in Blue Shift format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to Blue Shift compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to Blue Shift.");
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Half-Life BSP29[COLOR LIGHT]", NULL, old_is_bsp29 && !old_is_broken_clipnodes && old_is_colored_lightmap))
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
					ImGui::TextUnformatted("Map already saved in BSP29 + COLORED LIGHTMAP format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to BSP29 + COLORED LIGHTMAP compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to BSP29 + COLORED LIGHTMAP.");
				}
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Half-Life BSP29[MONO LIGHT]", NULL, old_is_bsp29 && !old_is_broken_clipnodes && !old_is_colored_lightmap))
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
					ImGui::TextUnformatted("Map already saved in BSP29 + MONOCHROME LIGHTMAP format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to BSP29 + MONOCHROME LIGHTMAP compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to BSP29 + MONOCHROME LIGHTMAP.");
				}
				ImGui::EndTooltip();
			}

			if (old_is_broken_clipnodes)
			{
				if (ImGui::MenuItem("HL BSP29[BROKEN CLIPNODES][COLOR LIGHT]", NULL, old_is_bsp29 && old_is_broken_clipnodes && old_is_colored_lightmap))
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
					if (old_is_bsp29 && !old_is_broken_clipnodes && !old_is_colored_lightmap)
					{
						ImGui::TextUnformatted("Map already saved in BSP29 + BROKEN CLIPNODES + COLOR LIGHT format.");
					}
					else if (map->isValid())
					{
						ImGui::TextUnformatted("Saving map to BSP29 + BROKEN CLIPNODES + COLOR LIGHT compatibility format.");
					}
					else
					{
						ImGui::TextUnformatted("Map limits is reached, and can't be converted to BSP29 + BROKEN CLIPNODES + COLOR LIGH.");
					}
					ImGui::EndTooltip();
				}

				if (ImGui::MenuItem("HL BSP29[BROKEN CLIPNODES][MONO LIGHT]", NULL, old_is_bsp29 && old_is_broken_clipnodes && !old_is_colored_lightmap))
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
					if (old_is_bsp29 && !old_is_broken_clipnodes && !old_is_colored_lightmap)
					{
						ImGui::TextUnformatted("Map already saved in BSP29 + BROKEN CLIPNODES + MONO LIGHT format.");
					}
					else if (map->isValid())
					{
						ImGui::TextUnformatted("Saving map to BSP29 + BROKEN CLIPNODES + MONO LIGHT compatibility format.");
					}
					else
					{
						ImGui::TextUnformatted("Map limits is reached, and can't be converted to BSP29 + BROKEN CLIPNODES + MONO LIGH.");
					}
					ImGui::EndTooltip();
				}

			}

			if (ImGui::MenuItem("HL BSP2[32 bit][COLOR LIGHT]", NULL, old_is_bsp2 && !old_is_bsp2_old && old_is_colored_lightmap))
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



			if (ImGui::MenuItem("HL BSP2[32 bit][MONO LIGHT]", NULL, old_is_bsp2 && !old_is_bsp2_old && !old_is_colored_lightmap))
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


			if (ImGui::MenuItem("HL 2BSP[OLD][32 bit][COLOR LIGHT]", NULL, old_is_bsp2_old && old_is_colored_lightmap))
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
					ImGui::TextUnformatted("Map already saved in 2BSP (29)[OLD] + COLOR LIGHT format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to 2BSP (29)[OLD] + COLOR LIGHT compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to 2BSP (29)[OLD] + COLOR LIGHT.");
				}
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem("HL 2BSP[OLD][32 bit][MONO LIGHT]", NULL, old_is_bsp2_old && !old_is_colored_lightmap))
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
					ImGui::TextUnformatted("Map already saved in 2BSP (29)[OLD] + MONO LIGHT format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to 2BSP (29)[OLD] + MONO LIGHT compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to 2BSP (29)[OLD] + MONO LIGHT.");
				}
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem("XASH BSP30ex[32 bit][COLOR LIGHT]", NULL, old_is_bsp30ext && old_is_colored_lightmap))
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
					ImGui::TextUnformatted("Map already saved in XASH BSP30ex + COLOR LIGHT format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to XASH BSP30ex + COLOR LIGHT compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to XASH BSP30ex + COLOR LIGHT.");
				}
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem("XASH BSP30ex[32 bit][MONO LIGHT]", NULL, old_is_bsp2_old && !old_is_colored_lightmap))
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
					ImGui::TextUnformatted("Map already saved in XASH BSP30ex + MONO LIGHT format.");
				}
				else if (map->isValid())
				{
					ImGui::TextUnformatted("Saving map to XASH BSP30ex + MONO LIGHT compatibility format.");
				}
				else
				{
					ImGui::TextUnformatted("Map limits is reached, and can't be converted to XASH BSP30ex + MONO LIGHT.");
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
		if (ImGui::BeginMenu("Open"))
		{
			if (ImGui::MenuItem("MAP"))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select map path", "Map file (*.bsp){.bsp}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Open map in new Window");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("MDL"))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select model path", "Model file (*.mdl){.mdl}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Open model in new Window");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Wad"))
			{
				filterNeeded = true;
				ifd::FileDialog::Instance().Open("MapOpenDialog", "Select wad path", "Wad file (*.wad){.wad}", false, g_settings.lastdir);
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Add wad file path to current map");
				ImGui::EndTooltip();
			}
			ImGui::EndMenu();
		}

		if (ImGui::MenuItem("Close", NULL, false, !app->isLoading && map))
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
			if (ImGui::MenuItem("Close All", NULL, false, !app->isLoading))
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

		if (ImGui::BeginMenu("Export", !app->isLoading))
		{
			if ((map && !map->is_mdl_model) && ImGui::MenuItem("Entity file", NULL))
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
					entFilePath = GetWorkDir() + (map->bsp_name + ".ent");
					createDir(GetWorkDir());
				}

				logf("Export entities: {}\n", entFilePath);
				std::ofstream entFile(entFilePath, std::ios::trunc);
				map->update_ent_lump();
				if (map->bsp_header.lump[LUMP_ENTITIES].nLength > 0)
				{
					std::string entities = std::string(map->lumps[LUMP_ENTITIES], map->lumps[LUMP_ENTITIES] + map->bsp_header.lump[LUMP_ENTITIES].nLength - 1);
					entFile.write(entities.c_str(), entities.size());
				}
			}
			if (map && ImGui::MenuItem("All embedded textures to wad", NULL))
			{
				logf("Export wad: {}{}\n", GetWorkDir(), map->bsp_name + ".wad");
				if (ExportWad(map))
				{
					logf("Remove all embedded textures\n");
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
			if (ImGui::BeginMenu("Wavefront (.obj) [WIP]"))
			{
				if (ImGui::MenuItem("Scale 1x", NULL))
				{
					if (map)
					{
						map->ExportToObjWIP(GetWorkDir(), EXPORT_XYZ, 1);
					}
					else
					{
						logf("Select map first\n");
					}
				}

				for (int scale = 2; scale < 10; scale++, scale++)
				{
					std::string scaleitem = "UpScale x" + std::to_string(scale);
					if (ImGui::MenuItem(scaleitem.c_str(), NULL))
					{
						if (map)
						{
							map->ExportToObjWIP(GetWorkDir(), EXPORT_XYZ, scale);
						}
						else
						{
							logf("Select map first\n");
						}
					}
				}

				for (int scale = 16; scale > 0; scale--, scale--)
				{
					std::string scaleitem = "DownScale x" + std::to_string(scale);
					if (ImGui::MenuItem(scaleitem.c_str(), NULL))
					{
						if (map)
						{
							map->ExportToObjWIP(GetWorkDir(), EXPORT_XYZ, -scale);
						}
						else
						{
							logf("Select map first\n");
						}
					}
				}
				ImGui::EndMenu();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export map geometry without textures");
				ImGui::EndTooltip();
			}


			if (map && ImGui::MenuItem("ValveHammerEditor (.map) [WIP]", NULL))
			{
				if (map)
				{
					map->ExportToMapWIP(GetWorkDir());
				}
				else
				{
					logf("Select map first\n");
				}
			}

			if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export .map ( NOT WORKING at this time:) )");
				ImGui::EndTooltip();
			}

			if ((map && !map->is_mdl_model) && ImGui::MenuItem("VIS .prt file", NULL))
			{
				if (map)
				{
					map->ExportPortalFile();
				}
				else
				{
					logf("Select map first\n");
				}
			}


			if ((map && !map->is_mdl_model) && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export portal file for do REVIS");
				ImGui::EndTooltip();
			}

			if (map && ImGui::MenuItem("RAD.exe .ext & .wa_ files", NULL))
			{
				if (map)
				{
					map->ExportExtFile();
				}
				else
				{
					logf("Select map first\n");
				}
			}

			if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export face extens (.ext) file for rad.exe");
				ImGui::EndTooltip();
			}



			if (map && ImGui::MenuItem("Lighting .lit file", NULL))
			{
				if (map)
				{
					map->ExportLightFile();
				}
				else
				{
					logf("Select map first\n");
				}
			}

			if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Export lightmap file (.lit)");
				ImGui::EndTooltip();
			}


			if (map && !map->is_mdl_model)
			{
				if (ImGui::BeginMenu("Export BSP model"))
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
							if (ImGui::BeginMenu("With origin", !app->isLoading && i >= 0))
							{
								if (ImGui::MenuItem("With WAD", 0, false, !app->isLoading && i >= 0))
								{
									ExportModel(map, i, 0, false);
								}
								if (ImGui::MenuItem("With intenal textures[HL1]", 0, false, !app->isLoading && i >= 0))
								{
									ExportModel(map, i, 2, false);
								}
								if (ImGui::MenuItem("With intenal textures[QUAKE/HL1+XASH]", 0, false, !app->isLoading && i >= 0))
								{
									ExportModel(map, i, 1, false);
								}
								ImGui::EndMenu();
							}
							if (ImGui::BeginMenu("Without origin", !app->isLoading && i >= 0))
							{
								if (ImGui::MenuItem("With WAD", 0, false, !app->isLoading && i >= 0))
								{
									ExportModel(map, i, 0, true);
								}
								if (ImGui::MenuItem("With intenal textures[HL1]", 0, false, !app->isLoading && i >= 0))
								{
									ExportModel(map, i, 2, true);
								}
								if (ImGui::MenuItem("With intenal textures[QUAKE/HL1+XASH]", 0, false, !app->isLoading && i >= 0))
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
			}

			if ((map && !map->is_mdl_model) && ImGui::BeginMenu("WAD"))
			{
				std::string hash = "##1";
				for (auto& wad : map->getBspRender()->wads)
				{
					if (wad->dirEntries.size() == 0)
						continue;
					hash += "1";
					if (ImGui::MenuItem((basename(wad->filename) + hash).c_str()))
					{
						logf("Preparing to export {}.\n", basename(wad->filename));
						createDir(GetWorkDir());
						createDir(GetWorkDir() + "wads");
						createDir(GetWorkDir() + "wads/" + basename(wad->filename));

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
										logf("Exporting {} from {} to working directory.\n", texture->szName, basename(wad->filename));
										COLOR4* texturedata = ConvertWadTexToRGBA(texture);

										lodepng_encode32_file((GetWorkDir() + "wads/" + basename(wad->filename) + "/" + std::string(texture->szName) + ".png").c_str()
											, (unsigned char*)texturedata, texture->nWidth, texture->nHeight);


										/*	int lastMipSize = (texture->nWidth / 8) * (texture->nHeight / 8);

											COLOR3* palette = (COLOR3*)(texture->data + texture->nOffsets[3] + lastMipSize + sizeof(short) - 40);

											lodepng_encode24_file((GetWorkDir() + "wads/" + basename(wad->filename) + "/" + std::string(texture->szName) + ".pal.png").c_str()
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

			ImGui::EndMenu();
		}
		if ((map && !map->is_mdl_model) && ImGui::BeginMenu("Import", !app->isLoading))
		{
			if (ImGui::MenuItem("BSP model(native)", NULL))
			{
				showImportMapWidget_Type = SHOW_IMPORT_MODEL_BSP;
				showImportMapWidget = true;
			}

			if (ImGui::MenuItem("BSP model(cached as func_breakable)", NULL))
			{
				showImportMapWidget_Type = SHOW_IMPORT_MODEL_ENTITY;
				showImportMapWidget = true;
			}

			if (map && ImGui::MenuItem("Lighting .lit file", NULL))
			{
				if (map)
				{
					map->ImportLightFile();
				}
				else
				{
					logf("Select map first\n");
				}
			}

			if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Import lightmap file (.lit)");
				ImGui::EndTooltip();
			}


			if (ImGui::MenuItem("Entity file", NULL))
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
						entFilePath = GetWorkDir() + (map->bsp_name + ".ent");
					}

					logf("Import entities from: {}\n", entFilePath);
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
						logf("Fatal Error! No file!\n");
					}
				}
			}

			if (ImGui::MenuItem("Import all textures from wad", NULL))
			{
				if (map)
				{
					ifd::FileDialog::Instance().Open("WadOpenDialog", "Open .wad", "Wad file (*.wad){.wad},.*", false, g_settings.lastdir);
				}

				if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					std::string embtextooltip;
					ImGui::TextUnformatted(fmt::format("Embeds textures from {}{}", GetWorkDir(), map->bsp_name + ".wad").c_str());
					ImGui::EndTooltip();
				}
			}

			bool ditheringEnabled = ImGui::BeginMenu("WAD +dithering");

			if (ditheringEnabled || ImGui::BeginMenu("WAD"))
			{
				std::string hash = "##1";
				for (auto& wad : map->getBspRender()->wads)
				{
					if (wad->dirEntries.size() == 0)
						continue;
					hash += "1";
					if (ImGui::MenuItem((basename(wad->filename) + hash).c_str()))
					{
						logf("Preparing to import {}.\n", basename(wad->filename));
						if (!dirExists(GetWorkDir() + "wads/" + basename(wad->filename)))
						{
							logf("Error. No files in {} directory.\n", GetWorkDir() + "wads/" + basename(wad->filename));
						}
						else
						{
							copyFile(wad->filename, wad->filename + ".bak");

							Wad* resetWad = new Wad(wad->filename);
							resetWad->write(NULL, 0);
							delete resetWad;

							Wad* tmpWad = new Wad(wad->filename);

							std::vector<WADTEX*> textureList{};
							fs::path tmpPath = GetWorkDir() + "wads/" + basename(wad->filename);

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

									logf("Importing {} from workdir {} wad.\n", basename(file), basename(wad->filename));
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
											logf("Need apply quantizer to {}\n", basename(file));
											Quantizer* tmpCQuantizer = new Quantizer(256, 8);

											if (ditheringEnabled)
												tmpCQuantizer->ApplyColorTableDither((COLOR3*)image_bytes, w2, h2);
											else
												tmpCQuantizer->ApplyColorTable((COLOR3*)image_bytes, w2 * h2);

											logf("Reduce color of image from >{} to {}\n", oldcolors, GetImageColors((COLOR3*)image_bytes, w2 * h2));

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
							logf("Success load all textures\n");

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

		if (map && dirExists(g_settings.gamedir + "/svencoop_addon/maps/"))
		{
			if (ImGui::MenuItem("Sven Test"))
			{
				std::string mapPath = g_settings.gamedir + "/svencoop_addon/maps/" + map->bsp_name + ".bsp";
				std::string entPath = g_settings.gamedir + "/svencoop_addon/scripts/maps/bspguy/maps/" + map->bsp_name + ".ent";

				map->update_ent_lump(true); // strip nodes before writing (to skip slow node graph generation)
				map->write(mapPath);
				map->update_ent_lump(false); // add the nodes back in for conditional loading in the ent file

				std::ofstream entFile(entPath, std::ios::trunc);
				if (entFile.is_open())
				{
					logf("Writing {}\n", entPath);
					entFile.write((const char*)map->lumps[LUMP_ENTITIES], map->bsp_header.lump[LUMP_ENTITIES].nLength - 1);
				}
				else
				{
					logf("Failed to open ent file for writing:\n{}\n", entPath);
					logf("Check that the directories in the path exist, and that you have permission to write in them.\n");
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Saves the .bsp and .ent file to your svencoop_addon folder.\n\nAI nodes will be stripped to skip node graph generation.\n");
				ImGui::EndTooltip();
			}
		}

		if (ImGui::MenuItem("Reload", 0, false, !app->isLoading))
		{
			app->reloadMaps();
		}
		if (ImGui::MenuItem("Validate", 0, false, !app->isLoading))
		{
			if (map)
			{
				logf("Validating {}\n", map->bsp_name);
				map->validate();
			}
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Settings", 0, false, !app->isLoading))
		{
			if (!showSettingsWidget)
			{
				reloadSettings = true;
			}
			showSettingsWidget = true;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Exit", NULL))
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
				logf("Save settings fatal error!\n");
			}
			else
			{
				glfwTerminate();
				std::quick_exit(0);
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit", (map && !map->is_mdl_model)))
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

		if (ImGui::MenuItem(undoTitle.c_str(), "Ctrl+Z", false, canUndo))
		{
			rend->undo();
		}
		else if (ImGui::MenuItem(redoTitle.c_str(), "Ctrl+Y", false, canRedo))
		{
			rend->redo();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Cut", "Ctrl+X", false, nonWorldspawnEntSelected && app->pickInfo.selectedEnts.size()))
		{
			app->cutEnt();
		}
		if (ImGui::MenuItem("Copy", "Ctrl+C", false, nonWorldspawnEntSelected && app->pickInfo.selectedEnts.size()))
		{
			app->copyEnt();
		}
		if (ImGui::MenuItem("Paste", "Ctrl+V", false, mapSelected && app->copiedEnts.size()))
		{
			app->pasteEnt(false);
		}
		if (ImGui::MenuItem("Paste at original origin", 0, false, entSelected && app->copiedEnts.size()))
		{
			app->pasteEnt(true);
		}
		if (ImGui::MenuItem("Delete", "Del", false, nonWorldspawnEntSelected))
		{
			app->deleteEnts();
		}
		if (ImGui::MenuItem("Unhide all", "CTRL+ALT+H"))
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

		if (ImGui::MenuItem("Duplicate BSP model", 0, false, !app->isLoading && allowDuplicate))
		{
			logf("Execute 'duplicate' for {} models.\n", app->pickInfo.selectedEnts.size());
			for (auto& ent : app->pickInfo.selectedEnts)
			{
				DuplicateBspModelCommand* command = new DuplicateBspModelCommand("Duplicate BSP Model", ent);
				command->execute();
				map->getBspRender()->pushUndoCommand(command);
			}
		}

		if (ImGui::MenuItem(app->movingEnt ? "Ungrab" : "Grab", "ALT+G", false, nonWorldspawnEntSelected))
		{
			if (!app->movingEnt)
				app->grabEnt();
			else
			{
				app->ungrabEnt();
			}
		}
		if (ImGui::MenuItem("Transform", "Ctrl+M", false, entSelected))
		{
			showTransformWidget = !showTransformWidget;
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Properties", "Alt+Enter", false, entSelected))
		{
			showKeyvalueWidget = !showKeyvalueWidget;
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Map", (map && !map->is_mdl_model)))
	{
		if (ImGui::MenuItem("Entity Report", NULL))
		{
			showEntityReport = true;
		}

		if (ImGui::MenuItem("Show Limits", NULL))
		{
			showLimitsWidget = true;
		}

		ImGui::Separator();


		if (ImGui::MenuItem("Clean", 0, false, !app->isLoading && map))
		{
			CleanMapCommand* command = new CleanMapCommand("Clean " + map->bsp_name, app->getSelectedMapId(), rend->undoLumpState);
			rend->saveLumpState(0xffffffff, false);
			command->execute();
			rend->pushUndoCommand(command);
		}

		if (ImGui::MenuItem("Optimize", 0, false, !app->isLoading && map))
		{
			OptimizeMapCommand* command = new OptimizeMapCommand("Optimize " + map->bsp_name, app->getSelectedMapId(), rend->undoLumpState);
			rend->saveLumpState(0xffffffff, false);
			command->execute();
			rend->pushUndoCommand(command);
		}

		if (ImGui::BeginMenu("Show clipnodes", map))
		{
			if (ImGui::MenuItem("[-1] - Auto", NULL, app->clipnodeRenderHull == -1))
			{
				app->clipnodeRenderHull = -1;
			}
			if (ImGui::MenuItem("[0] - Point", NULL, app->clipnodeRenderHull == 0))
			{
				app->clipnodeRenderHull = 0;
			}
			if (ImGui::MenuItem("[1] - Human", NULL, app->clipnodeRenderHull == 1))
			{
				app->clipnodeRenderHull = 1;
			}
			if (ImGui::MenuItem("[2] - Large", NULL, app->clipnodeRenderHull == 2))
			{
				app->clipnodeRenderHull = 2;
			}
			if (ImGui::MenuItem("[3] - Head", NULL, app->clipnodeRenderHull == 3))
			{
				app->clipnodeRenderHull = 3;
			}
			ImGui::EndMenu();
		}
		ImGui::Separator();

		bool hasAnyCollision = anyHullValid[1] || anyHullValid[2] || anyHullValid[3];

		if (ImGui::BeginMenu("Delete Hull", hasAnyCollision && !app->isLoading && map))
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
					logf("Deleted hull {} in map {}\n", i, map->bsp_name);
					//}
					checkValidHulls();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Redirect Hull", hasAnyCollision && !app->isLoading && map))
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
							logf("Redirected hull {} to hull {} in map {}\n", i, k, map->bsp_name);
							//}
							checkValidHulls();
						}
					}
					ImGui::EndMenu();
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Fixes", !app->isLoading && map))
		{
			if (ImGui::MenuItem("Bad surface extents"))
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
				ImGui::TextUnformatted("Mark all bad surfaces as 'TEX_SPECIAL'");
				ImGui::EndTooltip();
			}
			if (ImGui::MenuItem("Swapped leaf mins/maxs"))
			{
				for (int i = 0; i < map->leafCount; i++)
				{
					for (int n = 0; n < 3; n++)
					{
						if (map->leaves[i].nMins[n] > map->leaves[i].nMaxs[n])
						{
							logf("Leaf {}: swap mins/maxs\n", i);
							std::swap(map->leaves[i].nMins[n], map->leaves[i].nMaxs[n]);
						}
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Swap all bad mins/maxs in leaves LUMP.");
				ImGui::EndTooltip();
			}
			if (ImGui::MenuItem("Swapped models mins/maxs"))
			{
				for (int i = 0; i < map->modelCount; i++)
				{
					for (int n = 0; n < 3; n++)
					{
						if (map->models[i].nMins[n] > map->models[i].nMaxs[n])
						{
							logf("Model {}: swap mins/maxs\n", i);
							std::swap(map->models[i].nMins[n], map->models[i].nMaxs[n]);
						}
					}
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Swap all bad mins/maxs in models.");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Bad face reference in marksurf"))
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
				ImGui::TextUnformatted("Replace all invalid surfaces to zero.");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Unused models"))
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
				ImGui::TextUnformatted("Attach all unused models to func_wall.");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Missing textures"))
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
								logf("Found unnamed texture in face {}. Replaced by aaatrigger.\n", i);
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
				ImGui::TextUnformatted("Replace all missing textures to white with same size.");
				ImGui::TextUnformatted("Replace all unnamed textures to AAATRIGGER");
				ImGui::EndTooltip();
			}


			ImGui::EndMenu();
		}

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Create", (map && !map->is_mdl_model)))
	{
		if (ImGui::MenuItem("Entity", 0, false, map))
		{
			Entity* newEnt = new Entity();
			vec3 origin = (cameraOrigin + app->cameraForward * 100);
			if (app->gridSnappingEnabled)
				origin = app->snapToGrid(origin);
			newEnt->addKeyvalue("origin", origin.toKeyvalueString());
			newEnt->addKeyvalue("classname", "info_player_deathmatch");

			CreateEntityCommand* createCommand = new CreateEntityCommand("Create Entity", app->getSelectedMapId(), newEnt);
			delete newEnt;
			createCommand->execute();
			rend->pushUndoCommand(createCommand);
		}

		if (ImGui::MenuItem("BSP Passable Model", 0, false, !app->isLoading && map))
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
			command->execute();
			delete newEnt;
			rend->pushUndoCommand(command);

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

		if (ImGui::MenuItem("BSP Trigger Model", 0, false, !app->isLoading && map))
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
			command->execute();
			delete newEnt;
			rend->pushUndoCommand(command);

			newEnt = map->ents[map->ents.size() - 1];
			if (newEnt && newEnt->getBspModelIdx() >= 0)
			{
				BSPMODEL& model = map->models[newEnt->getBspModelIdx()];
				model.iFirstFace = 0;
				model.nFaces = 0;
			}
		}

		if (ImGui::MenuItem("BSP Solid Model", 0, false, !app->isLoading && map))
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
			command->execute();
			delete newEnt;
			rend->pushUndoCommand(command);

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

	if (ImGui::BeginMenu("Widgets"))
	{
		if (map && map->is_mdl_model)
		{
			if (ImGui::MenuItem("Go to", "Ctrl+G", showGOTOWidget))
			{
				showGOTOWidget = !showGOTOWidget;
				showGOTOWidget_update = true;
			}
			if (ImGui::MenuItem("Log", "", showLogWidget))
			{
				showLogWidget = !showLogWidget;
			}
		}
		else
		{
			if (ImGui::MenuItem("Debug", NULL, showDebugWidget))
			{
				showDebugWidget = !showDebugWidget;
			}
			if (ImGui::MenuItem("Keyvalue Editor", "Alt+Enter", showKeyvalueWidget))
			{
				showKeyvalueWidget = !showKeyvalueWidget;
			}
			if (ImGui::MenuItem("Transform", "Ctrl+M", showTransformWidget))
			{
				showTransformWidget = !showTransformWidget;
			}
			if (ImGui::MenuItem("Go to", "Ctrl+G", showGOTOWidget))
			{
				showGOTOWidget = !showGOTOWidget;
				showGOTOWidget_update = true;
			}
			if (ImGui::MenuItem("Face Properties", "", showFaceEditWidget))
			{
				showFaceEditWidget = !showFaceEditWidget;
			}
			if (ImGui::MenuItem("Texture Browser", "", showTextureBrowser))
			{
				showTextureBrowser = !showTextureBrowser;
			}
			if (ImGui::MenuItem("LightMap Editor (WIP)", "", showLightmapEditorWidget))
			{
				showLightmapEditorWidget = !showLightmapEditorWidget;
				FaceSelectePressed();
				showLightmapEditorUpdate = true;
			}
			if (ImGui::MenuItem("Map merge", "", showMergeMapWidget))
			{
				showMergeMapWidget = !showMergeMapWidget;
			}
			if (ImGui::MenuItem("Log", "", showLogWidget))
			{
				showLogWidget = !showLogWidget;
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Windows"))
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

	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("View help"))
		{
			showHelpWidget = true;
		}
		if (ImGui::MenuItem("About"))
		{
			showAboutWidget = true;
		}
		ImGui::EndMenu();
	}

	if (DebugKeyPressed)
	{
		if (ImGui::BeginMenu("(DEBUG)"))
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
	if (ImGui::Begin("toolbar", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
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
			ImGui::TextUnformatted("Object selection mode");
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
			ImGui::TextUnformatted("Face selection mode");
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
	if (ImGui::Begin("Overlay", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		ImGui::Text("%.0f FPS", imgui_io->Framerate);
		if (ImGui::BeginPopupContextWindow())
		{
			ImGui::Checkbox("VSync", &g_settings.vsync);
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

		if (ImGui::Begin("status", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
		{
			if (app->modelUsesSharedStructures)
			{
				if (app->transformMode == TRANSFORM_MODE_MOVE && !app->moveOrigin)
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "SHARED DATA (EDIT ONLY VISUAL DATA WITHOUT COLLISION)");
				else
					ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "SHARED DATA");
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
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "CONCAVE SOLID");
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
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "INVALID SOLID");
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
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "BAD SURFACE EXTENTS");
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
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "LIGHTMAP TOO LARGE");
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

		if (ImGui::Begin("loader", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
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
			case 0: ImGui::Text("Loading |"); break;
			case 1: ImGui::Text("Loading /"); break;
			case 2: ImGui::Text("Loading -"); break;
			case 3: ImGui::Text("Loading \\"); break;
			case 4: ImGui::Text("Loading |"); break;
			case 5: ImGui::Text("Loading /"); break;
			case 6: ImGui::Text("Loading -"); break;
			case 7: ImGui::Text("Loading |"); break;
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

	if (ImGui::Begin("Debug info", &showDebugWidget))
	{
		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text(fmt::format("Origin: {} {} {}", (int)cameraOrigin.x, (int)cameraOrigin.y, (int)cameraOrigin.z).c_str());
			ImGui::Text(fmt::format("Angles: {} {} {}", (int)cameraAngles.x, (int)cameraAngles.y, (int)cameraAngles.z).c_str());

			ImGui::Text(fmt::format("Selected faces: {}", (unsigned int)app->pickInfo.selectedFaces.size()).c_str());
			ImGui::Text(fmt::format("PickMode: {}", app->pickMode).c_str());
		}
		if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!map)
			{
				ImGui::Text("No map selected.");
			}
			else
			{
				ImGui::Text(fmt::format("Name: {}", map->bsp_name.c_str()).c_str());

				if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
				{
					if (app->pickInfo.selectedEnts.size())
					{
						ImGui::Text(fmt::format("Entity ID: {}", entIdx).c_str());
					}

					int modelIdx = -1;

					if (entIdx >= 0)
					{
						modelIdx = map->ents[entIdx]->getBspModelIdx();
					}


					if (modelIdx > 0)
					{
						ImGui::Checkbox("Debug clipnodes", &app->debugClipnodes);
						ImGui::SliderInt("Clipnode", &app->debugInt, 0, app->debugIntMax);

						ImGui::Checkbox("Debug nodes", &app->debugNodes);
						ImGui::SliderInt("Node", &app->debugNode, 0, app->debugNodeMax);
					}

					if (app->pickInfo.selectedFaces.size())
					{
						BSPFACE32& face = map->faces[app->pickInfo.selectedFaces[0]];

						if (modelIdx > 0)
						{
							BSPMODEL& model = map->models[modelIdx];
							ImGui::Text(fmt::format("Model ID: {}", modelIdx).c_str());

							ImGui::Text(fmt::format("Model polies: {}", model.nFaces).c_str());
						}

						ImGui::Text(fmt::format("Face ID: {}", app->pickInfo.selectedFaces[0]).c_str());
						ImGui::Text(fmt::format("Plane ID: {}", face.iPlane).c_str());

						if (face.iTextureInfo < map->texinfoCount)
						{
							BSPTEXTUREINFO& info = map->texinfos[face.iTextureInfo];
							if (info.iMiptex >= 0 && info.iMiptex < map->textureCount)
							{
								int texOffset = ((int*)map->textures)[info.iMiptex + 1];
								if (texOffset >= 0)
								{
									BSPMIPTEX& tex = *((BSPMIPTEX*)(map->textures + texOffset));
									ImGui::Text(fmt::format("Texinfo ID: {}", face.iTextureInfo).c_str());
									ImGui::Text(fmt::format("Texture ID: {}", info.iMiptex).c_str());
									ImGui::Text(fmt::format("Texture: {} ({}x{})", tex.szName, tex.nWidth, tex.nHeight).c_str());
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
						ImGui::Text(fmt::format("Lightmap Offset: {}", face.nLightmapOffset).c_str());
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
				if (!map)
				{
					ImGui::Text("No map selected");
				}
				else
				{
					vec3 localCamera = cameraOrigin - map->getBspRender()->mapOffset;

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
							ImGui::Text(fmt::format("Contents: {}", map->getLeafContentsName(contents)).c_str());
							if (i == 0)
							{
								ImGui::Text(fmt::format("Leaf: {}", leafIdx).c_str());
							}
							ImGui::Text(fmt::format("Parent Node: {} (child {})",
								nodeBranch.size() ? nodeBranch[nodeBranch.size() - 1] : headNode,
								childIdx).c_str());
							ImGui::Text(fmt::format("Head Node: {}", headNode).c_str());
							ImGui::Text(fmt::format("Depth: {}", nodeBranch.size()).c_str());

							ImGui::Unindent();
							ImGui::TreePop();
						}
						ImGui::PopStyleColor();
					}
				}
			}
			else
			{
				ImGui::Text("No model selected");
			}
		}

		if (map && ImGui::CollapsingHeader("Textures usage", ImGuiTreeNodeFlags_DefaultOpen))
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

			ImGui::Text(fmt::format("Total textures used in map {}", map->textureCount).c_str());
			ImGui::Text(fmt::format("Used {} internal textures of {}", InternalTextures, TotalInternalTextures).c_str());
			ImGui::Text(fmt::format("Used {} wad files", TotalInternalTextures > 0 ? (int)mapTexsUsage.size() - 1 : (int)mapTexsUsage.size()).c_str());
			ImGui::Text(fmt::format("Used {} wad textures", WadTextures).c_str());

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

		if (map && ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("DebugVec0 %6.2f %6.2f %6.2f", app->debugVec0.x, app->debugVec0.y, app->debugVec0.z);
			ImGui::Text("DebugVec1 %6.2f %6.2f %6.2f", app->debugVec1.x, app->debugVec1.y, app->debugVec1.z);
			ImGui::Text("DebugVec2 %6.2f %6.2f %6.2f", app->debugVec2.x, app->debugVec2.y, app->debugVec2.z);
			ImGui::Text("DebugVec3 %6.2f %6.2f %6.2f", app->debugVec3.x, app->debugVec3.y, app->debugVec3.z);

			float mb = map->getBspRender()->undoMemoryUsage / (1024.0f * 1024.0f);
			ImGui::Text("Undo Memory Usage: %.2f MB", mb);

			bool isScalingObject = app->transformMode == TRANSFORM_MODE_SCALE && app->transformTarget == TRANSFORM_OBJECT;
			bool isMovingOrigin = app->transformMode == TRANSFORM_MODE_MOVE && app->transformTarget == TRANSFORM_ORIGIN && app->originSelected;
			bool isTransformingValid = !(app->modelUsesSharedStructures && app->transformMode != TRANSFORM_MODE_MOVE) && (app->isTransformableSolid || isScalingObject);
			bool isTransformingWorld = entIdx == 0 && app->transformTarget != TRANSFORM_OBJECT;

			ImGui::Text(fmt::format("isTransformableSolid {}", app->isTransformableSolid).c_str());
			ImGui::Text(fmt::format("isScalingObject {}", isScalingObject).c_str());
			ImGui::Text(fmt::format("isMovingOrigin {}", isMovingOrigin).c_str());
			ImGui::Text(fmt::format("isTransformingValid {}", isTransformingValid).c_str());
			ImGui::Text(fmt::format("isTransformingWorld {}", isTransformingWorld).c_str());
			ImGui::Text(fmt::format("transformMode {}", app->transformMode).c_str());
			ImGui::Text(fmt::format("transformTarget {}", app->transformTarget).c_str());
			ImGui::Text(fmt::format("modelUsesSharedStructures {}", app->modelUsesSharedStructures).c_str());

			ImGui::Text(fmt::format("showDragAxes {}\nmovingEnt {}\nanyAltPressed {}",
				app->showDragAxes, app->movingEnt, app->anyAltPressed).c_str());

			ImGui::Checkbox("Show DragAxes", &app->showDragAxes);
		}

		if (map)
		{
			if (ImGui::Button("PRESS ME TO DECAL"))
			{
				for (auto& ent : map->ents)
				{
					if (ent->hasKey("classname") && ent->keyvalues["classname"] == "infodecal")
					{
						map->decalShoot(ent->getOrigin(), "Hello world");
					}
				}
			}

			/*if (ImGui::Button("TEST VIS DATA[CLIPNODES]"))
			{
				debugVisMode = 1;
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted("MARK ALL VISIBLED CLIPNODES FACES FROM CAMERA POS");
				ImGui::TextUnformatted("TEST PVS VIS DATA");
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			*/
			if (ImGui::Button("TEST VIS DATA[HIGHFACES]"))
			{
				debugVisMode = 2;
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted("MARK ALL VISIBLED AND INVISIBLED FACES FROM CAMERA POS");
				ImGui::TextUnformatted("TEST PVS VIS DATA");
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			static int model1 = 0;
			static int model2 = 0;

			ImGui::DragInt("Model 1 ##mdl1", &model1, 1, 0, MAX_MAP_MODELS);

			ImGui::DragInt("Model 2 ##mdl1", &model2, 1, 0, MAX_MAP_MODELS);

			if (ImGui::Button("Swap two models"))
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
				ImGui::TextUnformatted("SWAP TWO MODELS, USEFUL FOR EDITING WITH SAVE CRC");
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
			logf("Debug: Used {} wad files(include map file)\n", (int)mapTexsUsage.size());
	}
	ImGui::End();


	if (debugVisMode > 0 && !g_app->reloading)
	{
		vec3 localCamera = cameraOrigin - map->getBspRender()->mapOffset;

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
				auto faceList = map->getLeafFaces(l + 1);
				for (const auto& idx : faceList)
				{
					map->getBspRender()->highlightFace(idx, true, COLOR4(230 + rand() % 25, 0, 0, 255), true);
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
					map->getBspRender()->highlightFace(idx, true, COLOR4(0, 0, 230 + rand() % 25, 255), true);
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
	if (ImGui::Begin("Texture browser", &showTextureBrowser, 0))
	{
		// Список встроенных в карту текстур, с возможностью Удалить/Экспортировать/Импортировать/Переименовать
		// Список встроенных в карту WAD текстур, с возможностью Удалить/
		// Список всех WAD файлов и доступных текстур, с возможностью добавления в карту ссылки или копии текстуры.
		if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_::ImGuiTabBarFlags_FittingPolicyScroll |
			ImGuiTabBarFlags_::ImGuiTabBarFlags_NoCloseWithMiddleMouseButton |
			ImGuiTabBarFlags_::ImGuiTabBarFlags_Reorderable))
		{
			ImGui::Dummy(ImVec2(0, 10));
			if (ImGui::BeginTabItem("Internal"))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGuiListClipper clipper;
				clipper.Begin(LineOffsets.Size, 30.0f);
				while (clipper.Step())
				{

				}
				clipper.End();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Internal Names"))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGuiListClipper clipper;
				clipper.Begin(LineOffsets.Size, 30.0f);
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
						clipper.Begin(LineOffsets.Size, 30.0f);
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
	if (ImGui::Begin("Keyvalue Editor", &showKeyvalueWidget, 0))
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
			ImGui::Text("Class:"); ImGui::SameLine();
			if (cname != "worldspawn")
			{
				if (ImGui::Button((" " + cname + " ").c_str()))
					ImGui::OpenPopup("classname_popup");
			}
			else
			{
				ImGui::Text(cname.c_str());
			}
			ImGui::PopFont();

			if (fgdClass)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted((fgdClass->description).c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
			}


			if (ImGui::BeginPopup("classname_popup"))
			{
				ImGui::Text("Change Class");
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

			if (ImGui::BeginTabBar("##tabs"))
			{
				if (ImGui::BeginTabItem("Attributes"))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_SmartEditTab(entIdx);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Flags"))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_FlagsTab(entIdx);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Raw Edit"))
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
				ImGui::Text("No entity selected");
			else
				ImGui::Text("No fgd loaded");
		}

	}
	ImGui::End();
}

void Gui::drawKeyvalueEditor_SmartEditTab(int entIdx)
{
	Bsp* map = app->getSelectedMap();
	if (!map || entIdx < 0)
	{
		ImGui::Text("No entity selected");
		return;
	}
	Entity* ent = map->ents[entIdx];
	std::string cname = ent->keyvalues["classname"];
	FgdClass* fgdClass = app->fgd->getFgdClass(cname);
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::BeginChild("SmartEditWindow");

	ImGui::Columns(2, "smartcolumns", false); // 4-ways, with border

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
					keyvalue.description = "Model";
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
			std::string niceName = keyvalue.description;

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

							inputData->bspRenderer->refreshEnt(inputData->entIdx);
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
		ImGui::Text("No entity selected");
		return;
	}

	Entity* ent = map->ents[entIdx];

	ImGui::BeginChild("FlagsWindow");

	unsigned int spawnflags = strtoul(ent->keyvalues["spawnflags"].c_str(), NULL, 10);
	FgdClass* fgdClass = app->fgd->getFgdClass(ent->keyvalues["classname"]);

	ImGui::Columns(2, "keyvalcols", true);

	static bool checkboxEnabled[32];

	for (int i = 0; i < 32; i++)
	{
		if (i == 16)
		{
			ImGui::NextColumn();
		}
		std::string name;
		if (fgdClass)
		{
			name = fgdClass->spawnFlagNames[i];
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
	}

	ImGui::Columns(1);

	ImGui::EndChild();
}

void Gui::drawKeyvalueEditor_RawEditTab(int entIdx)
{
	Bsp* map = app->getSelectedMap();
	if (!map || entIdx < 0)
	{
		ImGui::Text("No entity selected");
		return;
	}

	Entity* ent = map->ents[entIdx];
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::Columns(4, "keyvalcols", false);

	float butColWidth = smallFont->CalcTextSizeA(GImGui->FontSize, 100, 100, " X ").x + style.FramePadding.x * 4;
	float textColWidth = (ImGui::GetWindowWidth() - (butColWidth + style.FramePadding.x * 2) * 2) * 0.5f;

	ImGui::SetColumnWidth(0, butColWidth);
	ImGui::SetColumnWidth(1, textColWidth);
	ImGui::SetColumnWidth(2, textColWidth);
	ImGui::SetColumnWidth(3, butColWidth);

	ImGui::NextColumn();
	ImGui::Text("  Key"); ImGui::NextColumn();
	ImGui::Text("Value"); ImGui::NextColumn();
	ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::BeginChild("RawValuesWindow");

	ImGui::Columns(4, "keyvalcols2", false);

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
					if (g_app->SelectedMap)
						g_app->SelectedMap->getBspRender()->saveLumpState(0xffffffff, false);
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
						if (g_app->SelectedMap)
							g_app->SelectedMap->getBspRender()->saveLumpState(0xffffffff, false);
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
					ImGui::TextUnformatted("ANGLES NOT SUPPORTED");
				}
				else if (ent->keyvalues["classname"] == "env_sprite")
				{
					ImGui::SetNextItemWidth(inputWidth);
					ImGui::TextUnformatted("ANGLES PARTIALLY SUPPORT");
				}
				else if (ent->keyvalues["classname"] == "func_breakable")
				{
					ImGui::SetNextItemWidth(inputWidth);
					ImGui::TextUnformatted("ANGLES Y NOT SUPPORT");
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


	if (ImGui::Button(" Add : "))
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

	ImGui::InputText("##keyval_add", &keyName);

	ImGui::EndChild();
}

void Gui::drawGOTOWidget()
{
	ImGui::SetNextWindowSize(ImVec2(410.f, 200.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(410.f, 330.f), ImVec2(410.f, 330.f));
	static vec3 coordinates = vec3();
	static vec3 angles = vec3();
	float angles_y = 0.0f;
	static int modelid = -1, faceid = -1, entid = -1;

	if (ImGui::Begin("GO TO MENU", &showGOTOWidget, 0))
	{
		entid = g_app->pickInfo.GetSelectedEnt();
		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
		if (showGOTOWidget_update)
		{
			coordinates = cameraOrigin;
			angles = cameraAngles;
			showGOTOWidget_update = false;
		}
		ImGui::Text("Coordinates");
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat("##xpos", &coordinates.x, 0.1f, 0, 0, "X: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##ypos", &coordinates.y, 0.1f, 0, 0, "Y: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##zpos", &coordinates.z, 0.1f, 0, 0, "Z: %.0f");
		ImGui::PopItemWidth();
		ImGui::Text("Angles");
		ImGui::PushItemWidth(inputWidth);
		ImGui::DragFloat("##xangles", &angles.x, 0.1f, 0, 0, "X: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##yangles", &angles_y, 0.0f, 0, 0, "Y: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat("##zangles", &angles.z, 0.1f, 0, 0, "Z: %.0f");
		ImGui::PopItemWidth();

		ImGui::PushStyleColor(ImGuiCol_Button, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, (ImVec4)ImColor::HSV(0, 0.7f, 0.7f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, (ImVec4)ImColor::HSV(0, 0.8f, 0.8f));
		if (ImGui::Button("Go to"))
		{
			cameraOrigin = coordinates;
			cameraAngles = angles;
			makeVectors(angles, app->cameraForward, app->cameraRight, app->cameraUp);
		}
		ImGui::PopStyleColor(3);
		Bsp* map = app->getSelectedMap();
		if (map && !map->is_mdl_model)
		{
			ImGui::Separator();
			ImGui::PushItemWidth(inputWidth);
			ImGui::DragInt("Model id:", &modelid);
			ImGui::DragInt("Face id:", &faceid);
			ImGui::DragInt("Entity id:", &entid);
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

	if (ImGui::Begin("Transformation", &showTransformWidget, 0))
	{
		if (!ent)
		{
			ImGui::Text("No entity selected");
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
						vec3 ori = ent->hasKey("origin") ? parseVector(ent->keyvalues["origin"]) : vec3();
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

			ImGui::Text("Move");
			ImGui::PushItemWidth(inputWidth);

			if (ImGui::DragFloat("##xpos", &x, 0.01f, -FLT_MAX_COORD, FLT_MAX_COORD, app->gridSnappingEnabled ? "X: %.2f" : "X: %.0f"))
			{
				originChanged = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat("##ypos", &y, 0.01f, -FLT_MAX_COORD, FLT_MAX_COORD, app->gridSnappingEnabled ? "Y: %.2f" : "Y: %.0f"))
			{
				originChanged = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat("##zpos", &z, 0.01f, -FLT_MAX_COORD, FLT_MAX_COORD, app->gridSnappingEnabled ? "Z: %.2f" : "Z: %.0f"))
			{
				originChanged = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;


			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y));

			ImGui::Text("Scale");
			ImGui::PushItemWidth(inputWidth);

			if (ImGui::DragFloat("##xscale", &sx, 0.002f, 0, 0, "X: %.3f"))
			{
				scaled = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat("##yscale", &sy, 0.002f, 0, 0, "Y: %.3f"))
			{
				scaled = true;
			}
			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			if (ImGui::IsItemActive())
				inputsAreDragging = true;
			ImGui::SameLine();

			if (ImGui::DragFloat("##zscale", &sz, 0.002f, 0, 0, "Z: %.3f"))
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
			ImGui::Text("Target: "); ImGui::NextColumn();

			if (app->transformMode == TRANSFORM_MODE_NONE)
			{
				ImGui::BeginDisabled();
			}

			if (ImGui::RadioButton("Object", &app->transformTarget, TRANSFORM_OBJECT))
			{
				pickCount++;
				vertPickCount++;
			}

			ImGui::NextColumn();
			if (app->transformMode == TRANSFORM_MODE_SCALE)
			{
				ImGui::BeginDisabled();
			}

			if (ImGui::RadioButton("Vertex", &app->transformTarget, TRANSFORM_VERTEX))
			{
				pickCount++;
				vertPickCount++;
			}

			ImGui::NextColumn();

			if (ImGui::RadioButton("Origin", &app->transformTarget, TRANSFORM_ORIGIN))
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
			ImGui::Text("3D Axes: "); ImGui::NextColumn();
			ImGui::RadioButton("Hide", &app->transformMode, TRANSFORM_MODE_NONE); ImGui::NextColumn();
			ImGui::RadioButton("Move", &app->transformMode, TRANSFORM_MODE_MOVE); ImGui::NextColumn();
			ImGui::RadioButton("Scale", &app->transformMode, TRANSFORM_MODE_SCALE); ImGui::NextColumn();

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
			ImGui::Text("Grid Snap:"); ImGui::NextColumn();
			ImGui::SetNextItemWidth(inputWidth4 * 3);
			if (ImGui::SliderInt("##gridsnap", &current_element, 0, grid_snap_modes - 1, element_names[current_element]))
			{
				app->gridSnapLevel = current_element - 1;
				app->gridSnappingEnabled = current_element != 0;
				originChanged = true;
			}
			ImGui::Columns(1);

			ImGui::PushItemWidth(inputWidth);
			ImGui::Checkbox("Texture lock", &app->textureLock);
			ImGui::SameLine();
			ImGui::TextDisabled("(WIP)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted("Doesn't work for angled faces yet. Applies to scaling only.");
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			ImGui::SameLine();
			if (app->transformMode != TRANSFORM_MODE_MOVE || app->transformTarget != TRANSFORM_OBJECT)
				ImGui::BeginDisabled();
			ImGui::Checkbox("Move entity", &app->moveOrigin);
			if (app->transformMode != TRANSFORM_MODE_MOVE || app->transformTarget != TRANSFORM_OBJECT)
				ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::TextDisabled("(origin)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted("Move 'origin' keyvalue instead of model data.");
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
						logf("Scaling has no effect on origins\n");
					}
				}
			}

			inputsWereDragged = inputsAreDragging;
		}
	}
	ImGui::End();
}

void Gui::clearLog()
{
	Buf.clear();
	LineOffsets.clear();
	LineOffsets.push_back(0);
}

void Gui::addLog(const char* s)
{
	int old_size = Buf.size();
	Buf.append(s);
	for (int new_size = Buf.size(); old_size < new_size; old_size++)
		if (Buf[old_size] == '\n')
			LineOffsets.push_back(old_size + 1);
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
	ImGui::SetNextWindowSize(ImVec2(750.f, 300.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));
	if (!ImGui::Begin("Log", &showLogWidget))
	{
		ImGui::End();
		return;
	}

	g_mutex_list[4].lock();
	for (int i = 0; i < g_log_buffer.size(); i++)
	{
		addLog(g_log_buffer[i].c_str());
	}
	g_log_buffer.clear();
	g_mutex_list[4].unlock();

	static int i = 0;

	ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

	bool copy = false;
	bool toggledAutoScroll = false;
	if (ImGui::BeginPopupContextWindow())
	{
		if (ImGui::MenuItem("Copy"))
		{
			copy = true;
		}
		if (ImGui::MenuItem("Clear"))
		{
			clearLog();
		}
		if (ImGui::MenuItem("Auto-scroll", NULL, &AutoScroll))
		{
			toggledAutoScroll = true;
		}
		ImGui::EndPopup();
	}

	ImGui::PushFont(consoleFont);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	const char* buf = Buf.begin();
	const char* buf_end = Buf.end();

	if (copy) ImGui::LogBegin(ImGuiLogType_Clipboard, 0);

	ImGuiListClipper clipper;
	clipper.Begin(LineOffsets.Size);
	while (clipper.Step())
	{
		for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
		{
			const char* line_start = buf + LineOffsets[line_no];
			const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
			ImGui::TextUnformatted(line_start, line_end);
		}
	}
	clipper.End();

	if (copy) ImGui::LogFinish();

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

	if (ImGui::Begin("Settings", &showSettingsWidget))
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
		ImGui::BeginChild("left pane", ImVec2(150, 0), true);

		for (int i = 0; i < settings_tabs; i++)
		{
			if (ImGui::Selectable(tab_titles[i], settingsTab == i))
				settingsTab = i;
		}

		ImGui::Separator();


		ImGui::Dummy(ImVec2(0, 60));
		if (ImGui::Button("Apply settings"))
		{
			apply_settings_pressed = true;
		}

		ImGui::EndChild();


		ImGui::SameLine();

		// right

		ImGui::BeginGroup();
		float footerHeight = settingsTab <= 2 ? ImGui::GetFrameHeightWithSpacing() + 4.f : 0.f;
		ImGui::BeginChild("item view", ImVec2(0, -footerHeight)); // Leave room for 1 line below us
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

		ImGui::BeginChild("right pane content");
		if (settingsTab == 0)
		{
			ImGui::Text("Game Directory:");
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText("##gamedir", &g_settings.gamedir);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			if (ImGui::Button("...##gamedir"))
			{
				ifd::FileDialog::Instance().Open("GameDir", "Select game dir", std::string(), false, g_settings.lastdir);
			}
			ImGui::Text("Import/Export Directory:");
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText("##workdir", &g_settings.workingdir);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			if (ImGui::Button("...##workdir"))
			{
				ifd::FileDialog::Instance().Open("WorkingDir", "Select working dir", std::string(), false, g_settings.lastdir);
			}
			if (ImGui::DragFloat("Font Size", &fontSize, 0.1f, 8, 48, "%f pixels"))
			{
				shouldReloadFonts = true;
			}
			ImGui::DragInt("Undo Levels", &g_settings.undoLevels, 0.05f, 0, 64);
#ifndef NDEBUG
			ImGui::BeginDisabled();
#endif
			ImGui::Checkbox("Verbose Logging", &g_verbose);
#ifndef NDEBUG
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Verbose logging can't be disabled in DEBUG MODE");
				ImGui::EndTooltip();
			}
#endif
			ImGui::SameLine();

			ImGui::Checkbox("Make map backup", &g_settings.backUpMap);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Creates a backup of the BSP file when saving for the first time.");
				ImGui::EndTooltip();
			}

			ImGui::Checkbox("Preserve map CRC", &g_settings.preserveCrc32);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Hack original map CRC after anything edited.");
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			ImGui::Checkbox("Auto import entity file", &g_settings.autoImportEnt);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Automatically import an entity file when the map is opened.");
				ImGui::EndTooltip();
			}

			ImGui::Checkbox("Same directory for entity file", &g_settings.sameDirForEnt);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Use the same directory as the bsp file to import and export the entity file.");
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			if (ImGui::Checkbox("Save windows state", &g_settings.save_windows))
			{
				imgui_io->IniFilename = !g_settings.save_windows ? NULL : iniPath.c_str();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Save windows position and state.");
				ImGui::EndTooltip();
			}

			ImGui::Checkbox("Load default list is empty", &g_settings.defaultIsEmpty);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("If enabled, load default settings lists, if list is empty.");
				ImGui::TextUnformatted("(example entities list, res paths, etc)");
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			ImGui::Checkbox("Move camera to first entity", &g_settings.start_at_entity);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("If enabled, camera moved to first player_start/player_deathmatch/trigger_camera entity.");
				ImGui::EndTooltip();
			}
			ImGui::Separator();

			if (ImGui::Button("RESET ALL SETTINGS"))
			{
				g_settings.reset();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Warning! You want to return all settings to default values?!");
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

			if (ImGui::Button("Add fgd path"))
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

			if (ImGui::Button("Add res path"))
			{
				g_settings.resPaths.push_back({ std::string(), true });
			}
		}
		else if (settingsTab == 3)
		{
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox("Strip wads after load", &g_settings.stripWad);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Automatically strip wad filenames. (path/to/wadname.wad to wadname.wad)");
				ImGui::EndTooltip();
			}
			ImGui::SameLine();

			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox("Mark texinfos for clean", &g_settings.mark_unused_texinfos);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Additional cleanup option, mark more texinfos for delete.");
				ImGui::EndTooltip();
			}
			ImGui::Separator();

			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox("Merge verts [WIP, CAN CAUSE PROBLEMS]", &g_settings.merge_verts);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Additional cleanup option for clean similar verts.\nWarning!! CAN CRASH YOUR MAP!");
				ImGui::EndTooltip();
			}
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::Text("Conditional Point Ent Triggers");

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

			if (ImGui::Button("Add new 'Point Ent Trigger'"))
			{
				g_settings.conditionalPointEntTriggers.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text("Ents That Never Need Any Hulls");

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

			if (ImGui::Button("Add new 'No Hulls Ent'"))
			{
				g_settings.entsThatNeverNeedAnyHulls.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text("Ents That Never Need Collision");

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

			if (ImGui::Button("Add new 'No Collision Ent'"))
			{
				g_settings.entsThatNeverNeedCollision.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text("Passable ents");

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

			if (ImGui::Button("Add new 'Passable Ent'"))
			{
				g_settings.passableEnts.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text("Player Only Triggers");

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

			if (ImGui::Button("Add new 'Player Trigger Ent'"))
			{
				g_settings.playerOnlyTriggers.emplace_back(std::string());
			}
			ImGui::Separator();
			ImGui::Text("Monster Only Triggers");

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

			if (ImGui::Button("Add new 'Monster Trigger Ent'"))
			{
				g_settings.monsterOnlyTriggers.emplace_back(std::string());
			}
		}
		else if (settingsTab == 4)
		{
			ImGui::SetNextItemWidth(pathWidth / 2);
			static unsigned int vis_data_count = MAX_MAP_VISDATA / (1024 * 1024);
			static unsigned int light_data_count = MAX_MAP_LIGHTDATA / (1024 * 1024);

			ImGui::DragFloat("MAX FLOAT COORDINATES", &FLT_MAX_COORD, 64.f, 512.f, 2147483647.f, "%.0f");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("MAX MAP MODELS", (int*)&MAX_MAP_MODELS, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("MAX MAP ENTITIES", (int*)&MAX_MAP_ENTS, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("MAX MAP TEXTURES", (int*)&MAX_MAP_TEXTURES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("MAX MAP NODES", (int*)&MAX_MAP_NODES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("MAX MAP CLIPNODES", (int*)&MAX_MAP_CLIPNODES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("MAX MAP LEAVES", (int*)&MAX_MAP_LEAVES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			if (ImGui::DragInt("MAX MAP VISDATA", (int*)&vis_data_count, 4, 128, 2147483647, "%uMB"))
			{
				MAX_MAP_VISDATA = vis_data_count * (1024 * 1024);
			}
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("MAX MAP EDGES", (int*)&MAX_MAP_EDGES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("MAX MAP SURFEDGES", (int*)&MAX_MAP_SURFEDGES, 4, 128, 2147483647, "%u");
			ImGui::SetNextItemWidth(pathWidth / 2);
			if (ImGui::DragInt("MAX MAP LIGHTDATA", (int*)&light_data_count, 4, 128, 2147483647, "%uMB"))
			{
				MAX_MAP_LIGHTDATA = light_data_count * (1024 * 1024);
			}
			ImGui::SetNextItemWidth(pathWidth / 2);
			if (ImGui::DragInt("MAX MAP TEXTURE DIMENSION", (int*)&MAX_TEXTURE_DIMENSION, 4, 32, 1048576, "%u"))
			{
				MAX_TEXTURE_SIZE = ((MAX_TEXTURE_DIMENSION * MAX_TEXTURE_DIMENSION * 2 * 3) / 2);
			}
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::DragInt("TEXTURE_STEP", (int*)&TEXTURE_STEP, 4, 4, 512, "%u");
		}
		else if (settingsTab == 5)
		{
			ImGui::Text("Viewport:");
			ImGui::Checkbox("VSync", &g_settings.vsync);
			ImGui::DragFloat("Field of View", &app->fov, 0.1f, 1.0f, 150.0f, "%.1f degrees");
			ImGui::DragFloat("Back Clipping plane", &app->zFar, 10.0f, -FLT_MAX_COORD, FLT_MAX_COORD, "%.0f", ImGuiSliderFlags_Logarithmic);
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

			ImGui::Text("Render Flags:");

			ImGui::Columns(2, 0, false);

			if (ImGui::Checkbox("Textures", &renderTextures))
			{
				g_render_flags ^= RENDER_TEXTURES;
			}
			if (ImGui::Checkbox("Lightmaps", &renderLightmaps))
			{
				g_render_flags ^= RENDER_LIGHTMAPS;
				for (int i = 0; i < app->mapRenderers.size(); i++)
					app->mapRenderers[i]->updateModelShaders();
			}
			if (ImGui::Checkbox("Wireframe", &renderWireframe))
			{
				g_render_flags ^= RENDER_WIREFRAME;
			}
			if (ImGui::Checkbox("Origin", &renderOrigin))
			{
				g_render_flags ^= RENDER_ORIGIN;
			}
			if (ImGui::Checkbox("Entity Links", &renderEntConnections))
			{
				g_render_flags ^= RENDER_ENT_CONNECTIONS;
				if (g_render_flags & RENDER_ENT_CONNECTIONS)
				{
					app->updateEntConnections();
				}
			}

			if (ImGui::Checkbox("Point Entities", &renderPointEnts))
			{
				g_render_flags ^= RENDER_POINT_ENTS;
			}
			if (ImGui::Checkbox("Normal Solid Entities", &renderEntities))
			{
				g_render_flags ^= RENDER_ENTS;
			}

			ImGui::NextColumn();
			if (ImGui::Checkbox("Special Solid Entities", &renderSpecialEnts))
			{
				g_render_flags ^= RENDER_SPECIAL_ENTS;
			}
			if (ImGui::Checkbox("Special World Faces", &renderSpecial))
			{
				g_render_flags ^= RENDER_SPECIAL;
			}
			if (ImGui::Checkbox("Models", &renderModels))
			{
				g_render_flags ^= RENDER_MODELS;
			}
			if (ImGui::Checkbox("Animate models", &renderAnimatedModels))
			{
				g_render_flags ^= RENDER_MODELS_ANIMATED;
			}

			if (ImGui::Checkbox("World Leaves", &renderWorldClipnodes))
			{
				g_render_flags ^= RENDER_WORLD_CLIPNODES;
			}
			if (ImGui::Checkbox("Entity Leaves", &renderEntClipnodes))
			{
				g_render_flags ^= RENDER_ENT_CLIPNODES;
			}
			if (ImGui::Checkbox("Transparent clipnodes", &transparentNodes))
			{
				g_render_flags ^= RENDER_TRANSPARENT;
				for (int i = 0; i < app->mapRenderers.size(); i++)
				{
					app->mapRenderers[i]->updateClipnodeOpacity(transparentNodes ? 128 : 255);
				}
			}

			ImGui::Columns(1);

			ImGui::Separator();
			ImGui::Text("Transparent Textures:");

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

			if (ImGui::Button("Add new 'Transparent Texture'"))
			{
				g_settings.transparentTextures.emplace_back(std::string());
			}

			ImGui::Separator();
			ImGui::Text("Transparent Entities:");

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

			if (ImGui::Button("Add new 'Transparent Entity'"))
			{
				g_settings.transparentEntities.emplace_back(std::string());
			}


			ImGui::Separator();
			ImGui::Text("Reverse Pitch Entities:");

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

			if (ImGui::Button("Add new 'Reverse Pitch Entity'"))
			{
				g_settings.entsNegativePitchPrefix.emplace_back(std::string());
			}
		}
		else if (settingsTab == 6)
		{
			ImGui::DragFloat("Movement speed", &app->moveSpeed, 1.0f, 100.0f, 1000.0f, "%.1f");
			ImGui::DragFloat("Rotation speed", &app->rotationSpeed, 0.1f, 0.1f, 100.0f, "%.1f");
		}

		ImGui::EndChild();
		ImGui::EndChild();

		ImGui::EndGroup();
	}
	ImGui::End();


	if (oldShowSettings && !showSettingsWidget || apply_settings_pressed)
	{
		FixupAllSystemPaths();
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
	if (ImGui::Begin("Help", &showHelpWidget))
	{

		if (ImGui::BeginTabBar("##tabs"))
		{
			if (ImGui::BeginTabItem("UI Controls"))
			{
				ImGui::Dummy(ImVec2(0, 10));

				// user guide from the demo
				ImGui::BulletText("Click and drag on lower corner to resize window\n(double-click to auto fit window to its contents).");
				ImGui::BulletText("While adjusting numeric inputs:\n");
				ImGui::Indent();
				ImGui::BulletText("Hold SHIFT/ALT for faster/slower edit.");
				ImGui::BulletText("Double-click or CTRL+click to input value.");
				ImGui::Unindent();
				ImGui::BulletText("While inputing text:\n");
				ImGui::Indent();
				ImGui::BulletText("CTRL+A or double-click to select all.");
				ImGui::BulletText("CTRL+X/C/V to use clipboard cut/copy/paste.");
				ImGui::BulletText("CTRL+Z,CTRL+Y to undo/redo.");
				ImGui::BulletText("You can apply arithmetic operators +,*,/ on numerical values.\nUse +- to subtract.");
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("3D Controls"))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGui::BulletText("WASD to move (hold SHIFT/CTRL for faster/slower movement).");
				ImGui::BulletText("Hold right mouse button to rotate view.");
				ImGui::BulletText("Left click to select objects/entities. Right click for options.");
				ImGui::BulletText("While grabbing an entity:\n");
				ImGui::Indent();
				ImGui::BulletText("Mouse wheel to push/pull (hold SHIFT/CTRL for faster/slower).");
				ImGui::BulletText("Click outside of the entity or press G to let go.");
				ImGui::Unindent();
				ImGui::BulletText("While grabbing 3D transform axes:\n");
				ImGui::Indent();
				ImGui::BulletText("Hold SHIFT/CTRL for faster/slower adjustments");
				ImGui::Unindent();

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Vertex Manipulation"))
			{
				ImGui::Dummy(ImVec2(0, 10));
				ImGui::BulletText("Press F to split a face while 2 edges are selected.");
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
	ImGui::SetNextWindowSize(ImVec2(500.f, 140.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("About", &showAboutWidget))
	{
		ImGui::InputText("Version", &g_version_string, ImGuiInputTextFlags_ReadOnly);

		static char author[] = "w00tguy";
		ImGui::InputText("Author", author, strlen(author), ImGuiInputTextFlags_ReadOnly);

		static char url[] = "https://github.com/wootguy/bspguy";
		ImGui::InputText("Contact", url, strlen(url), ImGuiInputTextFlags_ReadOnly);
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


	if (ImGui::Begin("Merge maps", &showMergeMapWidget))
	{

		if (inPaths.size() < 2)
		{
			inPaths.push_back(std::string());
			inPaths.push_back(std::string());
		}

		ImGui::InputText("Input map1.bsp file", &inPaths[0]);
		ImGui::InputText("Input map2.bsp file", &inPaths[1]);
		int p = 1;
		while (inPaths[p].size())
		{
			p++;
			if (inPaths.size() < p)
				inPaths.push_back(std::string());
			ImGui::InputText("Input map2.bsp file", &inPaths[p]);
		}

		ImGui::InputText("output .bsp file", &outPath);
		ImGui::Checkbox("Delete unused info", &DeleteUnusedInfo);
		ImGui::Checkbox("Optimize", &Optimize);
		ImGui::Checkbox("No hull 2", &DeleteHull2);
		ImGui::Checkbox("No ripent", &NoRipent);
		ImGui::Checkbox("No script", &NoScript);

		if (ImGui::Button("Merge maps", ImVec2(120, 0)))
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
				logf("ERROR: at least 2 input maps are required\n");
			}
			else
			{
				for (int i = 0; i < maps.size(); i++)
				{
					logf("Preprocessing {}:\n", maps[i]->bsp_name);
					if (DeleteUnusedInfo)
					{
						logf("    Deleting unused data...\n");
						STRUCTCOUNT removed = maps[i]->remove_unused_model_structures();
						g_progress.clear();
						removed.print_delete_stats(2);
					}

					if (DeleteHull2 || (Optimize && !maps[i]->has_hull2_ents()))
					{
						logf("    Deleting hull 2...\n");
						maps[i]->delete_hull(2, 1);
						maps[i]->remove_unused_model_structures().print_delete_stats(2);
					}

					if (Optimize)
					{
						logf("    Optmizing...\n");
						maps[i]->delete_unused_hulls().print_delete_stats(2);
					}

					logf("\n");
				}
				BspMerger merger;
				Bsp* result = merger.merge(maps, vec3(), outPath, NoRipent, NoScript);

				logf("\n");
				if (result->isValid()) result->write(outPath);
				logf("\n");
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
					logf("Error while map merge!\n");
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


		ImGui::InputText(".bsp file", &mapPath);
		ImGui::SameLine();

		if (ImGui::Button("...##open_bsp_file1"))
		{
			ifd::FileDialog::Instance().Open("BspOpenDialog", "Opep bsp model", "BSP file (*.bsp){.bsp},.*", false, g_settings.lastdir);
		}

		if (ImGui::Button("Load", ImVec2(120, 0)))
		{
			fixupPath(mapPath, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);
			if (fileExists(mapPath))
			{
				logf("Loading new map file from {} path.\n", mapPath);
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
					BspRenderer* mapRenderer = new BspRenderer(bspModel, NULL, NULL, NULL, NULL);
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
							logf("Fatal Error! No worldspawn found!\n");
						}
						else
						{
							logf("Binding .bsp model to func_breakable.\n");
							Entity* tmpEnt = new Entity("func_breakable");
							tmpEnt->setOrAddKeyvalue("gibmodel", std::string("models/") + basename(mapPath));
							tmpEnt->setOrAddKeyvalue("model", std::string("models/") + basename(mapPath));
							tmpEnt->setOrAddKeyvalue("spawnflags", "1");
							tmpEnt->setOrAddKeyvalue("origin", cameraOrigin.toKeyvalueString());
							map->ents.push_back(tmpEnt);
							map->update_ent_lump();
							logf("Success! Now you needs to copy model to path: {}\n", std::string("models/") + basename(mapPath));
							app->updateEnts();
							app->reloadBspModels();
						}
						delete model;
					}
				}
			}
			else
			{
				logf("No file found! Try again!\n");
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
			ImGui::Text("No map selected");
		}
		else
		{
			if (ImGui::BeginTabBar("##tabs"))
			{
				if (ImGui::BeginTabItem("Summary"))
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

					ImGui::BeginChild("content");
					ImGui::Dummy(ImVec2(0, 10));
					ImGui::PushFont(consoleFontLarge);

					float midWidth = consoleFontLarge->CalcTextSizeA(fontSize * 1.1f, FLT_MAX, FLT_MAX, "    Current / Max    ").x;
					float otherWidth = (ImGui::GetWindowWidth() - midWidth) / 2;
					ImGui::Columns(3);
					ImGui::SetColumnWidth(0, otherWidth);
					ImGui::SetColumnWidth(1, midWidth);
					ImGui::SetColumnWidth(2, otherWidth);

					ImGui::Text("Data Type"); ImGui::NextColumn();
					ImGui::Text(" Current / Max"); ImGui::NextColumn();
					ImGui::Text("Fullness"); ImGui::NextColumn();

					ImGui::Columns(1);
					ImGui::Separator();
					ImGui::BeginChild("chart");
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

				if (ImGui::BeginTabItem("Clipnodes"))
				{
					drawLimitTab(map, SORT_CLIPNODES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Nodes"))
				{
					drawLimitTab(map, SORT_NODES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Faces"))
				{
					drawLimitTab(map, SORT_FACES);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Vertices"))
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

	ImGui::BeginChild("content");
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

	ImGui::Text("Classname"); ImGui::NextColumn();
	ImGui::Text("Model"); ImGui::NextColumn();
	ImGui::Text(countName); ImGui::NextColumn();
	ImGui::Text("Usage"); ImGui::NextColumn();

	ImGui::Columns(1);
	ImGui::Separator();
	ImGui::BeginChild("chart");
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
			ImGui::Text("No map selected");
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
			ImGui::BeginChild("entlist", ImVec2(0.f, -footerHeight));

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
								ImGui::Text(fmt::format("Classname \"{}\" not found in fgd files!", cname).c_str());
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
							ImGui::OpenPopup("ent_context");
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

			ImGui::BeginChild("filters");

			ImGui::Separator();
			ImGui::Dummy(ImVec2(0, 8));

			static std::vector<std::string> usedClasses;
			static std::set<std::string> uniqueClasses;

			static bool comboWasOpen = false;

			ImGui::SetNextItemWidth(280);
			ImGui::Text("Classname Filter");
			ImGui::SameLine(280);
			ImGui::Text("Flags Filter");
			ImGui::SetNextItemWidth(270);
			if (ImGui::BeginCombo("##classfilter", classFilter.c_str()))
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
			if (ImGui::BeginCombo("##flagsfilter", flagsFilter.c_str()))
			{
				if (app->fgd)
				{
					if (ImGui::Selectable("(none)", false))
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
			ImGui::Text("Keyvalue Filter");

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
					if (ImGui::Button("Add", ImVec2(100, 0)))
					{
						MAX_FILTERS++;
						break;
					}
				}

				if (i == 1)
				{
					ImGui::SameLine();
					if (ImGui::Button("Del", ImVec2(100, 0)))
					{
						if (MAX_FILTERS > 1)
							MAX_FILTERS--;
						break;
					}
				}
			}

			if (ImGui::Checkbox("Partial Matching", &partialMatches))
			{
				filterNeeded = true;
			}

			ImGui::SameLine();

			if (ImGui::Button("GO TO ENT"))
			{
				app->goToEnt(map, app->pickInfo.GetSelectedEnt());
			}

			ImGui::SameLine();

			if (ImGui::Button("SHOW ENT"))
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
	ImGui::ColorConvertRGBtoHSV(
		color.Value.x, color.Value.y, color.Value.z, hue, saturation, value);

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

	ImGui::InvisibleButton("saturation_value_selector", SV_PICKER_SIZE);

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
	ImGui::InvisibleButton("hue_selector", ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

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
		ImGui::InvisibleButton("alpha_selector", ImVec2(HUE_PICKER_WIDTH, SV_PICKER_SIZE.y));

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
		logf("Import all {} faces...", map->faceCount);
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
		//logf("\nImport {} ligtmap\n", lightId);
		std::string filename = fmt::format("{}{}Full{}Style.png", GetWorkDir().c_str(), "lightmap", lightId);
		unsigned char* image_bytes;
		unsigned int w2, h2;
		auto error = lodepng_decode24_file(&image_bytes, &w2, &h2, filename.c_str());

		if (error == 0 && image_bytes)
		{
			/*for (int i = 0; i < 100; i++)
			{
				logf("{}/", image_bytes[i]);
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
		logf("Export {} faces.\n", (unsigned int)app->pickInfo.selectedFaces.size());
		faces_to_export = app->pickInfo.selectedFaces;
	}
	else
	{
		logf("Export ALL {} faces.\n", map->faceCount);
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

		//logf("\nExport {} ligtmap\n", lightId);
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
			filename = fmt::format("{}{}Full{}Style.png", GetWorkDir().c_str(), "lightmap", lightId);
			logf("Exporting to {} file\n", filename);
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
		filename = fmt::format("{}{}_FACE{}-STYLE{}.png", GetWorkDir().c_str(), "lightmap", faceIdx, i);
		logf("Exporting {}\n", filename);
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
		filename = fmt::format("{}{}_FACE{}-STYLE{}.png", GetWorkDir().c_str(), "lightmap", faceIdx, i);
		unsigned int w = size[0], h = size[1];
		unsigned int w2 = 0, h2 = 0;
		logf("Importing {}\n", filename);
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
				logf("Invalid lightmap size! Need {}x{} 24bit png!\n", w, h);
			}
			free(image_bytes);
		}
		else
		{
			logf("Invalid lightmap image format. Need 24bit png!\n");
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
			ImGui::TextDisabled("Pick color : ");
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
						//logf("upload {} style at offset {}\n", i, offset);
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
			ImGui::Text(fmt::format("Lightmap width:{} height:{}", size[0], size[1]).c_str());
			ImGui::Separator();
			ColorPicker3(imgui_io, colourPatch);
			ImGui::SetNextItemWidth(100.f);
			if (ImGui::Button("Pick color", ImVec2(120, 0)))
			{
				needPickColor = true;
			}
			ImGui::Separator();
			ImGui::SetNextItemWidth(100.f);
			ImGui::Combo(" Disable light", &type, light_names, IM_ARRAYSIZE(light_names));
			map->getBspRender()->showLightFlag = type - 1;
			ImGui::Separator();
			if (ImGui::Button("Save", ImVec2(120, 0)))
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
			if (ImGui::Button("Reload", ImVec2(120, 0)))
			{
				showLightmapEditorUpdate = true;
			}

			ImGui::Separator();
			if (ImGui::Button("Export", ImVec2(120, 0)))
			{
				logf("Export lightmaps to png files...\n");
				createDir(GetWorkDir());
				ExportLightmap(face, faceIdx, map);
			}
			ImGui::SameLine();
			if (ImGui::Button("Import", ImVec2(120, 0)))
			{
				logf("Import lightmaps from png files...\n");
				ImportLightmap(face, faceIdx, map);
				showLightmapEditorUpdate = true;
				map->getBspRender()->reloadLightmaps();
			}
			ImGui::Separator();

			ImGui::Text("WARNING! SAVE MAP\nBEFORE NEXT ACTION!");
			ImGui::Separator();
			if (ImGui::Button("Export ALL", ImVec2(125, 0)))
			{
				logf("Export lightmaps to png files...\n");
				createDir(GetWorkDir());

				//for (int z = 0; z < map->faceCount; z++)
				//{
				//	lightmaps = 0;
				//	ExportLightmaps(map->faces[z], z, map);
				//}

				ExportOneBigLightmap(map);
			}
			ImGui::SameLine();
			if (ImGui::Button("Import ALL", ImVec2(125, 0)))
			{
				logf("Import lightmaps from png files...\n");

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
			ImGui::Text("No face selected");
		}
	}
	ImGui::End();
}
void Gui::drawFaceEditorWidget()
{
	ImGui::SetNextWindowSize(ImVec2(300.f, 570.f), ImGuiCond_FirstUseEver);
	//ImGui::SetNextWindowSize(ImVec2(400, 600));
	if (ImGui::Begin("Face Editor", &showFaceEditWidget))
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

		static float verts_merge_epsilon = 1.0f;

		static int tmpStyles[4] = { 255,255,255,255 };
		static bool stylesChanged = false;

		Bsp* map = app->getSelectedMap();
		if (!map || app->pickMode != PICK_FACE || app->pickInfo.selectedFaces.empty())
		{
			ImGui::Text("No face selected");
			ImGui::End();
			return;
		}
		BspRenderer* mapRenderer = map->getBspRender();
		if (!mapRenderer || !mapRenderer->texturesLoaded)
		{
			ImGui::Text("Loading textures...");
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
			ImGui::Text(fmt::format("Lightmap size {} / {} ( {} )", lmSize[0], lmSize[1], lmSize[0] * lmSize[1]).c_str());


		ImGui::Text("Scale");

		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Almost always breaks lightmaps if changed.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat("##scalex", &scaleX, 0.001f, 0, 0, "X: %.3f") && scaleX != 0)
		{
			scaledX = true;
		}
		ImGui::SameLine();
		if (ImGui::DragFloat("##scaley", &scaleY, 0.001f, 0, 0, "Y: %.3f") && scaleY != 0)
		{
			scaledY = true;
		}

		ImGui::Dummy(ImVec2(0, 8));

		ImGui::Text("Shift");

		ImGui::SameLine();
		ImGui::TextDisabled("(WIP)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted("Sometimes breaks lightmaps if changed.");
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}

		if (ImGui::DragFloat("##shiftx", &shiftX, 0.1f, 0, 0, "X: %.3f"))
		{
			shiftedX = true;
		}
		ImGui::SameLine();
		if (ImGui::DragFloat("##shifty", &shiftY, 0.1f, 0, 0, "Y: %.3f"))
		{
			shiftedY = true;
		}

		ImGui::PopItemWidth();

		inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.3f;
		ImGui::PushItemWidth(inputWidth);

		ImGui::Text("Advanced settings");
		ImGui::SameLine();
		ImGui::TextDisabled("[ANGLES] (WIP)");

		if (ImGui::DragFloat("##rotateX", &rotateX, 0.01f, 0, 0, "X: %.3f"))
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

		if (ImGui::DragFloat("##rotateY", &rotateY, 0.01f, 0, 0, "Y: %.3f"))
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

		ImGui::Checkbox("Lock Angles", &lockRotate);

		if (app->pickInfo.selectedFaces.size() == 1)
		{
			ImGui::Separator();
			ImGui::Text("Face Styles");
			if (ImGui::DragInt("# 1:", &tmpStyles[0], 1, 0, 255)) stylesChanged = true;
			ImGui::SameLine();
			if (ImGui::DragInt("# 2:", &tmpStyles[1], 1, 0, 255)) stylesChanged = true;
			if (ImGui::DragInt("# 3:", &tmpStyles[2], 1, 0, 255)) stylesChanged = true;
			ImGui::SameLine();
			if (ImGui::DragInt("# 4:", &tmpStyles[3], 1, 0, 255)) stylesChanged = true;
			ImGui::Separator();
			ImGui::Text("Expert settings");
			ImGui::SameLine();
			ImGui::TextDisabled("[VERTS] (WIP)");

			std::string tmplabel = "##unklabel";

			BSPFACE32 face = map->faces[app->pickInfo.selectedFaces[0]];
			int edgeIdx = 0;
			for (auto& v : edgeVerts)
			{
				edgeIdx++;
				tmplabel = fmt::format("##edge{}1", edgeIdx);
				if (ImGui::DragFloat(tmplabel.c_str(), &v.x, 0.1f, 0, 0, "T1: %.3f"))
				{
					updatedFaceVec = true;
				}

				tmplabel = fmt::format("##edge{}2", edgeIdx);
				ImGui::SameLine();
				if (ImGui::DragFloat(tmplabel.c_str(), &v.y, 0.1f, 0, 0, "T2: %.3f"))
				{
					updatedFaceVec = true;
				}

				tmplabel = fmt::format("##edge{}3", edgeIdx);
				ImGui::SameLine();
				if (ImGui::DragFloat(tmplabel.c_str(), &v.z, 0.1f, 0, 0, "T3: %.3f"))
				{
					updatedFaceVec = true;
				}

			}

			//map->edges[0].iVertex
		}

		if (app->pickInfo.selectedFaces.size() > 1)
		{
			ImGui::Separator();
			ImGui::Text("Optimize verts");
			ImGui::DragFloat("Merge power:##epsilon", &verts_merge_epsilon, 0.1f, 0.0f, 1000.0f);
			if (ImGui::Button("Merge verts!"))
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
							if (map->verts[v].z != vec.z && VectorCompare(map->verts[v], vec, verts_merge_epsilon))
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
				map->remove_unused_model_structures(CLEAN_VERTICES);
				map->getBspRender()->reload();
			}
			ImGui::Separator();
		}

		ImGui::PopItemWidth();


		ImGui::Text("Flags");
		if (ImGui::Checkbox("Special", &isSpecial))
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

		ImGui::Text("Texture");
		ImGui::SetNextItemWidth(inputWidth);
		if (!validTexture)
		{
			ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(0, 0.6f, 0.6f));
		}

		if (ImGui::InputText("##texname", textureName, MAXTEXTURENAME))
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
		ImGui::Text("%.0fx%.0f", width, height);
		if (!ImGui::IsMouseDown(ImGuiMouseButton_::ImGuiMouseButton_Left) &&
			(updatedFaceVec || scaledX || scaledY || shiftedX || shiftedY || textureChanged || stylesChanged || refreshSelectedFaces || toggledFlags || updatedTexVec))
		{
			unsigned int newMiptex = 0;
			pickCount++;
			map->getBspRender()->saveLumpState(0xffffffff, false);
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

			checkFaceErrors();
			updatedFaceVec = scaledX = scaledY = shiftedX = shiftedY =
				textureChanged = toggledFlags = updatedTexVec = stylesChanged = false;

			map->getBspRender()->pushModelUndoState("Edit Face", EDIT_MODEL_LUMPS);

			mapRenderer->updateLightmapInfos();
			mapRenderer->calcFaceMaths();
			app->updateModelVerts();

			reloadLimits();
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

		tmp = fmt::format("{:5.2f} MB", max / meg);
		stat.max = std::string(tmp);
	}
	else
	{
		tmp = fmt::format("{:8}", val);
		stat.val = std::string(tmp);

		tmp = fmt::format("{:8}", max);
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

		tmp = fmt::format("{:-5.1f} MB", max / meg);
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
			//logf("Bad surface extents ({} x {})\n", size[0], size[1]);
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
