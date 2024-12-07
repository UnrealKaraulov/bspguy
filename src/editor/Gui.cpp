#include "lang.h"
#include "Gui.h"
#include "Renderer.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Settings.h"
#include "BspMerger.h"
#include "filedialog/ImFileDialog.h"
#include "imgui_stdlib.h"
#include "quantizer.h"
#include "vis.h"
#include "winding.h"
#include "util.h"
#include "log.h"

#include <lodepng.h>
#include <execution>
#ifndef WIN_XP_86
#include <ranges>
#endif
#include <algorithm>
#include "LeafNavMesh.h"

#include "as.h"
float g_tooltip_delay = 0.6f; // time in seconds before showing a IMGUI_TOOLTIP

bool filterNeeded = true;

std::string iniPath = "./imgui.ini";

enum umd_flags : unsigned int {
	UMD_TEXTURES_SKIP_OPTIMIZE = 1 << 0,
	UMD_OPTIMIZE_DISABLED = 1 << 1
};

enum cell_type :unsigned char
{
	cell_none = 0,
	cell_brush,
	cell_wall,
	cell_hostage,
	cell_player_TT,
	cell_player_CT,
	cell_light,
	cell_buyzone,
	cell_bombzone,
	cell_waterzone
};

int UMD_MAGIC = 'umd2';

struct cell
{
	unsigned char height;
	unsigned char height_offset;
	unsigned char texid;
	cell_type type;
};


int cell_idx(const vec3& pos, const vec3& mins, float cell_size, int cell_x, int cell_y, int cell_layers, int layer) {
	int y = static_cast<int>(floatRound((pos.x - mins.x) / cell_size));
	int x = static_cast<int>(floatRound((pos.y - mins.y) / cell_size));
	int lvl = static_cast<int>(floatRound((pos.z - mins.z) / cell_size));
	int index = (lvl * cell_layers + layer) * cell_y * cell_x + y * cell_x + x;
	return index < 0 ? 0 : index;
}

void IMGUI_TOOLTIP(ImGuiContext& g, const std::string& IMGUI_TOOLTIP)
{
	if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
		ImGui::BeginTooltip();
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
		ImGui::TextUnformatted(IMGUI_TOOLTIP.c_str());
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

Gui::Gui(Renderer* app)
{
	guiHoverAxis = 0;
	this->app = app;
}

void Gui::init()
{
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
		return (void*)(size_t)tex;
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

	lodepng_decode32_file(&icon_data, &w, &h, "./pictures/object.png");
	objectIconTexture = new Texture(w, h, icon_data, "objIcon", true);
	objectIconTexture->upload();
	lodepng_decode32_file(&icon_data, &w, &h, "./pictures/face.png");
	faceIconTexture = new Texture(w, h, icon_data, "faceIcon", true);
	faceIconTexture->upload();
	lodepng_decode32_file(&icon_data, &w, &h, "./pictures/leaf.png");
	leafIconTexture = new Texture(w, h, icon_data, "leafIcon", true);
	leafIconTexture->upload();
}

ImVec4 imguiColorFromConsole(unsigned int colors)
{
	bool intensity = (colors & PRINT_INTENSITY) != 0;
	float red = (colors & PRINT_RED) ? (intensity ? 1.0f : 0.5f) : 0.0f;
	float green = (colors & PRINT_GREEN) ? (intensity ? 1.0f : 0.5f) : 0.0f;
	float blue = (colors & PRINT_BLUE) ? (intensity ? 1.0f : 0.5f) : 0.0f;
	return ImVec4(red, green, blue, 1.0f);
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

	if (showLogWidget)
	{
		drawLog();
	}

	if (showHelpWidget)
	{
		drawHelp();
	}

	if (showAboutWidget)
	{
		drawAbout();
	}

	if (showSettingsWidget)
	{
		drawSettings();
	}

	Bsp* map = app->getSelectedMap();
	if (map && map->is_mdl_model && map->map_mdl)
	{
		drawMDLWidget();
	}
	else
	{
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
		if (showOverviewWidget)
		{
			drawOverviewWidget();
		}
		if (showTransformWidget)
		{
			drawTransformWidget();
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

		if (openEmptyContext != -2)
		{
			if (app->pickMode == PICK_OBJECT)
			{
				if (openEmptyContext == 0)
				{
					ImGui::OpenPopup("empty_context");
				}
				else
				{
					ImGui::OpenPopup("ent_context");
				}
			}
			else
			{
				ImGui::OpenPopup("face_context");
			}
			openEmptyContext = -2;
		}

		drawBspContexMenu();
	}

	app->anyPopupOpened = imgui_io->WantCaptureMouse;

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

		ImGui_ImplOpenGL3_CreateFontsTexture();
	}
}

void Gui::openContextMenu(bool empty)
{
	openEmptyContext = empty ? 0 : 1;
}

void Gui::copyTexture()
{
	Bsp* map = app->getSelectedMap();
	if (!map)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0313));
		return;
	}
	else if (app->pickInfo.selectedFaces.size() == 0)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0314));
		return;
	}

	std::string outfaces;
	for (const auto& f : app->pickInfo.selectedFaces)
	{
		outfaces += std::to_string(f) + " ";
	}
	if (outfaces.size())
	{
		outfaces.pop_back();
		ImGui::SetClipboardText(outfaces.c_str());
	}

	BSPTEXTUREINFO& texinfo = map->texinfos[map->faces[app->pickInfo.selectedFaces[0]].iTextureInfo];
	copiedMiptex = texinfo.iMiptex == -1 || texinfo.iMiptex >= map->textureCount ? 0 : texinfo.iMiptex;
}

void Gui::pasteTexture()
{
	pasteTextureNow = true;
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

	copiedLightmap.face = (int)app->pickInfo.selectedFaces[0];

	int size[2];
	map->GetFaceLightmapSize(copiedLightmap.face, size);
	copiedLightmap.width = size[0];
	copiedLightmap.height = size[1];
	copiedLightmap.layers = map->lightmap_count((int)app->pickInfo.selectedFaces[0]);
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
	int faceIdx = (int)app->pickInfo.selectedFaces[0];

	int size[2];
	map->GetFaceLightmapSize(faceIdx, size);

	if (size[0] != copiedLightmap.width || size[1] != copiedLightmap.height)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, "WARNING: lightmap sizes don't match ({}x{} != {}{})",
			copiedLightmap.width,
			copiedLightmap.height,
			size[0],
			size[1]);
	}

	BSPFACE32& src = map->faces[copiedLightmap.face];
	BSPFACE32& dst = map->faces[faceIdx];
	memcpy(dst.nStyles, src.nStyles, MAX_LIGHTMAPS);
	dst.nLightmapOffset = src.nLightmapOffset;
}

int ImportModel(Bsp* map, const std::string& mdl_path, bool noclip)
{
	if (!map || !map->getBspRender())
		return -1;
	if (!fileExists(mdl_path))
		return -1;
	Bsp* bspModel = new Bsp(mdl_path);
	bspModel->setBspRender(map->getBspRender());

	std::vector<BSPPLANE> newPlanes;
	std::vector<vec3> newVerts;
	std::vector<BSPEDGE32> newEdges;
	std::vector<int> newSurfedges;
	std::vector<BSPTEXTUREINFO> newTexinfo;
	std::vector<BSPFACE32> newFaces;
	std::vector<COLOR3> newLightmaps;
	std::vector<BSPNODE32> newNodes;
	std::vector<BSPCLIPNODE32> newClipnodes;
	std::vector<WADTEX*> newTextures;
	std::vector<BSPLEAF32> newLeaves;
	std::vector<int> newMarkSurfaces;

	STRUCTREMAP remap(bspModel);
	STRUCTUSAGE usage(bspModel);
	bspModel->copy_bsp_model(0, map, remap, usage, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces,
		newLightmaps, newNodes, newClipnodes, newTextures, newLeaves, newMarkSurfaces, true);

	if (!noclip && newClipnodes.size())
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
	if (newLeaves.size())
	{
		map->append_lump(LUMP_LEAVES, &newLeaves[0], sizeof(BSPLEAF32) * newLeaves.size());
	}
	if (newMarkSurfaces.size())
	{
		map->append_lump(LUMP_MARKSURFACES, &newMarkSurfaces[0], sizeof(int) * newMarkSurfaces.size());
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

	if (newTextures.size())
	{
		while (newTextures.size())
		{
			auto& tex = newTextures[newTextures.size() - 1];
			if (tex->data)
			{
				auto data = ConvertWadTexToRGB(tex);
				map->add_texture(tex->szName, (unsigned char*)data, tex->nWidth, tex->nHeight);
				delete[]data;
			}
			else
			{
				map->add_texture(tex->szName, NULL, tex->nWidth, tex->nHeight);
			}
			delete tex;
			newTextures.pop_back();
		}
	}

	map->update_lump_pointers();

	if (newTexinfo.size())
	{
		for (auto& texinfo : newTexinfo)
		{
			if (texinfo.iMiptex < 0 || texinfo.iMiptex >= bspModel->textureCount)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
				continue;
			}
			int newMiptex = -1;
			int texOffset = ((int*)bspModel->textures)[texinfo.iMiptex + 1];
			if (texOffset < 0)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
				continue;
			}
			BSPMIPTEX& tex = *((BSPMIPTEX*)(bspModel->textures + texOffset));
			for (int i = map->textureCount - 1; i >= 0; i--)
			{
				int tex2Offset = ((int*)map->textures)[i + 1];
				if (tex2Offset >= 0)
				{
					BSPMIPTEX* tex2 = ((BSPMIPTEX*)(map->textures + tex2Offset));
					if (strcasecmp(tex.szName, tex2->szName) == 0)
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

						newMiptex = map->add_texture(tex.szName, (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight);

						delete wadTex;
						delete[] imageData;
						break;
					}
				}
			}
			texinfo.iMiptex = newMiptex;
			if (newMiptex < 0)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
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
	map->models[newModelIdx] = bspModel->models[0];

	map->models[newModelIdx].iFirstFace = remap.faces[map->models[newModelIdx].iFirstFace];
	map->models[newModelIdx].iHeadnodes[0] = map->models[newModelIdx].iHeadnodes[0] < 0 ? -1 : remap.nodes[map->models[newModelIdx].iHeadnodes[0]];

	if (!noclip)
	{
		for (int i = 1; i < MAX_MAP_HULLS; i++)
		{
			map->models[newModelIdx].iHeadnodes[i] = map->models[newModelIdx].iHeadnodes[i] < 0 ? -1 : remap.clipnodes[map->models[newModelIdx].iHeadnodes[i]];
		}
	}
	else
	{
		for (int i = 1; i < MAX_MAP_HULLS; i++)
		{
			map->models[newModelIdx].iHeadnodes[i] = CONTENTS_EMPTY;
		}
	}

	//if (map->models[newModelIdx].nVisLeafs > 0 && map->models[newModelIdx].nVisLeafs > newLeaves.size())
	//{
	//	map->models[newModelIdx].nVisLeafs--;
	//}
	//else if (map->models[newModelIdx].nVisLeafs > newLeaves.size())
	//{
	//	map->leafCount--;
	//	map->bsp_header.lump[LUMP_LEAVES].nLength -= sizeof(BSPLEAF32);
	//}
	//else if (newLeaves.size() > map->models[newModelIdx].nVisLeafs)
	//{
	//	map->models[newModelIdx].nVisLeafs++;
	//}

	bspModel->setBspRender(NULL);
	delete bspModel;

	g_app->deselectObject();

	map->save_undo_lightmaps();
	map->resize_all_lightmaps();

	BspRenderer* rend = map->getBspRender();

	rend->reuploadTextures();

	rend->loadLightmaps();
	rend->refreshModel(newModelIdx);
	rend->preRenderEnts();

	map->getBspRender()->pushModelUndoState("IMPORT MODEL", EDIT_MODEL_LUMPS | FL_ENTITIES);

	return newModelIdx;
}

void ExportModel(Bsp* src_map, const std::string& export_path, int model_id, int ExportType, bool movemodel)
{
	Bsp* bspModel = new Bsp();
	bspModel->setBspRender(src_map->getBspRender());
	bspModel->bsp_valid = true;

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		bspModel->bsp_header.lump[i].nOffset = 0;
		bspModel->bsp_header.lump[i].nLength = 0;
		bspModel->replacedLump[i] = false;
	}

	bspModel->replace_lump(LUMP_TEXTURES,
		new unsigned char[sizeof(int)] {0, 0, 0, 0}, sizeof(int));
	bspModel->textureCount = 0;

	bspModel->ents.clear();
	bspModel->ents.push_back(new Entity("worldspawn"));

	int src_entId = src_map->get_ent_from_model(0);

	if (src_entId >= 0)
	{
		if (src_map->ents[src_entId]->hasKey("wad"))
		{
			bspModel->ents[bspModel->ents.size() - 1]->setOrAddKeyvalue("wad", src_map->ents[src_entId]->keyvalues["wad"]);
			bspModel->ents[bspModel->ents.size() - 1]->setOrAddKeyvalue("message", "bsp model");
		}
	}

	bspModel->update_ent_lump();

	/*bspModel->create_node(true);
	bspModel->create_clipnode(true);
	bspModel->create_leaf_back(CONTENTS_SOLID);*/

	bspModel->create_node();
	bspModel->create_clipnode();
	bspModel->create_leaf(CONTENTS_SOLID);

	std::vector<BSPPLANE> newPlanes;
	std::vector<vec3> newVerts;
	std::vector<BSPEDGE32> newEdges;
	std::vector<int> newSurfedges;
	std::vector<BSPTEXTUREINFO> newTexinfo;
	std::vector<BSPFACE32> newFaces;
	std::vector<COLOR3> newLightmaps;
	std::vector<BSPNODE32> newNodes;
	std::vector<BSPCLIPNODE32> newClipnodes;
	std::vector<WADTEX*> newTextures;
	std::vector<BSPLEAF32> newLeaves;
	std::vector<int> newMarkSurfaces;

	STRUCTREMAP remap(src_map);
	STRUCTUSAGE usage(src_map);

	src_map->copy_bsp_model(model_id, bspModel, remap, usage, newPlanes, newVerts, newEdges, newSurfedges, newTexinfo, newFaces,
		newLightmaps, newNodes, newClipnodes, newTextures, newLeaves, newMarkSurfaces, true);

	if (newEdges.size())
	{
		bspModel->append_lump(LUMP_EDGES, &newEdges[0], sizeof(BSPEDGE32) * newEdges.size());
	}
	if (newFaces.size())
	{
		bspModel->append_lump(LUMP_FACES, &newFaces[0], sizeof(BSPFACE32) * newFaces.size());
	}
	if (newLeaves.size())
	{
		bspModel->append_lump(LUMP_LEAVES, &newLeaves[0], sizeof(BSPLEAF32) * newLeaves.size());
	}
	if (newMarkSurfaces.size())
	{
		bspModel->append_lump(LUMP_MARKSURFACES, &newMarkSurfaces[0], sizeof(int) * newMarkSurfaces.size());
	}

	if (newNodes.size())
	{
		bspModel->append_lump(LUMP_NODES, &newNodes[0], sizeof(BSPNODE32) * newNodes.size());
	}

	if (newClipnodes.size())
	{
		bspModel->append_lump(LUMP_CLIPNODES, &newClipnodes[0], sizeof(BSPCLIPNODE32) * newClipnodes.size());
	}

	if (newPlanes.size())
	{
		bspModel->append_lump(LUMP_PLANES, &newPlanes[0], sizeof(BSPPLANE) * newPlanes.size());
	}
	if (newSurfedges.size())
	{
		bspModel->append_lump(LUMP_SURFEDGES, &newSurfedges[0], sizeof(int) * newSurfedges.size());
	}

	if (newTextures.size())
	{
		while (newTextures.size())
		{
			auto& tex = newTextures[newTextures.size() - 1];
			if (tex->data && ExportType != 0)
			{
				auto data = ConvertWadTexToRGB(tex);
				int mip = bspModel->add_texture(tex->szName, (unsigned char*)data, tex->nWidth, tex->nHeight);
				delete[]data;
				data = ConvertMipTexToRGB(bspModel->find_embedded_texture(tex->szName, mip));
				delete[]data;
			}
			else
			{
				bspModel->add_texture(tex->szName, NULL, tex->nWidth, tex->nHeight);
			}
			delete tex;
			newTextures.pop_back();
		}
	}

	bspModel->update_lump_pointers();

	if (newTexinfo.size())
	{
		for (auto& texinfo : newTexinfo)
		{
			if (texinfo.iMiptex < 0 || texinfo.iMiptex >= src_map->textureCount)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
				continue;
			}
			int newMiptex = -1;
			int texOffset = ((int*)src_map->textures)[texinfo.iMiptex + 1];
			if (texOffset < 0)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
				continue;
			}
			BSPMIPTEX& tex = *((BSPMIPTEX*)(src_map->textures + texOffset));
			for (int i = bspModel->textureCount - 1; i >= 0; i--)
			{
				int tex2Offset = ((int*)bspModel->textures)[i + 1];
				if (tex2Offset >= 0)
				{
					BSPMIPTEX* tex2 = ((BSPMIPTEX*)(bspModel->textures + tex2Offset));
					if (strcasecmp(tex.szName, tex2->szName) == 0)
					{
						newMiptex = i;
						break;
					}
				}
			}
			if (newMiptex < 0 && src_map->getBspRender() && src_map->getBspRender()->wads.size())
			{
				for (auto& s : src_map->getBspRender()->wads)
				{
					if (s->hasTexture(tex.szName))
					{
						WADTEX* wadTex = s->readTexture(tex.szName);
						if (ExportType != 0)
						{
							COLOR3* imageData = ConvertWadTexToRGB(wadTex);
							newMiptex = src_map->add_texture(tex.szName, (unsigned char*)imageData, wadTex->nWidth, wadTex->nHeight);
							delete[] imageData;
						}
						else
						{
							newMiptex = src_map->add_texture(tex.szName, NULL, wadTex->nWidth, wadTex->nHeight);
						}

						delete wadTex;
						break;
					}
				}
			}
			texinfo.iMiptex = newMiptex;
			if (newMiptex < 0)
			{
				texinfo.iMiptex = 0;
				texinfo.nFlags = TEX_SPECIAL;
			}
		}
		bspModel->append_lump(LUMP_TEXINFO, &newTexinfo[0], sizeof(BSPTEXTUREINFO) * newTexinfo.size());
	}

	if (newVerts.size())
	{
		bspModel->append_lump(LUMP_VERTICES, &newVerts[0], sizeof(vec3) * newVerts.size());
	}
	if (newLightmaps.size())
	{
		bspModel->append_lump(LUMP_LIGHTING, &newLightmaps[0], sizeof(COLOR3) * newLightmaps.size());
	}

	int newModelIdx = bspModel->create_model();

	bspModel->models[newModelIdx] = src_map->models[model_id];
	bspModel->models[newModelIdx].iFirstFace = remap.faces[bspModel->models[newModelIdx].iFirstFace];
	bspModel->models[newModelIdx].iHeadnodes[0] = remap.nodes[bspModel->models[newModelIdx].iHeadnodes[0]];
	for (int i = 1; i < MAX_MAP_HULLS; i++)
	{
		bspModel->models[newModelIdx].iHeadnodes[i] = remap.clipnodes[bspModel->models[newModelIdx].iHeadnodes[i]];
	}

	/*STRUCTCOUNT removed = bspModel->remove_unused_model_structures();
	if (!removed.allZero())
		removed.print_delete_stats(1);*/

	if (movemodel)
	{
		vec3 modelOrigin = src_map->get_model_center(model_id);
		print_log(get_localized_string(LANG_0325));
		bspModel->move(-modelOrigin, 0, true, true);
	}

	if (ExportType != 0)
	{
		print_log(get_localized_string(LANG_0326));
		bspModel->update_lump_pointers();
		update_unused_wad_files(src_map, bspModel, ExportType);
	}

	if (ExportType == 1)
	{
		bspModel->is_bsp29 = true;
		bspModel->is_texture_has_pal = false;
		bspModel->target_save_texture_has_pal = false;
		bspModel->bsp_header.nVersion = 29;
	}
	else
	{
		bspModel->is_bsp29 = false;
		bspModel->is_texture_has_pal = true;
		bspModel->target_save_texture_has_pal = true;
		bspModel->bsp_header.nVersion = 30;
	}

	//if (src_entId >= 0)
	//{
	//	if (src_map->ents[src_entId]->classname == "func_water")
	//	{
	//		bspModel->models[0].vOrigin = getCenter(bspModel->models[0].nMins, bspModel->models[0].nMaxs);
	//	}
	//}

	bspModel->bsp_path = export_path;
	bspModel->write(bspModel->bsp_path);
	removeFile(bspModel->bsp_path);

	unsigned char* tmpCompressed = new unsigned char[MAX_MAP_LEAVES / 8];
	memset(tmpCompressed, 0xFF, MAX_MAP_LEAVES / 8);

	/* if something bad */
	bspModel->models[newModelIdx].nVisLeafs = bspModel->leafCount - 1;

	// ADD LEAFS TO ALL VISIBILITY BYTES
	for (int i = 0; i < bspModel->leafCount; i++)
	{
		if (bspModel->leaves[i].nVisOffset < 0)
		{
			bspModel->leaves[i].nVisOffset = bspModel->visDataLength;
			unsigned char* newVisLump = new unsigned char[bspModel->visDataLength + MAX_MAP_LEAVES / 8];
			memcpy(newVisLump, bspModel->visdata, bspModel->visDataLength);
			memcpy(newVisLump + bspModel->visDataLength, tmpCompressed, MAX_MAP_LEAVES / 8);
			bspModel->replace_lump(LUMP_VISIBILITY, newVisLump, bspModel->visDataLength + MAX_MAP_LEAVES / 8);
		}
	}
	// recompile vis lump, remove unused textures
	bspModel->remove_unused_model_structures(CLEAN_VISDATA | CLEAN_TEXTURES);

	bspModel->validate();
	bspModel->write(bspModel->bsp_path);
	bspModel->setBspRender(NULL);

	delete bspModel;
	delete[] tmpCompressed;


	//print_log(get_localized_string(LANG_0315));
	//src_map->update_ent_lump();
	//src_map->update_lump_pointers();
	//src_map->validate();
	//src_map->write(src_map->bsp_path + ".tmp.bsp");


	//print_log(get_localized_string(LANG_0316));

	//Bsp* tmpMap = new Bsp(src_map->bsp_path + ".tmp.bsp");

	//tmpMap->force_skip_crc = true;

	//if (ExportType == 1)
	//{
	//	tmpMap->is_bsp29 = true;
	//	tmpMap->is_texture_has_pal = false;
	//	tmpMap->bsp_header.nVersion = 29;
	//}
	//else
	//{
	//	tmpMap->is_bsp29 = false;
	//	tmpMap->is_texture_has_pal = true;
	//	tmpMap->bsp_header.nVersion = 30;
	//}

	//print_log(get_localized_string(LANG_0317));
	//removeFile(src_map->bsp_path + ".tmp.bsp");

	//vec3 modelOrigin = tmpMap->get_model_center(model_id);

	//BSPMODEL tmpModel = src_map->models[model_id];

	//if (tmpMap->modelCount < 1)
	//{
	//	print_log(get_localized_string(LANG_0318));
	//	tmpMap->create_model();
	//}

	//print_log(get_localized_string(LANG_0319));
	//tmpMap->models[0] = tmpModel;

	//for (int i = 1; i < tmpMap->ents.size(); i++)
	//{
	//	delete tmpMap->ents[i];
	//}
	//print_log(get_localized_string(LANG_0320));

	//Entity* tmpEnt = new Entity("worldspawn");

	//tmpEnt->setOrAddKeyvalue("compiler", g_version_string);
	//tmpEnt->setOrAddKeyvalue("message", "bsp model");

	//print_log(get_localized_string(LANG_0321));
	//tmpMap->modelCount = 1;
	//tmpMap->lumps[LUMP_MODELS] = (unsigned char*)tmpMap->models;
	//tmpMap->bsp_header.lump[LUMP_MODELS].nLength = sizeof(BSPMODEL);

	//tmpMap->ents.clear();
	//tmpMap->ents.push_back(tmpEnt);

	//tmpMap->update_ent_lump();
	//tmpMap->update_lump_pointers();

	//print_log(get_localized_string(LANG_0322));
	//STRUCTCOUNT removed = tmpMap->remove_unused_model_structures(CLEAN_LIGHTMAP | CLEAN_PLANES | CLEAN_NODES | CLEAN_CLIPNODES | CLEAN_CLIPNODES_SOMETHING | CLEAN_LEAVES | CLEAN_FACES | CLEAN_SURFEDGES | CLEAN_TEXINFOS |
	//	CLEAN_EDGES | CLEAN_VERTICES | CLEAN_TEXTURES | CLEAN_VISDATA);
	//if (!removed.allZero())
	//	removed.print_delete_stats(1);


	//int markid = 0;
	//for (int i = 0; i < tmpMap->leafCount; i++)
	//{
	//	BSPLEAF32& tmpLeaf = tmpMap->leaves[i];
	//	if (tmpLeaf.nMarkSurfaces > 0)
	//	{
	//		tmpLeaf.iFirstMarkSurface = markid;
	//		markid += tmpLeaf.nMarkSurfaces;
	//	}
	//}

	//tmpMap->models[0].nVisLeafs = tmpMap->leafCount - 1;

	//int totalLeaves = 1;
	//totalLeaves += tmpMap->models[0].nVisLeafs;
	//if (totalLeaves > tmpMap->leafCount)
	//{
	//	while (totalLeaves > tmpMap->leafCount)
	//		tmpMap->create_leaf(CONTENTS_EMPTY);
	//}
	//else if (totalLeaves < tmpMap->leafCount)
	//{
	//	while (totalLeaves < tmpMap->leafCount)
	//	{
	//		tmpMap->models[0].nVisLeafs++;
	//		totalLeaves++;
	//	}
	//}

	//if (movemodel)
	//{
	//	print_log(get_localized_string(LANG_0325));
	//	tmpMap->move(-modelOrigin, 0, true, true);
	//}

	//print_log(get_localized_string(LANG_0326));
	//tmpMap->update_lump_pointers();
	//update_unused_wad_files(src_map, tmpMap, ExportType);

	//if (model_id != 0)
	//{
	//	for (int i = 0; i < tmpMap->leafCount; i++)
	//	{
	//		tmpMap->leaves[i].nVisOffset = 0;
	//	}
	//	unsigned char* newVisLump = new unsigned char[MAX_MAP_LEAVES / 8];
	//	memset(newVisLump, 255, MAX_MAP_LEAVES / 8);
	//	tmpMap->replace_lump(LUMP_VISIBILITY, newVisLump, MAX_MAP_LEAVES / 8);
	//}

	//print_log(get_localized_string(LANG_0327));
	//removed = tmpMap->remove_unused_model_structures(CLEAN_LIGHTMAP | CLEAN_PLANES | CLEAN_NODES | CLEAN_CLIPNODES | CLEAN_CLIPNODES_SOMETHING | CLEAN_LEAVES | CLEAN_FACES | CLEAN_SURFEDGES | CLEAN_TEXINFOS |
	//	CLEAN_EDGES | CLEAN_VERTICES | CLEAN_TEXTURES | CLEAN_VISDATA | CLEAN_MARKSURFACES);

	//if (model_id == 0)
	//{
	//	int rowSize = (((tmpMap->leafCount - 1) + 63) & ~63) >> 3;
	//	unsigned char* tmpVisData = new unsigned char[rowSize];
	//	unsigned char* tmpCompressed = new unsigned char[MAX_MAP_LEAVES / 8];

	//	for (int i = 1; i < tmpMap->leafCount; i++)
	//	{
	//		if (tmpMap->leaves[i].nVisOffset >= 0)
	//		{
	//			memset(tmpVisData, 0, rowSize);

	//			if (tmpMap->visdata)
	//				DecompressVis(tmpMap->visdata + tmpMap->leaves[i].nVisOffset, tmpVisData, rowSize, tmpMap->leafCount - 1, tmpMap->visDataLength - tmpMap->leaves[i].nVisOffset);
	//			else
	//				memset(tmpVisData, 255, rowSize);

	//			int size = CompressVis(tmpVisData, rowSize, tmpCompressed, MAX_MAP_LEAVES / 8);

	//			tmpMap->leaves[i].nVisOffset = tmpMap->visDataLength;

	//			unsigned char* newVisLump = new unsigned char[tmpMap->visDataLength + size];
	//			if (tmpMap->visdata)
	//				memcpy(newVisLump, tmpMap->visdata, tmpMap->visDataLength);
	//			memcpy(newVisLump + tmpMap->visDataLength, tmpCompressed, size);
	//			tmpMap->replace_lump(LUMP_VISIBILITY, newVisLump, tmpMap->visDataLength + size);
	//		}
	//	}

	//	delete[] tmpVisData;
	//	delete[] tmpCompressed;
	//}

	//tmpMap->update_ent_lump();
	//tmpMap->update_lump_pointers();

	//print_log("NODE FACES: {}\n", tmpMap->nodes[tmpMap->models[0].iHeadnodes[0]].nFaces);
	//print_log("NODE MINS/MAXS: {} {} {} / {} {} {}\n", tmpMap->nodes[tmpMap->models[0].iHeadnodes[0]].nMins.x, 
	//	tmpMap->nodes[tmpMap->models[0].iHeadnodes[0]].nMins.y, tmpMap->nodes[tmpMap->models[0].iHeadnodes[0]].nMins.z,
	//	tmpMap->nodes[tmpMap->models[0].iHeadnodes[0]].nMaxs.x, tmpMap->nodes[tmpMap->models[0].iHeadnodes[0]].nMaxs.y,
	//	tmpMap->nodes[tmpMap->models[0].iHeadnodes[0]].nMaxs.z);

	//if (tmpMap->validate())
	//{
	//	createDir(g_working_dir);
	//	removeFile(g_working_dir + src_map->bsp_name + "_model" + std::to_string(model_id) + ".bsp");
	//	tmpMap->write(g_working_dir + src_map->bsp_name + "_model" + std::to_string(model_id) + ".bsp");
	//}
	//else
	//{
	//	print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0341));
	//}

	//delete tmpMap;
}


void Gui::drawBspContexMenu()
{
	ImGuiContext& g = *GImGui;

	Bsp* map = app->getSelectedMap();

	if (!map)
		return;

	BspRenderer* rend = map->getBspRender();

	if (!rend)
		return;

	auto entIdxs = app->pickInfo.selectedEnts;

	if (app->originHovered && entIdxs.size())
	{
		int entIdx = entIdxs[0];
		Entity* ent = map->ents[entIdx];
		int modelIdx = ent->getBspModelIdx();

		if (ImGui::BeginPopup("ent_context") || ImGui::BeginPopup("empty_context"))
		{
			if (modelIdx > 0 && app->transformTarget == TRANSFORM_ORIGIN)
			{
				BSPMODEL& model = map->models[modelIdx];

				if (ImGui::MenuItem(get_localized_string(LANG_0430).c_str(), ""))
				{
					map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
						map->models[modelIdx].nMaxs);
					rend->refreshModel(modelIdx);
					pickCount++; // force gui refresh
				}

				if (ImGui::BeginMenu(get_localized_string(LANG_0431).c_str()))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0432).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.z = model.nMaxs.z;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0433).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.z = model.nMins.z;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					ImGui::Separator();
					if (ImGui::MenuItem(get_localized_string(LANG_0434).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.x = model.nMins.x;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0435).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.x = model.nMaxs.x;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					ImGui::Separator();
					if (ImGui::MenuItem(get_localized_string(LANG_0436).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.y = model.nMins.y;
						rend->refreshModel(modelIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0437).c_str()))
					{
						map->models[modelIdx].vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						map->models[modelIdx].vOrigin.y = model.nMaxs.y;
						rend->refreshModel(modelIdx);
						pickCount++;
					}

					ImGui::EndMenu();
				}
			}
			else if (modelIdx > 0)
			{
				BSPMODEL& model = map->models[modelIdx];

				if (ImGui::MenuItem(get_localized_string(LANG_0430).c_str(), ""))
				{
					ent->setOrAddKeyvalue("origin", (-getCenter(map->models[modelIdx].nMins,
						map->models[modelIdx].nMaxs)).toKeyvalueString());
					rend->refreshEnt(entIdx);
					pickCount++; // force gui refresh
				}

				if (ImGui::BeginMenu(get_localized_string(LANG_0431).c_str()))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0432).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						vOrigin.z = model.nMaxs.z;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0433).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						vOrigin.z = model.nMins.z;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					ImGui::Separator();
					if (ImGui::MenuItem(get_localized_string(LANG_0434).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						vOrigin.x = model.nMins.x;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0435).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						vOrigin.x = model.nMaxs.x;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
					}
					ImGui::Separator();
					if (ImGui::MenuItem(get_localized_string(LANG_0436).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						vOrigin.y = model.nMins.y;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					if (ImGui::MenuItem(get_localized_string(LANG_0437).c_str()))
					{
						vec3 vOrigin = getCenter(map->models[modelIdx].nMins,
							map->models[modelIdx].nMaxs);
						vOrigin.y = model.nMaxs.y;
						ent->setOrAddKeyvalue("origin", (-vOrigin).toKeyvalueString());
						rend->refreshEnt(entIdx);
						pickCount++;
					}
					ImGui::EndMenu();
				}
			}
			else
			{
				ImGui::BeginDisabled();
				ImGui::MenuItem("No selected model");
				ImGui::EndDisabled();
			}
			ImGui::EndPopup();
		}

		return;
	}

	if (app->pickMode != PICK_OBJECT)
	{
		if (ImGui::BeginPopup("face_context"))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0438).c_str()))
			{
				copyTexture();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0440).c_str(), get_localized_string(LANG_0441).c_str(), false,
				copiedMiptex >= 0 && copiedMiptex < map->textureCount))
			{
				pasteTexture();
			}

			ImGui::Separator();

			if (ImGui::MenuItem(get_localized_string(LANG_0442).c_str()))
			{
				copyLightmap();
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0444).c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0445).c_str(), "", false, copiedLightmap.face >= 0 && copiedLightmap.face < map->faceCount))
			{
				pasteLightmap();
			}

			ImGui::Separator();

			if (ImGui::MenuItem(get_localized_string("SELECT_ALL_TEXTURED").c_str()))
			{
				if (g_app->pickInfo.selectedFaces.size())
				{
					BSPFACE32& selface = map->faces[g_app->pickInfo.selectedFaces[0]];
					BSPTEXTUREINFO& seltexinfo = map->texinfos[selface.iTextureInfo];
					g_app->deselectFaces();
					for (int i = 0; i < map->faceCount; i++)
					{
						BSPFACE32& face = map->faces[i];
						BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
						if (texinfo.iMiptex == seltexinfo.iMiptex)
						{
							rend->highlightFace(i, 1);
							g_app->pickInfo.selectedFaces.push_back(i);
						}
					}
					pickCount++;
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string("SELECT_ALL_TEXTURED_FULL").c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem(get_localized_string("SELECT_FACE_MDL").c_str()))
			{
				if (g_app->pickInfo.selectedFaces.size())
				{
					int modelIdx = map->get_model_from_face((int)g_app->pickInfo.selectedFaces[0]);
					if (modelIdx >= 0)
					{
						BSPMODEL& model = map->models[modelIdx];
						for (int i = 0; i < model.nFaces; i++)
						{
							int faceIdx = model.iFirstFace + i;
							rend->highlightFace(faceIdx, 1);
							app->pickInfo.selectedFaces.push_back(faceIdx);
						}
					}
					pickCount++;
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string("SELECT_FACE_MDL_FULL").c_str());
				ImGui::EndTooltip();
			}

			if (ImGui::BeginMenu("Select Linked"))
			{
				for (int i = 0; i < 5; i++)
				{
					if (ImGui::MenuItem(fmt::format("Depth {}", (i + 1)).c_str()))
					{
						for (int n = 0; n <= i; n++)
						{
							std::vector<int> surfEdges;
							std::vector<int> vertices;
							for (auto f : app->pickInfo.selectedFaces)
							{
								BSPFACE32 face = map->faces[f];

								for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
								{
									int edgeIdx = map->surfedges[e];
									surfEdges.push_back(edgeIdx);

									for (int v = 0; v < 2; v++)
									{
										int vertIdx = map->edges[abs(edgeIdx)].iVertex[v];
										vertices.push_back(vertIdx);
									}
								}
							}

							for (int f = 0; f < map->faceCount; f++)
							{
								BSPFACE32 face = map->faces[f];

								if (std::find(app->pickInfo.selectedFaces.begin(), app->pickInfo.selectedFaces.end(), (int)f) != app->pickInfo.selectedFaces.end())
								{
									continue;
								}

								bool found = false;

								for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
								{
									int edgeIdx = map->surfedges[e];
									if (std::find(surfEdges.begin(), surfEdges.end(), edgeIdx) != surfEdges.end())
									{
										found = true;
										app->pickInfo.selectedFaces.push_back(f);
										rend->highlightFace(f, 1);
										break;
									}

									for (int v = 0; v < 2; v++)
									{
										int vertIdx = map->edges[abs(edgeIdx)].iVertex[v];

										if (std::find(vertices.begin(), vertices.end(), vertIdx) != vertices.end())
										{
											found = true;
											app->pickInfo.selectedFaces.push_back(f);
											rend->highlightFace(f, 1);
											break;
										}
									}
								}

								if (found)
									continue;
							}
						}
					}
				}

				ImGui::EndMenu();
			}

			ImGui::EndPopup();
		}
	}
	else /*if (app->pickMode == PICK_OBJECT)*/
	{
		if (!app->originHovered && ImGui::BeginPopup("ent_context") && entIdxs.size())
		{
			Entity* ent = map->ents[entIdxs[0]];
			int modelIdx = ent->getBspModelIdx();
			if (modelIdx < 0 && ent->isWorldSpawn())
				modelIdx = 0;

			if (modelIdx != 0 || app->hasCopiedEnt())
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

				if (app->hasCopiedEnt())
				{
					if (ImGui::BeginMenu((get_localized_string(LANG_0449) + "###BeginPaste").c_str()))
					{
						if (ImGui::MenuItem((get_localized_string(LANG_0449) + "###BEG_PASTE1").c_str(), get_localized_string(LANG_0441).c_str(), false))
						{
							app->pasteEnt(false);
						}
						if (ImGui::MenuItem((get_localized_string(LANG_0450) + "###BEG_OPASTE1").c_str(), 0, false))
						{
							app->pasteEnt(true);
						}
						if (ImGui::MenuItem("Paste with bspmodel###BEG_PASTE2", get_localized_string(LANG_0441).c_str(), false))
						{
							app->pasteEnt(false, true);
						}
						ImGui::EndMenu();
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
			if (entIdxs[0] < (int)map->ents.size() && map->ents[entIdxs[0]]->hide)
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0453).c_str(), get_localized_string(LANG_0454).c_str()))
				{
					map->ents[entIdxs[0]]->hide = false;
					rend->refreshEnt(entIdxs[0]);
					app->updateEntConnections();
				}
			}
			else if (ImGui::MenuItem(get_localized_string(LANG_0455).c_str(), get_localized_string(LANG_0454).c_str()))
			{
				map->hideEnts();
				app->clearSelection();
				rend->preRenderEnts();
				app->updateEntConnections();
				pickCount++;
			}

			ImGui::Separator();
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
								rend->refreshModel(modelIdx);
								checkValidHulls();
								print_log(get_localized_string(LANG_0330), modelIdx);
							}
							if (ImGui::MenuItem(get_localized_string(LANG_1069).c_str()))
							{
								map->delete_hull(1, modelIdx, -1);
								map->delete_hull(2, modelIdx, -1);
								map->delete_hull(3, modelIdx, -1);
								rend->refreshModelClipnodes(modelIdx);
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
										rend->refreshModel(modelIdx);
									else
										rend->refreshModelClipnodes(modelIdx);
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
								rend->refreshModelClipnodes(modelIdx);
								print_log(get_localized_string(LANG_0333), modelIdx);
							}

							ImGui::Separator();

							for (int i = 1; i < MAX_MAP_HULLS; i++)
							{
								bool isHullValid = map->models[modelIdx].iHeadnodes[i] >= 0;

								if (ImGui::MenuItem(("Hull " + std::to_string(i)).c_str(), 0, false, isHullValid))
								{
									map->simplify_model_collision(modelIdx, 1);
									rend->refreshModelClipnodes(modelIdx);
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
											rend->refreshModelClipnodes(modelIdx);
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

					if (ImGui::MenuItem("Print HeadNodes"))
					{
						if (modelIdx >= 0)
						{
							for (int i = 0; i < MAX_MAP_HULLS; i++)
							{
								print_log("iHeadNode{} = {}\n", i, map->models[modelIdx].iHeadnodes[i]);
							}
						}
					}

					ImGui::EndMenu();
				}


				ImGui::Separator();

				bool allowDuplicate = app->pickInfo.selectedEnts.size() > 0;
				if (allowDuplicate && app->pickInfo.selectedEnts.size() > 1)
				{
					for (auto& tmpentIdx : app->pickInfo.selectedEnts)
					{
						if (map->ents[tmpentIdx]->getBspModelIdx() <= 0)
						{
							allowDuplicate = false;
							break;
						}
					}
				}
				if (modelIdx > 0)
				{
					if (ImGui::MenuItem(get_localized_string("LANG_DUPLICATE_BSP").c_str(), 0, false, !app->isLoading && allowDuplicate))
					{
						print_log(get_localized_string(LANG_0336), app->pickInfo.selectedEnts.size());
						for (auto& tmpentIdx : app->pickInfo.selectedEnts)
						{
							if (map->ents[tmpentIdx]->isBspModel())
							{
								DuplicateBspModelCommand* command = new DuplicateBspModelCommand(get_localized_string("LANG_DUPLICATE_BSP"), (int)tmpentIdx);
								rend->pushUndoCommand(command);
								app->modelUsesSharedStructures = false;
							}
						}
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(get_localized_string("LANG_CREATE_DUPLICATE_BSP").c_str());
						ImGui::EndTooltip();
					}

					bool disableBspDupStruct = !app->modelUsesSharedStructures;
					if (disableBspDupStruct)
					{
						ImGui::BeginDisabled();
					}
					if (ImGui::MenuItem(get_localized_string("LANG_DUPLICATE_BSP_STRUCT").c_str(), 0, false, !app->isLoading && allowDuplicate))
					{
						print_log(get_localized_string(LANG_0336), app->pickInfo.selectedEnts.size());
						for (auto& tmpentIdx : app->pickInfo.selectedEnts)
						{
							if (map->ents[tmpentIdx]->isBspModel())
							{
								map->duplicate_model_structures(map->ents[tmpentIdx]->getBspModelIdx());
								app->modelUsesSharedStructures = false;
							}
						}
						map->update_ent_lump();
						rend->pushModelUndoState(get_localized_string("LANG_DUPLICATE_BSP_STRUCT"), EDIT_MODEL_LUMPS);

						pickCount++;
					}

					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(get_localized_string("LANG_CREATE_DUPLICATE_STRUCT").c_str());
						ImGui::EndTooltip();
					}
					if (disableBspDupStruct)
					{
						ImGui::EndDisabled();
					}

					bool IsValidForMerge = false;

					if (app->pickInfo.selectedEnts.size() > 1)
					{
						IsValidForMerge = true;
						for (auto& tmpentIdx : app->pickInfo.selectedEnts)
						{
							if (!map->ents[tmpentIdx]->isBspModel() || map->ents[tmpentIdx]->isWorldSpawn())
							{
								IsValidForMerge = false;
								break;
							}
						}
					}

					if (ImGui::MenuItem("MERGE BSPMODELS (WIP)", 0, false, !app->isLoading &&
						IsValidForMerge))
					{
						std::vector<int> toMerge = app->pickInfo.selectedEnts;

						std::vector<int> ents_to_erase;

						app->deselectObject();

						int merge_errors = 0;

						while (toMerge.size() > 1)
						{
							int ent1 = toMerge[toMerge.size() - 2];
							int ent2 = toMerge[toMerge.size() - 1];

							print_log(get_localized_string(LANG_1054), app->pickInfo.selectedEnts.size());

							int try_again = false;
							int newmodelid =
								map->merge_two_models_ents(ent1, ent2, try_again);

							int mdl1 = map->ents[ent1]->getBspModelIdx();
							int mdl2 = map->ents[ent2]->getBspModelIdx();

							if (newmodelid < 0)
							{
								print_log(PRINT_RED, "Model {} and {} is overlapped\n", mdl1, mdl2);
								print_log(PRINT_RED, "Impossible to merge it!\n");
								break;
							}

							if (map->ents[ent1]->getBspModelIdx() != newmodelid)
							{
								ents_to_erase.push_back(ent1);
								toMerge.erase(std::find(toMerge.begin(), toMerge.end(), ent1));
							}
							else
							{
								ents_to_erase.push_back(ent2);
								toMerge.erase(std::find(toMerge.begin(), toMerge.end(), ent2));
							}

							rend->refreshModel(newmodelid);
							rend->refreshModelClipnodes(newmodelid);
						}


						std::sort(ents_to_erase.begin(), ents_to_erase.end());
						while (ents_to_erase.size())
						{
							map->ents.erase(map->ents.begin() + ents_to_erase[ents_to_erase.size() - 1]);
							ents_to_erase.pop_back();
						}

						map->update_ent_lump();
						map->update_lump_pointers();

						rend->loadLightmaps();
						rend->preRenderEnts();

						rend->pushModelUndoState("MERGE {} and {} SELECTED BSP ENTITIES", EDIT_MODEL_LUMPS | FL_ENTITIES);

						if (merge_errors > 0)
						{
							print_log(PRINT_RED, "Found {} errors in models merging! Possible one model overlapped another!\n", merge_errors);
						}

					}
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted("CAN CAUSE SOMETHING PROBLEMS WITH MAP");
						ImGui::EndTooltip();
					}
				}
				if (ImGui::BeginMenu(get_localized_string(LANG_0466).c_str(), !app->isLoading && map))
				{
					if (ImGui::BeginMenu(get_localized_string(LANG_0467).c_str(), !app->isLoading))
					{
						if (ImGui::MenuItem(get_localized_string(LANG_0468).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(modelIdx) + ".bsp", modelIdx, 0, false);
						}
						if (ImGui::MenuItem(get_localized_string(LANG_0469).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(modelIdx) + ".bsp", modelIdx, 2, false);
						}
						if (ImGui::MenuItem(get_localized_string(LANG_0470).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(modelIdx) + ".bsp", modelIdx, 1, false);
						}
						ImGui::EndMenu();
					}

					if (ImGui::BeginMenu(get_localized_string(LANG_0471).c_str(), !app->isLoading && map))
					{
						if (ImGui::MenuItem(get_localized_string(LANG_1070).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(modelIdx) + ".bsp", modelIdx, 0, true);
						}
						if (ImGui::MenuItem(get_localized_string(LANG_1071).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(modelIdx) + ".bsp", modelIdx, 2, true);
						}
						if (ImGui::MenuItem(get_localized_string(LANG_1072).c_str(), 0, false, !app->isLoading))
						{
							ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(modelIdx) + ".bsp", modelIdx, 1, true);
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
			ImGui::Separator();
			if (ImGui::MenuItem(get_localized_string(LANG_0476).c_str(), get_localized_string(LANG_0477).c_str()))
			{
				showKeyvalueWidget = true;
			}


			ImGui::EndPopup();
		}

		if (ImGui::BeginPopup("empty_context"))
		{
			bool enabled = app->hasCopiedEnt();

			if (ImGui::MenuItem((get_localized_string(LANG_0449) + "###CONTENT_PASTE1").c_str(), get_localized_string(LANG_0441).c_str(), false, enabled))
			{
				app->pasteEnt(false);
			}
			if (ImGui::MenuItem((get_localized_string(LANG_0450) + "###CONTENT_OPASTE1").c_str(), 0, false, enabled))
			{
				app->pasteEnt(true);
			}
			if (ImGui::MenuItem("Paste with bspmodel###CONTENT_PASTE2", get_localized_string(LANG_0441).c_str(), false))
			{
				app->pasteEnt(false, true);
			}

			ImGui::EndPopup();
		}
	}
}

void Gui::drawMenuBar()
{
	ImGuiContext& g = *GImGui;
	static bool ditheringEnabled = false;
	Bsp* map = app->getSelectedMap();
	BspRenderer* rend = map ? map->getBspRender() : NULL;

	if (ImGui::BeginMainMenuBar())
	{
		if (ifd::FileDialog::Instance().IsDone("PngDirOpenDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				std::string png_import_dir = stripFileName(res.string());
				g_settings.lastdir = stripFileName(res.string());
				if (dirExists(png_import_dir))
				{
					createDir(g_working_dir);
					removeFile(g_working_dir + "temp2.wad");
					if (map && map->import_textures_to_wad(g_working_dir + "temp2.wad", png_import_dir, ditheringEnabled))
					{
						map->ImportWad(g_working_dir + "temp2.wad");
					}
					removeFile(g_working_dir + "temp2.wad");
				}
			}
			ifd::FileDialog::Instance().Close();
		}

		if (map)
		{
			if (ifd::FileDialog::Instance().IsDone("WadOpenDialog"))
			{
				if (ifd::FileDialog::Instance().HasResult())
				{
					std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
					for (size_t i = 0; i < map->ents.size(); i++)
					{
						if (map->ents[i]->keyvalues["classname"] == "worldspawn")
						{
							std::vector<std::string> wadNames = splitString(map->ents[i]->keyvalues["wad"], ";");
							std::string newWadNames;
							for (size_t k = 0; k < wadNames.size(); k++)
							{
								if (wadNames[k].find(res.filename().string()) == std::string::npos)
									newWadNames += wadNames[k] + ";";
							}
							map->ents[i]->setOrAddKeyvalue("wad", newWadNames);
							break;
						}
					}
					app->updateEnts();
					map->ImportWad(res.string());
					app->reloadBspModels();
					g_settings.lastdir = stripFileName(res.string());
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
				if (ends_with(pathlowercase, ".wad"))
				{
					if (!map)
					{
						app->addMap(new Bsp(""));
						app->selectMapId(0);
						map = app->getSelectedMap();
					}

					if (map)
					{
						bool foundInMap = false;
						for (auto& wad : rend->wads)
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
								rend->wads.push_back(wad);
								if (!ends_with(map->ents[0]->keyvalues["wad"], ";"))
									map->ents[0]->keyvalues["wad"] += ";";
								map->ents[0]->keyvalues["wad"] += basename(res.string()) + ";";
								map->update_ent_lump();
								app->updateEnts();
								app->reloading = true;
								rend->reload();
								app->reloading = false;
							}
							else
								delete wad;
						}
					}
				}
				else if (ends_with(pathlowercase, ".mdl"))
				{
					Bsp* tmpMap = new Bsp(res.string());
					tmpMap->is_mdl_model = true;
					app->addMap(tmpMap);
					app->selectMap(tmpMap);
				}
				else if (ends_with(pathlowercase, ".spr"))
				{
					Bsp* tmpMap = new Bsp(res.string());
					tmpMap->is_mdl_model = true;
					app->addMap(tmpMap);
					app->selectMap(tmpMap);
				}
				else if (ends_with(pathlowercase, ".csm"))
				{
					Bsp* tmpMap = new Bsp(res.string());
					tmpMap->is_mdl_model = true;
					app->addMap(tmpMap);
					app->selectMap(tmpMap);
				}
				else
				{
					Bsp* tmpMap = new Bsp(res.string());
					app->addMap(tmpMap);
					app->selectMap(tmpMap);
				}
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ImGui::BeginMenu(get_localized_string(LANG_0478).c_str()))
		{
			if (ImGui::MenuItem(get_localized_string(LANG_0479).c_str(), NULL, false, map && !map->is_mdl_model && !app->isLoading))
			{
				map->update_ent_lump();
				map->update_lump_pointers();
				map->validate();
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

				if (ImGui::MenuItem(get_localized_string(LANG_0481).c_str(), NULL, is_default_format && map->is_texture_has_pal))
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
						map->target_save_texture_has_pal = true;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0341));
					}
				}

				if (ImGui::MenuItem((get_localized_string(LANG_0481) + "[NO PALETTE]").c_str(), NULL, is_default_format && !map->is_texture_has_pal))
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
						map->target_save_texture_has_pal = true;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0341));
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
						map->target_save_texture_has_pal = true;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
						map->target_save_texture_has_pal = false;

						map->bsp_header.nVersion = 29;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
						map->target_save_texture_has_pal = false;

						map->bsp_header.nVersion = 29;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
							map->target_save_texture_has_pal = false;

							map->bsp_header.nVersion = 29;

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
						else
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
							map->target_save_texture_has_pal = false;

							map->bsp_header.nVersion = 29;

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
						else
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
						map->target_save_texture_has_pal = false;

						map->bsp_header.nVersion = 29;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
						map->target_save_texture_has_pal = false;

						map->bsp_header.nVersion = 29;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
						map->target_save_texture_has_pal = false;

						map->bsp_header.nVersion = 29;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
						map->target_save_texture_has_pal = false;

						map->bsp_header.nVersion = 29;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
						map->target_save_texture_has_pal = true;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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
						map->target_save_texture_has_pal = true;

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
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
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

				if (ImGui::MenuItem(get_localized_string("OPEN_SPR_VIEW").c_str()))
				{
					filterNeeded = true;
					ifd::FileDialog::Instance().Open("MapOpenDialog", "Select sprite path", "Sprite file (*.spr){.spr}", false, g_settings.lastdir);
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(get_localized_string(LANG_0527).c_str());
					ImGui::EndTooltip();
				}

				if (ImGui::MenuItem(get_localized_string("OPEN_XASHNT_CSM_VIEW").c_str()))
				{
					filterNeeded = true;
					ifd::FileDialog::Instance().Open("MapOpenDialog", "Select XashNT CSM path", "XashNT CSM model (*.csm){.csm}", false, g_settings.lastdir);
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
				if (mapRenderId >= 0)
				{
					if (rend)
					{
						map->setBspRender(NULL);
						app->deselectObject();
						app->clearSelection();
						app->deselectMap();
						mapRenderers.erase(mapRenderers.begin() + mapRenderId);
						delete rend;
						rend = NULL;
						map = NULL;
						app->selectMapId(0);
					}
				}
			}

			if (mapRenderers.size() > 1)
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
						app->selectMapId(0);

						rend = NULL;
						map = NULL;
						app->selectMapId(0);
						print_log(get_localized_string(LANG_0907));
					}
				}
			}

			if (ImGui::BeginMenu(get_localized_string(LANG_0532).c_str(), !app->isLoading && map))
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0533).c_str(), NULL, false, map && !map->is_mdl_model))
				{
					std::string entFilePath;
					if (g_settings.same_dir_for_ent) {
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
					createDir(g_working_dir);
					if (map->ExportEmbeddedWad(g_working_dir + map->bsp_name + ".wad"))
					{
						print_log(get_localized_string(LANG_0344));
						map->delete_embedded_textures();
						if (map->ents.size())
						{
							std::string wadstr = map->ents[0]->keyvalues["wad"];
							if (wadstr.find(map->bsp_name + ".wad;") == std::string::npos)
							{
								map->ents[0]->keyvalues["wad"] += map->bsp_name + ".wad;";
							}
						}
					}
				}

				static bool splitSmd = true;
				static bool oneRoot = false;

				if (ImGui::BeginMenu("StudioModel Data (.smd) [WIP]", map && !map->is_mdl_model))
				{
					if (ImGui::MenuItem("Split to goldsrc", NULL, &splitSmd))
					{
						//splitSmd
					}
					if (ImGui::MenuItem("Only root bone", NULL, &oneRoot))
					{
						//oneRoot
					}

					if (ImGui::MenuItem("Do Export", NULL))
					{
						map->ExportToSmdWIP(g_working_dir, splitSmd, oneRoot);
					}
					ImGui::EndMenu();
				}

				static int g_scale = 1;

				static bool g_group_faces = false;
				static bool g_group_as_objects = false;

				if (ImGui::BeginMenu("Wavefront(.obj)/XashNT(.csm) [WIP]", map && !map->is_mdl_model))
				{
					if (ImGui::BeginMenu("Select scale"))
					{
						if (ImGui::MenuItem(get_localized_string(LANG_0535).c_str(), NULL, g_scale == 1))
						{
							g_scale = 1;
						}

						for (int scale = 2; scale < 10; scale += 2)
						{
							std::string scaleitem = "UpScale x" + std::to_string(scale);
							if (ImGui::MenuItem(scaleitem.c_str(), NULL, g_scale == scale))
							{
								g_scale = scale;
							}
						}

						for (int scale = 16; scale > 0; scale -= 2)
						{
							std::string scaleitem = "DownScale x" + std::to_string(scale);
							if (ImGui::MenuItem(scaleitem.c_str(), NULL, g_scale == -scale))
							{
								g_scale = -scale;
							}
						}

						ImGui::EndMenu();
					}

					if (ImGui::MenuItem("Create face groups[OBJ]", NULL, &g_group_faces))
					{
						if (g_group_faces)
							g_group_as_objects = !g_group_faces;
					}

					if (ImGui::MenuItem("Create face objects[OBJ]", NULL, &g_group_as_objects))
					{
						if (g_group_faces)
							g_group_faces = !g_group_as_objects;
					}

					if (ImGui::BeginMenu("Export .obj"))
					{
						if (ImGui::MenuItem("Export only bsp"))
						{
							map->ExportToObjWIP(g_working_dir, g_scale, false, false, false, !g_group_faces &&
								!g_group_as_objects ? 0 : (g_group_faces ? 1 : 2));
						}
						if (ImGui::MenuItem("Export with models"))
						{
							map->ExportToObjWIP(g_working_dir, g_scale, false, true);
						}
						ImGui::EndMenu();
					}
					if (ImGui::BeginMenu("Export .csm"))
					{
						if (ImGui::MenuItem("Export only bsp"))
						{
							map->ExportToObjWIP(g_working_dir, g_scale, false, false, true);
						}
						if (ImGui::MenuItem("Export with models"))
						{
							map->ExportToObjWIP(g_working_dir, g_scale, false, true, true);
						}
						ImGui::EndMenu();
					}
					ImGui::EndMenu();
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(get_localized_string(LANG_0536).c_str());
					ImGui::EndTooltip();
				}

				static bool merge_faces = true;
				static bool use_one_back_vert = true;
				static bool inside_box = true;

				if (ImGui::BeginMenu("ValveHammerEditor (.map) [WIP]", map && !map->is_mdl_model))
				{
					ImGui::MenuItem("Merge faces", NULL, &merge_faces);

					ImGui::MenuItem("One back vert", NULL, &use_one_back_vert);

					ImGui::MenuItem("Create box", NULL, &inside_box);

					ImGui::Separator();

					if (ImGui::MenuItem("Full .map"))
					{
						map->ExportToMapWIP(g_working_dir, false, merge_faces, use_one_back_vert, inside_box);
					}
					else if (ImGui::MenuItem("Selected faces"))
					{
						map->ExportToMapWIP(g_working_dir, true, merge_faces, use_one_back_vert, inside_box);
					}
					ImGui::EndMenu();
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Export .map ( WIP )");
					ImGui::EndTooltip();
				}

				if (ImGui::MenuItem(get_localized_string(LANG_0537).c_str(), NULL, false, map && !map->is_mdl_model))
				{
					map->ExportPortalFile(map->bsp_path);
				}


				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(get_localized_string(LANG_0538).c_str());
					ImGui::EndTooltip();
				}

				if (ImGui::MenuItem(get_localized_string(LANG_0539).c_str(), NULL, false, map && !map->is_mdl_model))
				{
					std::string newpath;
					map->ExportExtFile(map->bsp_path, newpath);
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Export face extens (.ext) file for rad.exe");
					ImGui::EndTooltip();
				}



				if (ImGui::MenuItem(get_localized_string(LANG_0540).c_str(), NULL, false, map && !map->is_mdl_model))
				{
					map->ExportLightFile(map->bsp_path);
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(get_localized_string(LANG_0541).c_str());
					ImGui::EndTooltip();
				}

				ImGui::SetNextWindowSize({ -1.0f, 600.0f });
				if (ImGui::BeginMenu(get_localized_string(LANG_1076).c_str(), map && !map->is_mdl_model))
				{
					int modelIdx = -1;
					auto pickEnt = app->pickInfo.selectedEnts;
					if (pickEnt.size())
					{
						modelIdx = map->ents[pickEnt[0]]->getBspModelIdx();
					}
					for (int i = 0; i < map->modelCount; i++)
					{
						if (ImGui::BeginMenu(((modelIdx != i ? "Export Model" : "+ Export Model") + std::to_string(i) + ".bsp").c_str()))
						{
							if (ImGui::BeginMenu(get_localized_string(LANG_1077).c_str(), true))
							{
								if (ImGui::MenuItem(get_localized_string(LANG_1154).c_str(), 0, false, true))
								{
									ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(i) + ".bsp", i, 0, false);
								}
								if (ImGui::MenuItem(get_localized_string(LANG_1155).c_str(), 0, false, true))
								{
									ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(i) + ".bsp", i, 2, false);
								}
								if (ImGui::MenuItem(get_localized_string(LANG_1156).c_str(), 0, false, true))
								{
									ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(i) + ".bsp", i, 1, false);
								}
								ImGui::EndMenu();
							}
							if (ImGui::BeginMenu(get_localized_string(LANG_1078).c_str(), true))
							{
								if (ImGui::MenuItem(get_localized_string(LANG_1173).c_str(), 0, false, true))
								{
									ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(i) + ".bsp", i, 0, true);
								}
								if (ImGui::MenuItem(get_localized_string(LANG_1174).c_str(), 0, false, true))
								{
									ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(i) + ".bsp", i, 2, true);
								}
								if (ImGui::MenuItem(get_localized_string(LANG_1175).c_str(), 0, false, true))
								{
									ExportModel(map, g_working_dir + map->bsp_name + "_model" + std::to_string(i) + ".bsp", i, 1, true);
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
					for (auto& wad : rend->wads)
					{
						if (wad->dirEntries.size() == 0)
							continue;
						hash += "1";
						if (ImGui::MenuItem((basename(wad->filename) + hash).c_str()))
						{
							print_log(get_localized_string(LANG_0345), basename(wad->filename));
							map->export_wad_to_pngs(wad->filename, g_working_dir + "wads/" + basename(wad->filename));
						}
					}

					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("UnrealMapDrawTool (.umd) [WIP]", map && !map->is_mdl_model))
				{
					static int cell_size = 24;
					static bool texture_support = true;
					static bool fill_all_space = true;
					static bool NO_OPTIMIZE = false;

					if (ImGui::BeginMenu("Options###1"))
					{
						if (ImGui::BeginMenu("[Scan] Cell size"))
						{
							for (int tmpSize = 0; tmpSize <= 64; )
							{
								if (tmpSize <= 32)
								{
									tmpSize += 4;
								}
								else
								{
									tmpSize += 8;
								}

								if (ImGui::MenuItem((std::to_string(tmpSize) + " units###" + std::to_string(tmpSize)).c_str(), NULL, cell_size == tmpSize))
								{
									cell_size = tmpSize;
								}
							}
							ImGui::EndMenu();
						}

						if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
						{
							ImGui::BeginTooltip();
							ImGui::TextUnformatted("Smaller scan cell size is more better\nbut needed more time to scan.");
							ImGui::EndTooltip();
						}

						if (ImGui::MenuItem("Support textures", NULL, texture_support))
						{
							texture_support = !texture_support;
							if (texture_support)
							{
								fill_all_space = false;
							}
						}

						if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
						{
							ImGui::BeginTooltip();
							ImGui::TextUnformatted("Generate more faces but textured!");
							ImGui::EndTooltip();
						}

						if (ImGui::MenuItem("Fill near faces", NULL, !fill_all_space, !NO_OPTIMIZE))
						{
							fill_all_space = !fill_all_space;
						}

						if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
						{
							ImGui::BeginTooltip();
							ImGui::TextUnformatted("Instead fill all NON-EMPTY contents\ndo fill only with near faces.\nCan generate more faces.");
							ImGui::EndTooltip();
						}

						if (ImGui::MenuItem("NO OPTIMIZE [!!WARN!!]", NULL, NO_OPTIMIZE))
						{
							NO_OPTIMIZE = !NO_OPTIMIZE;
							fill_all_space = false;
						}

						if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
						{
							ImGui::BeginTooltip();
							ImGui::TextUnformatted("Gives fucking big number of cubes,\n can be used only for test purposes.");
							ImGui::EndTooltip();
						}

						ImGui::EndMenu();
					}

					int hull_for_export = -1;

					if (ImGui::MenuItem("Do Export [MAP]###2", NULL))
					{
						hull_for_export = 0;
					}

					if (ImGui::MenuItem("Do Export [HEAD_HULL]###3", NULL))
					{
						hull_for_export = 3;
					}

					if (hull_for_export >= 0)
					{
						print_log("Start exporting to UnrealMapDrawTool....\n");


						mapFixLightEnts(map);
						rend->pushModelUndoState("Create lights", FL_ENTITIES);
						FlushConsoleLog();
						vec3 mins{}, maxs{};
						/*map->get_bounding_box(mins, maxs);*/

						vec3 pos_debug_mins{}, pos_debug_maxs{};

						for (int i = 0; i < map->models[0].nFaces; i++)
						{
							BSPFACE32& face = map->faces[map->models[0].iFirstFace + i];
							for (int e = 0; e < face.nEdges; e++)
							{
								int edgeIdx = map->surfedges[face.iFirstEdge + e];
								BSPEDGE32& edge = map->edges[abs(edgeIdx)];
								int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];
								expandBoundingBox(map->verts[vertIdx], mins, maxs);
							}
						}

						FlushConsoleLog();
						mins += rend->mapOffset;
						maxs += rend->mapOffset;
						mins -= cell_size * 1.0f;
						maxs += cell_size * 1.0f;

						print_log("Found real world mins/maxs! Map offsets {},{},{}\n", rend->mapOffset.x, rend->mapOffset.y, rend->mapOffset.z);

						rend->calcFaceMaths();

						int hull = hull_for_export;

						int cell_x = 0;
						for (float x = mins.x; x < maxs.x; x += cell_size)
						{
							cell_x += 1;
						}
						int cell_y = 0;
						for (float y = mins.y; y < maxs.y; y += cell_size)
						{
							cell_y += 1;
						}
						int cell_levels = 0;
						for (float z = mins.z; z < maxs.z; z += cell_size)
						{
							cell_levels += 1;
						}
						int cell_layers = 2;

						std::vector<std::string> umdTextures{};


						std::vector<cell> cell_list(cell_x * cell_y * cell_levels * cell_layers);
						memset(&cell_list[0], 0, cell_list.size() * sizeof(cell));

						print_log("Pre vars calculated. Map mins/maxs {},{},{} / {},{},{}!\n", mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z);
						FlushConsoleLog();

						for (size_t entIdx = 0; entIdx < map->ents.size(); entIdx++)
						{
							print_log("\rProcess {} entity of {}...", entIdx, map->ents.size());
							FlushConsoleLog();
							int modelIdx = map->ents[entIdx]->getBspModelIdx();
							bool worldspawn = map->ents[entIdx]->isWorldSpawn();
							auto entity = map->ents[entIdx];
							if (modelIdx < 0)
							{
								int idx = cell_idx(entity->origin, mins, cell_size * 1.0f, cell_x, cell_y, cell_layers, 1);
								if (idx < (int)cell_list.size())
								{
									if (entity->classname.find("light") != std::string::npos)
									{
										cell_list[idx] = { 50,50,0,cell_light };
									}
									else if (entity->classname == "hostage_entity")
									{
										cell_list[idx] = { 50,50,0,cell_hostage };
									}
									else if (entity->classname == "info_player_start")
									{
										cell_list[idx] = { 50,50,0,cell_player_CT };
									}
									else if (entity->classname == "info_player_deathmatch")
									{
										cell_list[idx] = { 50,50,0,cell_player_TT };
									}
								}
								else
								{
									print_log("Fatal crash, index {} out of bounds {}\n", idx, cell_list.size());
								}
								continue;
							}

							int headNode = map->models[modelIdx].iHeadnodes[hull];
							if (headNode < 0)
								continue;

							BSPMODEL& model = map->models[modelIdx];

							vec3 model_mins{}, model_maxs{};

							if (modelIdx == 0)
							{
								model_mins = mins;
								model_maxs = maxs;
							}
							else
							{
								for (int i = 0; i < model.nFaces; i++)
								{
									BSPFACE32& face = map->faces[model.iFirstFace + i];
									for (int e = 0; e < face.nEdges; e++)
									{
										int edgeIdx = map->surfedges[face.iFirstEdge + e];
										BSPEDGE32& edge = map->edges[abs(edgeIdx)];
										int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];
										expandBoundingBox(map->verts[vertIdx], model_mins, model_maxs);
									}
								}

								model_mins += entity->origin;
								model_maxs += entity->origin;
							}

							auto faceIndices =
#ifndef WIN_XP_86
								std::views::iota(map->models[modelIdx].iFirstFace, map->models[modelIdx].iFirstFace + map->models[modelIdx].nFaces);
#else 
								std::vector<int>();
							for (int i = 0; i < model.nFaces; i++)
							{
								faceIndices.push_back(model.iFirstFace + i);
							}
#endif

							std::vector<std::vector<vec3>> faceVecs(faceIndices.size());
							for (size_t i = 0; i < faceIndices.size(); i++)
							{
								faceVecs[i] = map->get_face_verts(faceIndices[i]);
							}

							std::vector<vec3> offsets =
							{
								{0, 0, 0},
								{cell_size / 3.f, 0, 0},
								{-cell_size / 3.f, 0, 0},
								{0, cell_size / 3.f, 0},
								{0, -cell_size / 3.f, 0},
								{0, 0, cell_size / 3.f},
								{0, 0, -cell_size / 3.f},
								{0, 0, cell_size / 2.2f},
								{0, 0, -cell_size / 2.2f}
							};

							std::vector<float> parallel_X{};
							for (float x = mins.x; x < maxs.x; x += cell_size)
							{
								parallel_X.push_back(x);
							}

							std::mutex paralel_muta;

							for (float z = mins.z; z < maxs.z; z += cell_size)
							{
								print_log("\rProcess {} entity of {}... [{} of {}].........", entIdx, map->ents.size(), z, maxs.z);
								FlushConsoleLog();

								if (z > model_maxs.z || z < model_mins.z)
								{
									continue;
								}

								for (int layer = 0; layer < cell_layers; layer++)
								{
									if (layer != 0)
									{
										continue;
									}
									for (float y = mins.y; y < maxs.y; y += cell_size)
									{
										if (y > model_maxs.y || y < model_mins.y)
										{
											continue;
										}

										std::for_each(std::execution::par_unseq, parallel_X.begin(), parallel_X.end(),
											[&](float x)
											{
												if (x > model_maxs.x || x < model_mins.x)
												{
													return;
												}

												unsigned char texid = 0;

												vec3 pos = vec3(x, y, z);

												int index = cell_idx(pos, mins, (float)cell_size, cell_x, cell_y, cell_layers, layer);

												if ((size_t)index >= cell_list.size())
													return;
												cell& cur_cell = cell_list[index];
												expandBoundingBox(pos, pos_debug_mins, pos_debug_maxs);

												bool found = false;
												int leafIdx = 0;
												int planeIdx = -1;

												for (size_t off = 0; off < offsets.size(); off++)
												{
													int content = map->pointLeaf(headNode, pos + offsets[off], hull, leafIdx, planeIdx);
													if (CONTENTS_SOLID == content || (modelIdx > 0 && content == CONTENTS_WATER))
													{
														found = true;
														break;
													}
												}


												/*bool found = false;

												auto leaf_list = map->getLeafsFromPos(pos, cell_size);
												for (auto& leaf : leaf_list)
												{
													if (map->leaves[leaf].nContents == CONTENTS_SOLID)
													{
														found = true;
														break;
													}
												}*/

												if (found /*|| leaf_list.empty()*/)
												{
													int minFace = -1;

													if (texture_support)
													{
														float minDist = cell_size * 1.5f;
														for (size_t f = 0; f < faceIndices.size(); f++)
														{
															BSPFACE32& face = map->faces[faceIndices[f]];

															if (map->texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL)
															{
																continue;
															}

															auto& faceMath = rend->faceMaths[faceIndices[f]];

															float distanceToPlane = dotProduct(faceMath.normal, pos) - faceMath.fdist;
															float dot = std::fabs(distanceToPlane);

															if (dot > minDist)
															{
																continue;
															}

															bool isInsideFace = true;
															const std::vector<vec3>& vertices = faceVecs[f];

															for (size_t i = 0; i < vertices.size(); i++) {
																const vec3& v0 = vertices[i];
																const vec3& v1 = vertices[(i + 1) % vertices.size()];
																vec3 edge = v1 - v0;
																vec3 edgeNormal = crossProduct(faceMath.normal, edge).normalize();

																if (dotProduct(edgeNormal, pos - v0) > 0) {
																	isInsideFace = false;
																	break;
																}
															}

															if (!isInsideFace)
															{
																continue;
															}

															if (dot < minDist) {
																minDist = dot;
																minFace = faceIndices[f];
															}
														}
													}
													else
													{
														float minDist = cell_size * 3.0f;
														// more fast search
														for (size_t f = 0; f < faceIndices.size(); f++)
														{
															if (pos.dist(rend->faceMaths[faceIndices[f]].center) < minDist)
															{
																if (map->texinfos[map->faces[faceIndices[f]].iTextureInfo].nFlags
																	& TEX_SPECIAL)
																{
																	continue;
																}

																minFace = faceIndices[f];
																break;
															}
														}
													}

													/*int minFace = -1;
													float minDist = 1000.0f;

													for (size_t f = 0; f < faceIndices.size(); f++)
														{
														float tmpDist = std::fabs(rend->faceMaths[faceIndices[f]].center.dist(pos));
														if (tmpDist < minDist)
														{
															minDist = tmpDist;
															minFace = faceIndices[f];
														}
													}*/

													if (minFace >= 0)
													{
														BSPFACE32& face = map->faces[minFace];
														if (face.iTextureInfo >= 0)
														{
															BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
															BSPMIPTEX* tex = NULL;

															if (texinfo.iMiptex >= 0 && texinfo.iMiptex < map->textureCount)
															{
																int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
																if (texOffset >= 0)
																{
																	tex = ((BSPMIPTEX*)(map->textures + texOffset));
																	std::lock_guard<std::mutex> lock(paralel_muta);
																	bool hasTex = false;
																	for (size_t t = 0; t < umdTextures.size(); t++)
																	{
																		if (umdTextures[t] == tex->szName)
																		{
																			if (t <= 0xFF)
																			{
																				texid = (unsigned char)t;
																				hasTex = true;
																				break;
																			}
																		}
																	}

																	if (!hasTex && umdTextures.size() < 0xFF)
																	{
																		umdTextures.push_back(tex->szName);
																		texid = (unsigned char)(umdTextures.size() - 1);
																	}
																}
															}
														}
													}

													if (minFace >= 0 || fill_all_space)
													{
														if (worldspawn)
														{
															cur_cell = { 100, 0, texid, cell_brush };
														}
														else if (minFace >= 0)
														{
															if (entity->classname == "func_wall")
															{
																cur_cell = { 100, 0, texid, cell_wall };
															}
															else if (entity->classname == "func_water")
															{
																cur_cell = { 100, 0, texid, cell_waterzone };
															}
															else if (entity->classname == "func_buyzone")
															{
																cur_cell = { 100, 0, texid, cell_buyzone };
															}
															else if (entity->classname == "func_bomb_target")
															{
																cur_cell = { 100, 0, texid, cell_bombzone };
															}
															else
															{
																cur_cell = { 100, 0, texid, cell_wall };
															}
														}
													}
												}
											}
										);
									}

								}
							}
						}


						if (umdTextures.empty())
						{
							umdTextures.push_back("SKY");
						}
						createDir(g_working_dir);

						std::ofstream tmpmap(g_working_dir + "exported.umd", std::ios::out | std::ios::binary);

						print_log("\nSaved .umd map to {} path\n", g_working_dir + "exported.umd");

						if (tmpmap.is_open()) {
							tmpmap.write(reinterpret_cast<const char*>(&UMD_MAGIC), 4);

							int zero = 0;
							tmpmap.write(reinterpret_cast<const char*>(&zero), 4);
							tmpmap.write(reinterpret_cast<const char*>(&zero), 4);
							tmpmap.write(reinterpret_cast<const char*>(&zero), 4);
							tmpmap.write(reinterpret_cast<const char*>(&zero), 4);

							tmpmap.write(reinterpret_cast<const char*>(&cell_x), 4);
							tmpmap.write(reinterpret_cast<const char*>(&cell_y), 4);
							tmpmap.write(reinterpret_cast<const char*>(&cell_size), 4);
							tmpmap.write(reinterpret_cast<const char*>(&cell_size), 4);
							tmpmap.write(reinterpret_cast<const char*>(&cell_levels), 4);
							tmpmap.write(reinterpret_cast<const char*>(&cell_layers), 4);

							for (const auto& tmpcell : cell_list)
							{
								tmpmap.write(reinterpret_cast<const char*>(&tmpcell.height), 1);
								tmpmap.write(reinterpret_cast<const char*>(&tmpcell.height_offset), 1);
								tmpmap.write(reinterpret_cast<const char*>(&tmpcell.texid), 1);
								tmpmap.write(reinterpret_cast<const char*>(&tmpcell.type), 1);
							}

							int skybool = 0;
							tmpmap.write(reinterpret_cast<const char*>(&skybool), 4);

							unsigned int options = 0;
							if (texture_support)
							{
								options |= umd_flags::UMD_TEXTURES_SKIP_OPTIMIZE;
							}
							if (NO_OPTIMIZE)
							{
								options |= umd_flags::UMD_OPTIMIZE_DISABLED;
							}

							tmpmap.write(reinterpret_cast<const char*>(&options), 4);

							// textures
							int textureCount = (int)umdTextures.size();
							tmpmap.write(reinterpret_cast<const char*>(&textureCount), 4);

							for (const auto& texture : umdTextures) {
								int length = (int)texture.length();
								tmpmap.write(reinterpret_cast<const char*>(&length), 4);
								tmpmap.write(texture.c_str(), length);
							}

							tmpmap.close();
						}

						print_log("Success! Pos debug mins/maxs {},{},{} / {},{},{}!\n", pos_debug_mins.x, pos_debug_mins.y, pos_debug_mins.z, pos_debug_maxs.x, pos_debug_maxs.y, pos_debug_maxs.z);

					}
					ImGui::EndMenu();
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("TEST FEATURE WITH CELL SIZE 16\nNOW ONLY FOR WORLDSPAWN");
					ImGui::EndTooltip();
				}

				if (ImGui::MenuItem(get_localized_string("LANG_DUMP_TEX").c_str(), NULL, false, map))
				{
					createDir(g_working_dir + map->bsp_name + "/dump_textures/");

					if (g_all_Textures.size() && rend)
					{
						for (const auto& tex : g_all_Textures)
						{
							if (tex != missingTex)
							{
								if (tex->format == GL_RGBA)
									lodepng_encode32_file((g_working_dir + map->bsp_name + "/dump_textures/" + std::string(tex->texName) + ".png").c_str(), (const unsigned char*)tex->get_data(), tex->width, tex->height);
								else
									lodepng_encode24_file((g_working_dir + map->bsp_name + "/dump_textures/" + std::string(tex->texName) + ".png").c_str(), (const unsigned char*)tex->get_data(), tex->width, tex->height);
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


			if (ImGui::BeginMenu(get_localized_string(LANG_0543).c_str(), !app->isLoading))
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0544).c_str(), NULL, false, map && !map->is_mdl_model))
				{
					showImportMapWidget_Type = SHOW_IMPORT_MODEL_BSP;
					showImportMapWidget = true;
				}

				if (ImGui::MenuItem(get_localized_string(LANG_0545).c_str(), NULL, false, map && !map->is_mdl_model))
				{
					showImportMapWidget_Type = SHOW_IMPORT_MODEL_ENTITY;
					showImportMapWidget = true;
				}

				if (ImGui::MenuItem(get_localized_string(LANG_1079).c_str(), NULL, false, map && !map->is_mdl_model))
				{
					map->ImportLightFile(map->bsp_path);
				}

				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(get_localized_string(LANG_0546).c_str());
					ImGui::EndTooltip();
				}

				/*
					if (ImGui::MenuItem("Create .BSP from .JMF"))
					{
						// import all brushes
						// generate nodes
						// ... ?
					}
				*/


				if (ImGui::MenuItem(get_localized_string(LANG_1080).c_str(), NULL, false, map && !map->is_mdl_model))
				{
					if (map)
					{
						std::string entFilePath;
						if (g_settings.same_dir_for_ent) {
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
							g_app->updateEnts();
							app->reloading = true;
							for (size_t i = 0; i < mapRenderers.size(); i++)
							{
								BspRenderer* mapRender = mapRenderers[i];
								mapRender->reload();
							}
							app->reloading = false;
							g_app->reloadBspModels();
						}
						else
						{
							print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0348));
						}
					}
				}

				if (ImGui::MenuItem(get_localized_string(LANG_0547).c_str(), NULL, false, map && !map->is_mdl_model))
				{
					if (map)
					{
						ifd::FileDialog::Instance().Open("WadOpenDialog", "Open .wad", "Wad file (*.wad){.wad},.*", false, g_settings.lastdir);
					}

					if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(fmt::format(fmt::runtime(get_localized_string(LANG_0349)), g_working_dir, map->bsp_name + ".wad").c_str());
						ImGui::EndTooltip();
					}
				}

				if (ImGui::BeginMenu("Embedded##import", map && !map->is_mdl_model))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0549).c_str(), 0, ditheringEnabled))
						ditheringEnabled = !ditheringEnabled;

					if (ImGui::MenuItem("From .png files"))
					{
						if (map)
						{
							ifd::FileDialog::Instance().Open("PngDirOpenDialog", "Open .png dir", std::string(), false, g_settings.lastdir);
						}

						if (map && ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
						{
							ImGui::BeginTooltip();
							ImGui::TextUnformatted(fmt::format(fmt::runtime(get_localized_string(LANG_0349)), g_working_dir, map->bsp_name + ".wad").c_str());
							ImGui::EndTooltip();
						}
					}
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu(get_localized_string(LANG_0548).c_str(), map && !map->is_mdl_model))
				{
					if (ImGui::MenuItem(get_localized_string(LANG_0549).c_str(), 0, ditheringEnabled))
						ditheringEnabled = !ditheringEnabled;

					std::string hash = "##1";
					for (auto& wad : rend->wads)
					{
						if (wad->dirEntries.size() == 0)
							continue;
						hash += "1";
						if (ImGui::MenuItem((basename(wad->filename) + hash).c_str()))
						{
							print_log(get_localized_string(LANG_0350), basename(wad->filename));
							if (!map->import_textures_to_wad(wad->filename, g_working_dir + "wads/" + basename(wad->filename), ditheringEnabled))
							{
								//
							}
						}
					}
					ImGui::EndMenu();
				}

				ImGui::EndMenu();
			}

			if (map && dirExists(g_game_dir + "svencoop_addon/maps/"))
			{
				if (ImGui::MenuItem(get_localized_string(LANG_0550).c_str()))
				{
					std::string mapPath = g_game_dir + "svencoop_addon/maps/" + map->bsp_name + ".bsp";
					std::string entPath = g_game_dir + "svencoop_addon/scripts/maps/bspguy/maps/" + map->bsp_name + ".ent";

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

			/*

				if (ImGui::MenuItem("Merge", NULL, false, !app->isLoading)) {
					char* fname = tinyfd_openFileDialog("Merge Map", "",
						1, bspFilterPatterns, "GoldSrc Map Files (*.bsp)", 1);

					if (fname)
						g_app->merge(fname);
				}
				Bsp* map = g_app->mapRenderers[0]->map;
				tooltip(g, ("Merge one other BSP into the current file.\n\n"
					"Equivalent CLI command:\nbspguy merge " + map->name + " -noscript -noripent -maps \""
					+ map->name + ",other_map\"\n\nUse the CLI for automatic arrangement and optimization of "
					"many maps. The CLI also offers ripent fixes and script setup which can "
					"generate a playable map without you having to make any manual edits (Sven Co-op only).").c_str());

			*/

			if (ImGui::MenuItem(get_localized_string(LANG_0552).c_str(), 0, false, map && !map->is_mdl_model && !app->isLoading))
			{
				app->reloadMaps();
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0553).c_str(), 0, false, map && !map->is_mdl_model && !app->isLoading))
			{
				if (map)
				{
					print_log(get_localized_string(LANG_0358), map->bsp_name);
					if (!map->validate())
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1051));
					}
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
				g_settings.saveSettings();
				if (fileSize(g_settings_path) == 0)
				{
					g_settings.saveSettings();
					glfwTerminate();

#ifdef MINGW 
					std::set_terminate(NULL);
					std::terminate();
#else 
					std::quick_exit(0);
#endif
				}
				g_settings.saveSettings();
				if (fileSize(g_settings_path) == 0)
				{
					print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0359));
				}
				else
				{
					glfwTerminate();
#ifdef MINGW 
					std::set_terminate(NULL);
					std::terminate();
#else 
					std::quick_exit(0);
#endif
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
			bool nonWorldspawnEntSelected = entSelected;

			if (nonWorldspawnEntSelected)
			{
				for (auto& ent : app->pickInfo.selectedEnts)
				{
					if (map->ents[ent]->isWorldSpawn())
					{
						nonWorldspawnEntSelected = false;
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
			if (ImGui::MenuItem(get_localized_string(LANG_1083).c_str(), get_localized_string(LANG_1084).c_str(), false, app->pickInfo.selectedFaces.size() || (nonWorldspawnEntSelected && app->pickInfo.selectedEnts.size())))
			{
				if (app->pickInfo.selectedEnts.size())
					app->copyEnt();
				if (app->pickInfo.selectedFaces.size())
					copyTexture();
			}
			if (ImGui::BeginMenu((get_localized_string(LANG_0449) + "###BeginPaste2").c_str()))
			{
				if (ImGui::MenuItem((get_localized_string(LANG_0449) + "###BEG2_PASTE1").c_str(), get_localized_string(LANG_0441).c_str(), false))
				{
					app->pasteEnt(false);
				}
				if (ImGui::MenuItem((get_localized_string(LANG_0450) + "###BEG2_OPASTE1").c_str(), 0, false))
				{
					app->pasteEnt(true);
				}
				if (ImGui::MenuItem("Paste with bspmodel###BEG2_PASTE2", get_localized_string(LANG_0441).c_str(), false))
				{
					app->pasteEnt(false, true);
				}

				ImGui::EndMenu();
			}
			if (ImGui::MenuItem(get_localized_string(LANG_1085).c_str(), get_localized_string(LANG_1086).c_str(), false, nonWorldspawnEntSelected))
			{
				app->deleteEnts();
			}
			if (ImGui::MenuItem(get_localized_string(LANG_0559).c_str(), get_localized_string(LANG_0560).c_str()))
			{
				map->hideEnts(false);
				rend->preRenderEnts();
				app->updateEntConnections();
				pickCount++;
			}


			//if (ImGui::MenuItem("Paste entities from clipboard", 0, false)) 
			//{
			//	const char* clipBoardText = ImGui::GetClipboardText();
			//	if (clipBoardText && clipBoardText[0] == '{')
			//	{
			//		app->pasteEntsFromText(clipBoardText);
			//	}
			//}

			//IMGUI_TOOLTIP(g, "Creates entities from text data. You can use this to transfer entities "
			//	"from one bspguy window to another, or paste from .ent file text. Copy any entity "
			//	"in the viewer then paste to a text editor to see the format of the text data.");


			ImGui::Separator();


			bool allowDuplicate = app->pickInfo.selectedEnts.size() > 0;

			if (ImGui::MenuItem(get_localized_string("LANG_DUPLICATE_BSP").c_str(), 0, false, !app->isLoading && allowDuplicate))
			{
				print_log(get_localized_string(LANG_0336), app->pickInfo.selectedEnts.size());
				for (auto& tmpentIdx : app->pickInfo.selectedEnts)
				{
					if (map->ents[tmpentIdx]->isBspModel())
					{
						DuplicateBspModelCommand* command = new DuplicateBspModelCommand(get_localized_string("LANG_DUPLICATE_BSP"), (int)tmpentIdx);
						rend->pushUndoCommand(command);
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string("LANG_CREATE_DUPLICATE_BSP").c_str());
				ImGui::EndTooltip();
			}
			bool disableBspDupStruct = !app->modelUsesSharedStructures;
			if (disableBspDupStruct)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::MenuItem(get_localized_string("LANG_DUPLICATE_BSP_STRUCT").c_str(), 0, false, !app->isLoading && allowDuplicate))
			{
				print_log(get_localized_string(LANG_0336), app->pickInfo.selectedEnts.size());
				for (auto& tmpentIdx : app->pickInfo.selectedEnts)
				{
					if (map->ents[tmpentIdx]->isBspModel())
					{
						pickCount++;
						map->duplicate_model_structures(map->ents[tmpentIdx]->getBspModelIdx());
						rend->pushModelUndoState(get_localized_string("LANG_DUPLICATE_BSP_STRUCT"), EDIT_MODEL_LUMPS);
					}
				}
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string("LANG_CREATE_DUPLICATE_STRUCT").c_str());
				ImGui::EndTooltip();
			}
			if (disableBspDupStruct)
			{
				ImGui::EndDisabled();
			}

			/*if (ImGui::MenuItem("ADD TO WORLDSPAWN!", 0, false, !app->isLoading && allowDuplicate))
			{
				print_log(get_localized_string(LANG_1054), app->pickInfo.selectedEnts.size());
				for (auto& ent : app->pickInfo.selectedEnts)
				{
					if (map->ents[ent]->isBspModel())
					{
						map->add_model_to_worldspawn(map->ents[ent]->getBspModelIdx());
						map->ents[ent]->removeKeyvalue("model");
					}
				}
				rend->loadLightmaps();
				rend->preRenderEnts();
			}*/

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

			static int generateClipnodes = 0;

			if (ImGui::BeginMenu("MDL to BSP (WIP)", app->pickInfo.selectedEnts.size() == 1 &&
				rend->renderEnts[app->pickInfo.selectedEnts[0]].mdl))
			{
				if (ImGui::MenuItem("Bruteforce clipnodes", NULL, generateClipnodes == 1))
				{
					generateClipnodes = 1;
				}


				if (ImGui::MenuItem("Compile clipnodes", NULL, generateClipnodes == 2, false))
				{
					generateClipnodes = 2;
				}


				ImGui::Separator();

				if (ImGui::MenuItem("Convert selected to BSP"))
				{
					int ent = app->pickInfo.selectedEnts[0];

					map->import_mdl_to_bspmodel(ent, generateClipnodes);

					map->remove_unused_model_structures();

					map->save_undo_lightmaps();
					map->resize_all_lightmaps();

					rend->reuploadTextures();
					rend->loadLightmaps();


					rend->preRenderFaces();
					rend->preRenderEnts();

					rend->pushModelUndoState("CREATE MDL->BSP MODEL", EDIT_MODEL_LUMPS | FL_ENTITIES);
				}
				ImGui::EndMenu();
			}

			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Convert selected ent .MDL model to .BSP and add to map.");
				ImGui::EndTooltip();
			}

			if (ImGui::MenuItem("Recompile lighting", NULL, false, g_settings.rad_path.size()))
			{
				std::string path = g_settings.rad_path;
				FindPathInAssets(map, g_settings.rad_path, path);
				if (!fileExists(path))
				{
					print_log(PRINT_RED, "No hlrad.exe found!\n");
				}
				else
				{
					g_settings.save_cam = true;
					map->save_cam_pos = cameraOrigin;
					map->save_cam_angles = cameraAngles;

					map->update_ent_lump();
					map->update_lump_pointers();
					map->validate();
					map->write(map->bsp_path);

					Process* tmpProc = new Process(g_settings.rad_path);
					std::string args = g_settings.rad_options;
					std::string bsp_path;
					std::string old_bsp_path = map->bsp_path;
					map->ExportExtFile(old_bsp_path, bsp_path);

					size_t old_bsp_size = fileSize(bsp_path);
					if (old_bsp_size > 0)
					{
						replaceAll(args, "{map_path}", bsp_path);
						showConsoleWindow(true);

						tmpProc->arg(args);
						tmpProc->executeAndWait(0, 0, 0);

						if (fileSize(bsp_path) == old_bsp_size)
						{
							print_log(PRINT_RED, "Failed rad compiler!!!\n");
						}
						else
						{
							// close current map render
							int mapRenderId = map->getBspRenderId();
							if (mapRenderId >= 0)
							{
								BspRenderer* mapRender = map->getBspRender();
								if (mapRender)
								{
									map->setBspRender(NULL);
									app->deselectObject();
									app->clearSelection();
									app->deselectMap();
									mapRenderers.erase(mapRenderers.begin() + mapRenderId);
									delete mapRender;
									map = NULL;
									app->selectMapId(0);
								}
							}
							// remove old bsp 
							removeFile(old_bsp_path);

							// copy new bsp
							copyFile(bsp_path, old_bsp_path);
							map = new Bsp(old_bsp_path);
							app->addMap(map);

							// remove temporary files
							std::string delfileprefix = bsp_path.substr(0, bsp_path.size() - 4);
							removeFile(bsp_path);
							removeFile(delfileprefix + ".wa_");
							removeFile(delfileprefix + ".ext");
							removeFile(delfileprefix + ".log");
							removeFile(delfileprefix + ".err");
						}
					}
					else
					{
						print_log(PRINT_RED, "Error exporting old rad lighting!!\n");
					}
					delete tmpProc;
				}
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Recalculate lights using rad compiler. (From settings)");
				ImGui::EndTooltip();
			}
			if (ImGui::MenuItem("PROTECT MAP!(WIP)", NULL, false, !map->is_protected && rend))
			{
				map->merge_all_verts(1.f);

				bool partial_swap = false;
				for (int i = 0; i < map->edgeCount; i++)
				{
					std::swap(map->edges[i].iVertex[0], map->edges[i].iVertex[1]);
					map->surfedges[i] = -map->surfedges[i];
				}

				for (int m = 0; m < map->modelCount; m++)
				{
					BSPMODEL mdl = map->models[m];
					partial_swap = !partial_swap;
					if (mdl.iFirstFace >= 0 && mdl.nFaces > 1)
					{
						std::swap(map->faces[mdl.iFirstFace], map->faces[mdl.iFirstFace + 1]);
						for (int s = 0; s < map->marksurfCount; s++)
						{
							if (map->marksurfs[s] == mdl.iFirstFace)
							{
								map->marksurfs[s] = mdl.iFirstFace + 1;
							}
							else if (map->marksurfs[s] == mdl.iFirstFace + 1)
							{
								map->marksurfs[s] = mdl.iFirstFace;
							}
						}
					}
				}

				map->resize_all_lightmaps();

				rend->loadLightmaps();

				map->update_ent_lump();
				map->update_lump_pointers();

				map->is_protected = true;

				rend->pushModelUndoState("PROTECT MAP FROM DECOMPILER", EDIT_MODEL_LUMPS);
			}
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Protect map against decompilers.");
				ImGui::EndTooltip();
			}

			if (ImGui::BeginMenu("Porting tools"))
			{
				if (ImGui::BeginMenu("Delete OOB Data", !app->isLoading && app->getSelectedMap() && rend))
				{

					static const char* optionNames[10] = {
						"All Axes",
						"X Axis",
						"X Axis (positive only)",
						"X Axis (negative only)",
						"Y Axis",
						"Y Axis (positive only)",
						"Y Axis (negative only)",
						"Z Axis",
						"Z Axis (positive only)",
						"Z Axis (negative only)",
					};

					static int clipFlags[10] = {
						-1,
						OOB_CLIP_X | OOB_CLIP_X_NEG,
						OOB_CLIP_X,
						OOB_CLIP_X_NEG,
						OOB_CLIP_Y | OOB_CLIP_Y_NEG,
						OOB_CLIP_Y,
						OOB_CLIP_Y_NEG,
						OOB_CLIP_Z | OOB_CLIP_Z_NEG,
						OOB_CLIP_Z,
						OOB_CLIP_Z_NEG,
					};

					for (int i = 0; i < 10; i++) {
						if (ImGui::MenuItem(optionNames[i], 0, false, !app->isLoading && app->getSelectedMap() && app)) {
							if (map->ents[0]->hasKey("origin")) {
								vec3 ori = map->ents[0]->origin;
								print_log("Moved worldspawn origin by {} {} {}\n", ori.x, ori.y, ori.z);
								map->move(ori);
								map->ents[0]->removeKeyvalue("origin");

							}

							DeleteOobDataCommand* command = new DeleteOobDataCommand("Delete OOB Data",
								app->getSelectedMapId(), clipFlags[i], rend->undoLumpState);
							rend->pushUndoCommand(command);
						}
						IMGUI_TOOLTIP(g, "Deletes BSP data and entities outside of the "
							"max map boundary.\n\n"
							"This is useful for splitting maps to run in an engine with stricter map limits.");
					}

					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Delete Boxed Data", 0, false, !app->isLoading && app->getSelectedMap() && rend)) {
					if (!g_app->hasCullbox) {
						print_log("Create at least 2 entities with \"cull\" as a classname first!\n");
					}
					else {
						DeleteBoxedDataCommand* command = new DeleteBoxedDataCommand("Delete Boxed Data",
							app->getSelectedMapId(), g_app->cullMins, g_app->cullMaxs, rend->undoLumpState);
						rend->pushUndoCommand(command);
					}

				}
				IMGUI_TOOLTIP(g, "Deletes BSP data and entities inside of a box defined by 2 \"cull\" entities "
					"(for the min and max extent of the box). This is useful for getting maps to run in an "
					"engine with stricter map limits.\n\n"
					"Create 2 cull entities from the \"Create\" menu to define the culling box. "
					"A transparent red box will form between them.");
				if (ImGui::MenuItem("Deduplicate Models", 0, false, !app->isLoading && app->getSelectedMap())) {
					DeduplicateModelsCommand* command = new DeduplicateModelsCommand("Deduplicate models",
						app->getSelectedMapId(), rend->undoLumpState);
					rend->pushUndoCommand(command);
				}
				IMGUI_TOOLTIP(g, "Scans for duplicated BSP models and updates entity model keys to reference only one model in set of duplicated models. "
					"This lowers the model count and allows more game models to be precached.\n\n"
					"This does not delete BSP data structures unless you run the Clean command afterward.");
				if (ImGui::MenuItem("Downscale Invalid Textures", "(WIP)", false, !app->isLoading && app->getSelectedMap() && rend)) {
					map->downscale_invalid_textures();

					rend->preRenderFaces();
					g_app->gui->refresh();
					reloadLimits();
				}
				IMGUI_TOOLTIP(g, "Shrinks textures that exceed the max texture size and adjusts texture coordinates accordingly. Does not work with WAD textures yet.\n");
				if (ImGui::BeginMenu("Fix Bad Surface Extents", !app->isLoading && app->getSelectedMap()))
				{
					if (ImGui::MenuItem("Shrink Textures (512)", 0, false, !app->isLoading && app->getSelectedMap())) {
						FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Shrink textures (512)",
							app->getSelectedMapId(), false, true, 512, rend->undoLumpState);
						rend->pushUndoCommand(command);
					}
					IMGUI_TOOLTIP(g, "Downscales embedded textures on bad faces to a max resolution of 512x512 pixels. "
						"This alone will likely not be enough to fix all faces with bad surface extents."
						"You may also have to apply the Subdivide or Scale methods.");

					if (ImGui::MenuItem("Shrink Textures (256)", 0, false, !app->isLoading && app->getSelectedMap())) {
						FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Shrink textures (256)",
							app->getSelectedMapId(), false, true, 256, rend->undoLumpState);
						rend->pushUndoCommand(command);
					}
					IMGUI_TOOLTIP(g, "Downscales embedded textures on bad faces to a max resolution of 256x256 pixels. "
						"This alone will likely not be enough to fix all faces with bad surface extents."
						"You may also have to apply the Subdivide or Scale methods.");

					if (ImGui::MenuItem("Shrink Textures (128)", 0, false, !app->isLoading && app->getSelectedMap())) {
						FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Shrink textures (128)",
							app->getSelectedMapId(), false, true, 128, rend->undoLumpState);
						rend->pushUndoCommand(command);
					}
					IMGUI_TOOLTIP(g, "Downscales embedded textures on bad faces to a max resolution of 128x128 pixels. "
						"This alone will likely not be enough to fix all faces with bad surface extents."
						"You may also have to apply the Subdivide or Scale methods.");

					if (ImGui::MenuItem("Shrink Textures (64)", 0, false, !app->isLoading && app->getSelectedMap())) {
						FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Shrink textures (64)",
							app->getSelectedMapId(), false, true, 64, rend->undoLumpState);
						rend->pushUndoCommand(command);
					}
					IMGUI_TOOLTIP(g, "Downscales embedded textures to a max resolution of 64x64 pixels. "
						"This alone will likely not be enough to fix all faces with bad surface extents."
						"You may also have to apply the Subdivide or Scale methods.");

					ImGui::Separator();

					if (ImGui::MenuItem("Scale", 0, false, !app->isLoading && app->getSelectedMap())) {
						FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Scale faces",
							app->getSelectedMapId(), true, false, 0, rend->undoLumpState);
						rend->pushUndoCommand(command);
					}
					IMGUI_TOOLTIP(g, "Scales up face textures until they have valid extents. The drawback to this method is shifted texture coordinates and lower apparent texture quality.");

					if (ImGui::MenuItem("Subdivide", 0, false, !app->isLoading && app->getSelectedMap())) {
						FixSurfaceExtentsCommand* command = new FixSurfaceExtentsCommand("Subdivide faces",
							app->getSelectedMapId(), false, false, 0, rend->undoLumpState);
						rend->pushUndoCommand(command);
					}
					IMGUI_TOOLTIP(g, "Subdivides faces until they have valid extents. The drawback to this method is reduced in-game performace from higher poly counts.");

					ImGui::MenuItem("##", "WIP");
					IMGUI_TOOLTIP(g, "Anything you choose here will break lightmaps. "
						"Run the map through a RAD compiler to fix, and pray that the mapper didn't "
						"customize compile settings much.");
					ImGui::EndMenu();
				}
				if (ImGui::MenuItem("Cull Entity", 0, false, app->getSelectedMap())) {
					Entity* newEnt = new Entity();
					vec3 origin = (cameraOrigin + app->cameraForward * 100);
					if (app->gridSnappingEnabled)
						origin = app->snapToGrid(origin);
					newEnt->addKeyvalue("origin", origin.toKeyvalueString());
					newEnt->addKeyvalue("classname", "cull");

					CreateEntityCommand* createCommand = new CreateEntityCommand("Create Entity", app->getSelectedMapId(), newEnt);
					delete newEnt;
					rend->pushUndoCommand(createCommand);
				}
				IMGUI_TOOLTIP(g, "Create a point entity for use with the culling tool. 2 of these define the bounding box for structure culling operations.\n");

				ImGui::EndMenu();
			}

			if (ImGui::MenuItem("Generate nav mesh", NULL, false, (!rend->debugNavMesh || !g_app->debugLeafNavMesh)))
			{
				rend->generateNavMeshBuffer();
				rend->generateLeafNavMeshBuffer();
			}
			IMGUI_TOOLTIP(g, "I don't know for what it needs :) From original bspguy repository + crash fixes. \n");

			if (ImGui::BeginMenu("MAP TRANSFORMATION [WIP]", map))
			{
				if (ImGui::MenuItem("Mirror map x/y", NULL, false, map))
				{
					for (int i = 0; i < map->vertCount; i++)
					{
						std::swap(map->verts[i].x, map->verts[i].y);
					}

					for (int i = 0; i < map->faceCount; i++)
					{
						int* start = &map->surfedges[map->faces[i].iFirstEdge];
						int* end = &map->surfedges[map->faces[i].iFirstEdge + map->faces[i].nEdges];
						std::reverse(start, end);
					}

					for (int i = 0; i < map->planeCount; i++)
					{
						std::swap(map->planes[i].vNormal.x, map->planes[i].vNormal.y);
						map->planes[i].update_plane(false);
					}

					for (int i = 0; i < map->texinfoCount; i++)
					{
						std::swap(map->texinfos[i].vS.x, map->texinfos[i].vS.y);
						std::swap(map->texinfos[i].vT.x, map->texinfos[i].vT.y);
					}

					for (size_t i = 0; i < map->ents.size(); i++)
					{
						Entity* mapEnt = map->ents[i];
						if (!mapEnt->origin.IsZero())
						{
							std::swap(mapEnt->origin.x, mapEnt->origin.y);
							mapEnt->setOrAddKeyvalue("origin", mapEnt->origin.toKeyvalueString());
						}

						if (mapEnt->isBspModel())
						{
							continue;
						}

						if (mapEnt->hasKey("angle"))
						{
							float angle = str_to_float(mapEnt->keyvalues["angle"]);
							angle = 90.0f - angle;
							mapEnt->setOrAddKeyvalue("angle", std::to_string(fullnormalizeangle(angle)));
						}

						if (mapEnt->hasKey("angles"))
						{
							vec3 angles = parseVector(mapEnt->keyvalues["angles"]);
							angles[1] = 90.0f - angles[1];
							mapEnt->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
						}
						else if (!mapEnt->hasKey("angle"))
						{
							vec3 angles = vec3();
							angles[1] = 90.0f - angles[1];
							mapEnt->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
						}
					}

					for (int i = 0; i < map->leafCount; i++)
					{
						std::swap(map->leaves[i].nMins.x, map->leaves[i].nMins.y);
						std::swap(map->leaves[i].nMaxs.x, map->leaves[i].nMaxs.y);
					}

					for (int i = 0; i < map->modelCount; i++)
					{
						std::swap(map->models[i].nMins.x, map->models[i].nMins.y);
						std::swap(map->models[i].nMaxs.x, map->models[i].nMaxs.y);
					}

					for (int i = 0; i < map->nodeCount; i++)
					{
						std::swap(map->nodes[i].nMins.x, map->nodes[i].nMins.y);
						std::swap(map->nodes[i].nMaxs.x, map->nodes[i].nMaxs.y);
					}

					map->update_ent_lump();
					app->reloading = true;
					rend->reload();
					app->reloading = false;
				}

				if (ImGui::MenuItem("Rotate Counter Clockwise 90", NULL, false, map))
				{
					for (int i = 0; i < map->vertCount; i++)
					{
						std::swap(map->verts[i].x, map->verts[i].y);
						map->verts[i].x *= -1;
					}

					std::set<int> flipped;

					for (int i = 0; i < map->planeCount; i++)
					{
						std::swap(map->planes[i].vNormal.x, map->planes[i].vNormal.y);
						map->planes[i].vNormal.x *= -1;

						bool flip = map->planes[i].update_plane(true);

						if (flip)
						{
							flipped.insert(i);
						}
					}

					for (int i = 0; i < map->faceCount; i++)
					{
						if (flipped.count(map->faces[i].iPlane))
							map->faces[i].nPlaneSide = map->faces[i].nPlaneSide ? 0 : 1;
					}

					for (int i = 0; i < map->texinfoCount; i++)
					{
						std::swap(map->texinfos[i].vS.x, map->texinfos[i].vS.y);
						std::swap(map->texinfos[i].vT.x, map->texinfos[i].vT.y);

						map->texinfos[i].vS.x *= -1;
						map->texinfos[i].vT.x *= -1;
					}

					for (size_t i = 0; i < map->ents.size(); i++)
					{
						if (map->ents[i]->hasKey("origin"))
						{
							map->ents[i]->origin = parseVector(map->ents[i]->keyvalues["origin"]);

							std::swap(map->ents[i]->origin.x, map->ents[i]->origin.y);
							map->ents[i]->origin.x *= -1;

							map->ents[i]->setOrAddKeyvalue("origin", map->ents[i]->origin.toKeyvalueString());
						}

						if (map->ents[i]->isBspModel())
						{
							continue;
						}

						if (map->ents[i]->hasKey("angle"))
						{
							float angle = str_to_float(map->ents[i]->keyvalues["angle"]);
							angle += 90.0f;

							map->ents[i]->setOrAddKeyvalue("angle", std::to_string(fullnormalizeangle(angle)));
						}

						if (map->ents[i]->hasKey("angles"))
						{
							vec3 angles = parseVector(map->ents[i]->keyvalues["angles"]);
							angles[1] += 90.0f;

							map->ents[i]->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
						}
						else if (!map->ents[i]->hasKey("angle"))
						{
							vec3 angles = vec3();
							angles[1] += 90.0f;
							map->ents[i]->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
						}
					}

					for (int i = 0; i < map->leafCount; i++)
					{
						std::swap(map->leaves[i].nMins.y, map->leaves[i].nMaxs.y);

						std::swap(map->leaves[i].nMins.x, map->leaves[i].nMins.y);
						map->leaves[i].nMins.x *= -1;
						std::swap(map->leaves[i].nMaxs.x, map->leaves[i].nMaxs.y);
						map->leaves[i].nMaxs.x *= -1;
					}

					for (int i = 0; i < map->modelCount; i++)
					{
						std::swap(map->models[i].nMins.y, map->models[i].nMaxs.y);

						std::swap(map->models[i].nMins.x, map->models[i].nMins.y);
						map->models[i].nMins.x *= -1;
						std::swap(map->models[i].nMaxs.x, map->models[i].nMaxs.y);
						map->models[i].nMaxs.x *= -1;
					}

					for (int i = 0; i < map->nodeCount; i++)
					{
						std::swap(map->nodes[i].nMins.y, map->nodes[i].nMaxs.y);

						std::swap(map->nodes[i].nMins.x, map->nodes[i].nMins.y);
						map->nodes[i].nMins.x *= -1;
						std::swap(map->nodes[i].nMaxs.x, map->nodes[i].nMaxs.y);
						map->nodes[i].nMaxs.x *= -1;

						if (flipped.count(map->nodes[i].iPlane))
						{
							std::swap(map->nodes[i].iChildren[0], map->nodes[i].iChildren[1]);
						}
					}

					for (int i = 0; i < map->clipnodeCount; i++)
					{
						if (flipped.count(map->clipnodes[i].iPlane))
						{
							std::swap(map->clipnodes[i].iChildren[0], map->clipnodes[i].iChildren[1]);
						}
					}

					map->update_ent_lump();
					app->reloading = true;
					rend->reload();
					app->reloading = false;
				}

				if (ImGui::MenuItem("Rotate Clockwise 90", NULL, false, map))
				{
					for (int i = 0; i < map->vertCount; i++)
					{
						std::swap(map->verts[i].x, map->verts[i].y);
						map->verts[i].y *= -1;
					}

					std::set<int> flipped;

					for (int i = 0; i < map->planeCount; i++)
					{
						std::swap(map->planes[i].vNormal.x, map->planes[i].vNormal.y);
						map->planes[i].vNormal.y *= -1;

						bool flip = map->planes[i].update_plane(true);

						if (flip)
						{
							flipped.insert(i);
						}
					}

					for (int i = 0; i < map->faceCount; i++)
					{
						if (flipped.count(map->faces[i].iPlane))
							map->faces[i].nPlaneSide = map->faces[i].nPlaneSide ? 0 : 1;
					}

					for (int i = 0; i < map->texinfoCount; i++)
					{
						std::swap(map->texinfos[i].vS.x, map->texinfos[i].vS.y);
						std::swap(map->texinfos[i].vT.x, map->texinfos[i].vT.y);

						map->texinfos[i].vS.y *= -1;
						map->texinfos[i].vT.y *= -1;
					}

					for (size_t i = 0; i < map->ents.size(); i++)
					{
						if (map->ents[i]->hasKey("origin"))
						{
							map->ents[i]->origin = parseVector(map->ents[i]->keyvalues["origin"]);

							std::swap(map->ents[i]->origin.x, map->ents[i]->origin.y);
							map->ents[i]->origin.y *= -1;

							map->ents[i]->setOrAddKeyvalue("origin", map->ents[i]->origin.toKeyvalueString());
						}

						if (map->ents[i]->isBspModel())
						{
							continue;
						}

						if (map->ents[i]->hasKey("angle"))
						{
							float angle = str_to_float(map->ents[i]->keyvalues["angle"]);
							angle -= 90.0f;

							map->ents[i]->setOrAddKeyvalue("angle", std::to_string(fullnormalizeangle(angle)));
						}

						if (map->ents[i]->hasKey("angles"))
						{
							vec3 angles = parseVector(map->ents[i]->keyvalues["angles"]);
							angles[1] -= 90.0f;

							map->ents[i]->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
						}
						else if (!map->ents[i]->hasKey("angle"))
						{
							vec3 angles = vec3();
							angles[1] -= 90.0f;
							map->ents[i]->setOrAddKeyvalue("angles", angles.normalize_angles().toKeyvalueString());
						}
					}

					for (int i = 0; i < map->leafCount; i++)
					{
						std::swap(map->leaves[i].nMins.x, map->leaves[i].nMaxs.x);

						std::swap(map->leaves[i].nMins.x, map->leaves[i].nMins.y);
						map->leaves[i].nMins.y *= -1;
						std::swap(map->leaves[i].nMaxs.x, map->leaves[i].nMaxs.y);
						map->leaves[i].nMaxs.y *= -1;
					}

					for (int i = 0; i < map->modelCount; i++)
					{
						std::swap(map->models[i].nMins.x, map->models[i].nMaxs.x);

						std::swap(map->models[i].nMins.x, map->models[i].nMins.y);
						map->models[i].nMins.y *= -1;
						std::swap(map->models[i].nMaxs.x, map->models[i].nMaxs.y);
						map->models[i].nMaxs.y *= -1;
					}

					for (int i = 0; i < map->nodeCount; i++)
					{
						std::swap(map->nodes[i].nMins.x, map->nodes[i].nMaxs.x);

						std::swap(map->nodes[i].nMins.x, map->nodes[i].nMins.y);
						map->nodes[i].nMins.y *= -1;
						std::swap(map->nodes[i].nMaxs.x, map->nodes[i].nMaxs.y);
						map->nodes[i].nMaxs.y *= -1;

						if (flipped.count(map->nodes[i].iPlane))
						{
							std::swap(map->nodes[i].iChildren[0], map->nodes[i].iChildren[1]);
						}
					}

					for (int i = 0; i < map->clipnodeCount; i++)
					{
						if (flipped.count(map->clipnodes[i].iPlane))
						{
							std::swap(map->clipnodes[i].iChildren[0], map->clipnodes[i].iChildren[1]);
						}
					}

					map->update_ent_lump();
					app->reloading = true;
					rend->reload();
					app->reloading = false;
				}

				if (ImGui::BeginMenu("Scale map", map))
				{
					static bool ScaleOnlySelected = false;

					if (ImGui::MenuItem("Scale selected", NULL, &ScaleOnlySelected))
					{
						//ScaleOnlySelected = !ScaleOnlySelected;
					}

					for (float scale_val = 0.25f; scale_val <= 2.0f; scale_val += 0.25f)
					{
						if (std::fabs(scale_val - 1.0f) > EPSILON && ImGui::MenuItem(fmt::format("Scale {:2}X", scale_val).c_str()))
						{
							if (ScaleOnlySelected)
							{
								STRUCTUSAGE modelUsage = STRUCTUSAGE(map);
								std::set<int> models;

								for (auto s : app->pickInfo.selectedEnts)
								{
									int modelIdx = map->ents[s]->getBspModelIdx();
									if (modelIdx >= 0)
									{
										models.insert(modelIdx);
										map->mark_model_structures(modelIdx, &modelUsage, true);
									}
								}

								for (int i = 0; i < map->modelCount; i++)
								{
									if (models.count(i))
									{
										map->models[i].nMaxs *= scale_val;
										map->models[i].nMins *= scale_val;

										vec3 neworigin = map->models[i].vOrigin * scale_val;
										map->models[i].vOrigin = neworigin;
									}
								}
								for (int i = 0; i < map->vertCount; i++)
								{
									if (modelUsage.verts[i])
									{
										map->verts[i] *= scale_val;
									}
								}
								for (int i = 0; i < map->texinfoCount; i++)
								{
									if (modelUsage.texInfo[i])
									{
										map->texinfos[i].vS /= scale_val;
										map->texinfos[i].vT /= scale_val;
									}
								}
								for (int i = 0; i < (int)map->ents.size(); i++)
								{
									if (app->pickInfo.IsSelectedEnt(i))
									{
										vec3 neworigin = map->ents[i]->origin * scale_val;
										neworigin.z += std::fabs(neworigin.z - map->ents[i]->origin.z) * scale_val;
										map->ents[i]->setOrAddKeyvalue("origin", neworigin.toKeyvalueString());
									}
								}
								for (int i = 0; i < map->nodeCount; i++)
								{
									if (modelUsage.nodes[i])
									{
										map->nodes[i].nMaxs *= scale_val;
										map->nodes[i].nMins *= scale_val;
									}
								}
								for (int i = 0; i < map->leafCount; i++)
								{
									if (modelUsage.leaves[i])
									{
										map->leaves[i].nMaxs *= scale_val;
										map->leaves[i].nMins *= scale_val;
									}
								}
								for (int i = 0; i < map->planeCount; i++)
								{
									if (modelUsage.planes[i])
									{
										map->planes[i].fDist *= scale_val;
									}
								}
							}
							else
							{
								for (int i = 0; i < map->modelCount; i++)
								{
									map->models[i].nMaxs *= scale_val;
									map->models[i].nMins *= scale_val;

									vec3 neworigin = map->models[i].vOrigin * scale_val;
									map->models[i].vOrigin = neworigin;
								}
								for (int i = 0; i < map->vertCount; i++)
								{
									map->verts[i] *= scale_val;
								}
								for (int i = 0; i < map->texinfoCount; i++)
								{
									map->texinfos[i].vS /= scale_val;
									map->texinfos[i].vT /= scale_val;
								}
								for (size_t i = 0; i < map->ents.size(); i++)
								{
									vec3 neworigin = map->ents[i]->origin * scale_val;
									neworigin.z += std::fabs(neworigin.z - map->ents[i]->origin.z) * scale_val;
									map->ents[i]->setOrAddKeyvalue("origin", neworigin.toKeyvalueString());
								}
								for (int i = 0; i < map->nodeCount; i++)
								{
									map->nodes[i].nMaxs *= scale_val;
									map->nodes[i].nMins *= scale_val;
								}
								for (int i = 0; i < map->leafCount; i++)
								{
									map->leaves[i].nMaxs *= scale_val;
									map->leaves[i].nMins *= scale_val;
								}
								for (int i = 0; i < map->planeCount; i++)
								{
									//map->planes[i].update_plane(map->planes[i].vNormal, map->planes[i].fDist *= scale_val);
									map->planes[i].fDist *= scale_val;
								}
							}
							map->resize_all_lightmaps();

							rend->loadLightmaps();
							rend->preRenderEnts();
							rend->reloadClipnodes();

							map->update_ent_lump();
							map->update_lump_pointers();

							rend->pushModelUndoState(fmt::format("MAP SCALE TO {:2}", scale_val), EDIT_MODEL_LUMPS | FL_ENTITIES);
						}
					}
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Delete cull faces", map))
			{
				if (ImGui::MenuItem("Delete from [SKY LEAFS]"))
				{
					map->remove_faces_by_content(CONTENTS_SKY);

					map->save_undo_lightmaps();
					map->resize_all_lightmaps();
					rend->loadLightmaps();
					rend->preRenderFaces();

					map->update_ent_lump();
					map->update_lump_pointers();

					rend->pushModelUndoState("REMOVE FACES FROM SKY", EDIT_MODEL_LUMPS);
				}
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("WARNING! Can remove unexpected faces if VIS has been edited previously.");
					ImGui::EndTooltip();
				}

				if (ImGui::MenuItem("Delete from [SOLID LEAFS]"))
				{
					map->remove_faces_by_content(CONTENTS_SOLID);


					map->save_undo_lightmaps();
					map->resize_all_lightmaps();
					rend->loadLightmaps();
					rend->preRenderFaces();

					map->update_ent_lump();
					map->update_lump_pointers();
					rend->pushModelUndoState("REMOVE FACES FROM SOLID", EDIT_MODEL_LUMPS);
				}
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("WARNING! Can remove unexpected faces if VIS has been edited previously.");
					ImGui::EndTooltip();
				}
				if (rend->curLeafIdx > 0)
				{
					if (ImGui::MenuItem(fmt::format("Delete from [{} leaf]", rend->curLeafIdx).c_str()))
					{
						map->cull_leaf_faces(rend->curLeafIdx);

						map->save_undo_lightmaps();
						map->resize_all_lightmaps();

						rend->loadLightmaps();
						rend->preRenderFaces();

						map->update_ent_lump();
						map->update_lump_pointers();

						rend->pushModelUndoState(fmt::format("REMOVE FACES FROM {} LEAF", rend->curLeafIdx), EDIT_MODEL_LUMPS);
					}
					if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted("WARNING! Can remove unexpected faces if VIS has been edited previously.");
						ImGui::EndTooltip();
					}
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
						//for (size_t k = 0; k < mapRenderers.size(); k++) {
						//	Bsp* map = mapRenderers[k]->map;
						map->delete_hull(i, -1);
						rend->reloadClipnodes();
						//	mapRenderers[k]->reloadClipnodes();
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
								//for (size_t j = 0; j < mapRenderers.size(); j++) {
								//	Bsp* map = mapRenderers[j]->map;
								map->delete_hull(i, k);
								rend->reloadClipnodes();
								//	mapRenderers[j]->reloadClipnodes();
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
						if (!map->GetFaceExtents(i, bmins, bmaxs))
						{
							info.nFlags |= TEX_SPECIAL;
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
					if (rend)
					{
						app->reloading = true;
						rend->reload();
						app->reloading = false;
					}
				}
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(get_localized_string(LANG_0582).c_str());
					ImGui::EndTooltip();
				}

				if (ImGui::MenuItem("Fix bad leaf count"))
				{
					int totalLeaves = 1;
					for (int i = 0; i < map->modelCount; i++)
					{
						totalLeaves += map->models[i].nVisLeafs;
					}
					if (totalLeaves > map->leafCount)
					{
						while (totalLeaves > map->leafCount)
							map->create_leaf(CONTENTS_EMPTY);
					}
					else if (totalLeaves < map->leafCount)
					{
						while (totalLeaves < map->leafCount)
						{
							map->models[0].nVisLeafs++;
							totalLeaves++;
						}
					}
				}
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Create empty leafs. ");
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
							int texdata = (int)(((unsigned char*)tex) - map->textures) + tex->nOffsets[0] + texlen - sizeof(BSPMIPTEX);
							if (texdata > map->bsp_header.lump[LUMP_TEXTURES].nLength)
							{
								print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0364), i, map->bsp_header.lump[LUMP_TEXTURES].nLength, texdata);

								char* newlump = new char[texdata];
								memset(newlump, 0, texdata);
								memcpy(newlump, map->textures, map->bsp_header.lump[LUMP_TEXTURES].nLength);
								map->replace_lump(LUMP_TEXTURES, newlump, texdata);

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
									for (auto& s : rend->wads)
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
					rend->reuploadTextures();
					rend->preRenderFaces();
				}
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(get_localized_string(LANG_0585).c_str());
					ImGui::TextUnformatted(get_localized_string(LANG_0586).c_str());
					ImGui::EndTooltip();
				}

				//face_fix_duplicate_edges(i);
				ImGui::BeginDisabled();
				if (ImGui::MenuItem("Fix light entities[+TEXTURE]"))
				{


				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Fill map with light entities for '+' textures");
					ImGui::EndTooltip();
				}


				if (ImGui::MenuItem("Fix light entities"))
				{
					mapFixLightEnts(map);
					g_app->updateEnts();
				}
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Fill map with light entities");
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

				float snapSize = app->snapSize;
				if (snapSize < 16)
				{
					snapSize = 16;
				}

				CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", app->getSelectedMapId(), newEnt, snapSize, true);
				rend->pushUndoCommand(command);
				delete newEnt;

				if (map->ents.size())
				{
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
				map->resize_all_lightmaps();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0591).c_str(), 0, false, !app->isLoading && map))
			{
				vec3 origin = cameraOrigin + app->cameraForward * 100;
				if (app->gridSnappingEnabled)
					origin = app->snapToGrid(origin);

				Entity* newEnt = new Entity();
				newEnt->addKeyvalue("origin", origin.toKeyvalueString());
				newEnt->addKeyvalue("classname", "func_wall");

				float snapSize = app->snapSize;
				if (snapSize < 16)
				{
					snapSize = 16;
				}

				CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", app->getSelectedMapId(), newEnt, snapSize, false);
				rend->pushUndoCommand(command);
				map->save_undo_lightmaps();

				delete newEnt;

				if (map->ents.size())
				{
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
				map->resize_all_lightmaps();
			}

			if (ImGui::MenuItem(get_localized_string(LANG_0590).c_str(), 0, false, !app->isLoading && map))
			{
				vec3 origin = cameraOrigin + app->cameraForward * 100;
				if (app->gridSnappingEnabled)
					origin = app->snapToGrid(origin);

				Entity* newEnt = new Entity();
				newEnt->addKeyvalue("origin", origin.toKeyvalueString());
				newEnt->addKeyvalue("classname", "trigger_once");

				float snapSize = app->snapSize;
				if (snapSize < 16)
				{
					snapSize = 16;
				}

				CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", app->getSelectedMapId(), newEnt, snapSize, false);
				rend->pushUndoCommand(command);

				delete newEnt;
				if (map->ents.size())
				{
					newEnt = map->ents[map->ents.size() - 1];
					if (newEnt && newEnt->getBspModelIdx() >= 0)
					{
						BSPMODEL& model = map->models[newEnt->getBspModelIdx()];
						model.iFirstFace = 0;
						model.nFaces = 0;
					}
				}
				map->remove_unused_model_structures(CLEAN_FACES | CLEAN_MARKSURFACES);
			}

			if (ImGui::MenuItem("BSP Clip model", 0, false, !app->isLoading && map))
			{
				vec3 origin = cameraOrigin + app->cameraForward * 100;
				if (app->gridSnappingEnabled)
					origin = app->snapToGrid(origin);

				Entity* newEnt = new Entity();
				newEnt->addKeyvalue("origin", origin.toKeyvalueString());
				newEnt->addKeyvalue("classname", "func_wall");

				float snapSize = app->snapSize;
				if (snapSize < 16)
				{
					snapSize = 16;
				}

				CreateBspModelCommand* command = new CreateBspModelCommand("Create Model", app->getSelectedMapId(), newEnt, snapSize, false);
				rend->pushUndoCommand(command);

				delete newEnt;
				if (map->ents.size())
				{
					newEnt = map->ents[map->ents.size() - 1];
					if (newEnt && newEnt->getBspModelIdx() >= 0)
					{
						BSPMODEL& model = map->models[newEnt->getBspModelIdx()];
						model.iFirstFace = 0;
						model.nFaces = 0;
					}
				}
				map->remove_unused_model_structures(CLEAN_FACES | CLEAN_MARKSURFACES);
			}

			if (DebugKeyPressed)
			{
				if (ImGui::BeginMenu("Other"))
				{
					if (ImGui::MenuItem("Random DM spawn points"))
					{
						for (int i = (int)map->ents.size() - 1; i >= 0; i--)
						{
							if (map->ents[i]->classname == "info_player_deathmatch" ||
								map->ents[i]->classname == "info_player_start")
							{
								map->ents.erase(map->ents.begin() + i);
							}
						}
						//todo....
					}
					ImGui::EndMenu();
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
					app->pickMode = PICK_FACE;
					pickCount++;
					showLightmapEditorUpdate = true;
				}
				if (ImGui::MenuItem(get_localized_string(LANG_0600).c_str(), "", showMergeMapWidget))
				{
					showMergeMapWidget = !showMergeMapWidget;
				}
				if (ImGui::MenuItem("Map Overview", "", showOverviewWidget))
				{
					showOverviewWidget = !showOverviewWidget;
				}
				if (ImGui::MenuItem(get_localized_string(LANG_1096).c_str(), "", showLogWidget))
				{
					showLogWidget = !showLogWidget;
				}
			}
			ImGui::EndMenu();
		}

		AS_OnGuiTick();

		if (ImGui::BeginMenu(get_localized_string(LANG_0601).c_str()))
		{
#ifdef WIN32
			if (ImGui::MenuItem("Console", NULL, &g_console_visible))
			{
				showConsoleWindow(g_console_visible);
			}
#endif
			Bsp* selectedMap = app->getSelectedMap();
			for (BspRenderer* bspRend : mapRenderers)
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
				if (ImGui::MenuItem("Print textures"))
				{
					for (int i = 0; i < map->textureCount; i++)
					{
						int mip_offset = ((int*)map->textures)[i + 1];
						const char* name = "";
						int data_offset = 0;
						if (mip_offset >= 0)
						{
							BSPMIPTEX* tex = (BSPMIPTEX*)(map->textures + mip_offset);
							data_offset = tex->nOffsets[0];
							name = tex->szName;
							int colors = -1;
							if (tex->nOffsets[0] > 0)
							{
								int w = tex->nWidth;
								int h = tex->nHeight;

								int szAll = calcMipsSize(w, h);

								unsigned char* texdata = (unsigned char*)(((unsigned char*)tex) + tex->nOffsets[0]);
								colors = (int)*(unsigned short*)(texdata + szAll);
							}
							print_log("mip name \"{}\" offset {} data offset {}-{}-{}-{} size {}x{} colors {}\n", name, mip_offset, tex->nOffsets[0],
								tex->nOffsets[1], tex->nOffsets[2], tex->nOffsets[3]
								, tex->nWidth, tex->nHeight, colors);
						}
						else
						{
							print_log("mip name \"BAD NAME\" offset {} data offset NO DATA OFFSET\n", name, mip_offset, data_offset);
						}
					}
				}

				if (ImGui::MenuItem("CREATE SKYBOX"))
				{
					map->remove_faces_by_content(CONTENTS_SOLID);
					map->remove_faces_by_content(CONTENTS_SKY);

					for (int f = map->faceCount - 1; f >= 0; f--)
					{
						BSPFACE32 face = map->faces[f];

						if (face.iTextureInfo >= 0)
						{
							BSPTEXTUREINFO texinfo = map->texinfos[face.iTextureInfo];
							if (texinfo.iMiptex >= 0)
							{
								int texOffset = ((int*)map->textures)[texinfo.iMiptex + 1];
								if (texOffset >= 0)
								{
									BSPMIPTEX tex = *((BSPMIPTEX*)(map->textures + texOffset));
									std::string texname = toLowerCase(tex.szName);
									if (starts_with(texname, "sky"))
									{
										map->remove_face(f);
									}
								}
							}
						}
					}

					rend->loadLightmaps();
					rend->preRenderFaces();

					map->update_ent_lump();
					map->update_lump_pointers();

					vec3 org_mins = vec3(-256.0f, -256.0f, -256.0f), org_maxs = vec3(256.0f, 256.0f, 256.0f);
					float scale_val = ((FLT_MAX_COORD - 2.0f) / 256.0f);

					//map->get_bounding_box(mins, maxs);
					int newModelIdx = ImportModel(map, "./primitives/skybox.bsp", true);

					Entity* newEnt = new Entity("func_wall");

					newEnt->addKeyvalue("model", "*" + std::to_string(newModelIdx));
					map->ents.push_back(newEnt);

					for (auto& ent : map->ents)
					{
						if (ent->isWorldSpawn())
						{
							ent->setOrAddKeyvalue("MaxRange", std::to_string((int)(FLT_MAX_COORD * 2.0f + 1.0f)));
						}
					}

					map->update_ent_lump();

					if (map->ents.size() > 0)
					{
						rend->refreshEnt((int)(map->ents.size()) - 1);
					}

					//./primitives/skytest/sky_up.png
					//./primitives/skytest/sky_dn.png
					//./primitives/skytest/sky_ft.png
					//./primitives/skytest/sky_bk.png
					//./primitives/skytest/sky_fl.png
					//./primitives/skytest/sky_rt.png

					// up 
					{
						unsigned char* sky_data = NULL;
						unsigned int w, h;
						lodepng_decode24_file(&sky_data, &w, &h, "./primitives/skytest/sky_up.png");
						int out_w, out_h;
						auto images = splitImage((COLOR3*)sky_data, w, h, 4, 4, out_w, out_h);
						const int new_w = 256, new_h = 256;
						for (auto& img : images)
						{
							std::vector<COLOR3> new_img;
							scaleImage(img.data(), new_img, out_w, out_h, new_w, new_h);
							img = new_img;
						}
						out_w = new_w;
						out_h = new_h;

						print_log("Split {}x{} to {} images with size {}x{}\n", w, h, images.size(), out_w, out_h);
						for (int x = 0; x < 4; x++)
						{
							for (int y = 0; y < 4; y++)
							{
								auto img = getSubImage(images, x, y, 4);
								lodepng_encode24_file(("test-" + std::to_string(x) + "-" + std::to_string(y) + ".png").c_str(), (unsigned char*)img.data(), out_w, out_h);
							}
						}

						for (int x = 0; x < 4; x++)
						{
							for (int y = 0; y < 4; y++)
							{
								std::string sky_side = "box_up_" + std::to_string(x) + "x" + std::to_string(y);

								auto target_img = getSubImage(images, x, y, 4);
								if (GetImageColors(target_img.data(), new_w * new_h) > 256)
								{
									COLOR3 palette[256];
									unsigned int colorCount = 0;
									if (!map->is_texture_has_pal)
									{
										if (g_settings.pal_id >= 0)
										{
											colorCount = g_settings.palettes[g_settings.pal_id].colors;
											memcpy(palette, g_settings.palettes[g_settings.pal_id].data, g_settings.palettes[g_settings.pal_id].colors * sizeof(COLOR3));
										}
										else
										{
											colorCount = 256;
											memcpy(palette, g_settings.palette_default, 256 * sizeof(COLOR3));
										}
									}
									else
									{
										colorCount = 0;
									}

									COLOR3* newTex = new COLOR3[new_w * new_h];
									memcpy(newTex, target_img.data(), (new_w * new_h) * sizeof(COLOR3));

									Quantizer* tmpCQuantizer = new Quantizer(256, 8);
									if (colorCount != 0)
										tmpCQuantizer->SetColorTable(palette, 256);
									tmpCQuantizer->ApplyColorTable((COLOR3*)newTex, new_w * new_h);
									delete tmpCQuantizer;


									lodepng_encode24_file(("testQuantizer-" + std::to_string(x) + "-" + std::to_string(y) + ".png").c_str(), (unsigned char*)newTex, out_w, out_h);

									map->add_texture(sky_side.c_str(), (unsigned char*)newTex, new_w, new_h);
									delete[] newTex;
								}
								else
									map->add_texture(sky_side.c_str(), (unsigned char*)target_img.data(), new_w, new_h);
							}
						}
					}

					STRUCTUSAGE modelUsage = STRUCTUSAGE(map);
					map->mark_model_structures(newModelIdx, &modelUsage, true);

					map->models[newModelIdx].nMaxs *= scale_val;
					map->models[newModelIdx].nMins *= scale_val;

					for (int i = 0; i < map->vertCount; i++)
					{
						if (modelUsage.verts[i])
						{
							map->verts[i] *= scale_val;
						}
					}

					for (int i = 0; i < map->texinfoCount; i++)
					{
						if (modelUsage.texInfo[i])
						{
							mat4x4 scaleMat;
							scaleMat.loadIdentity();
							scaleMat.scale(1.0f / 2.0f, 1.0f / 2.0f, 1.0f / 2.0f);
							BSPTEXTUREINFO& info = map->texinfos[i];

							info.vS = (scaleMat * vec4(info.vS, 1)).xyz();
							info.vT = (scaleMat * vec4(info.vT, 1)).xyz();

							info.shiftS *= 2.0f;
							info.shiftT *= 2.0f;
						}
					}

					for (int i = 0; i < map->texinfoCount; i++)
					{
						if (modelUsage.texInfo[i])
						{
							mat4x4 scaleMat;
							scaleMat.loadIdentity();
							scaleMat.scale(1.0f / scale_val, 1.0f / scale_val, 1.0f / scale_val);
							BSPTEXTUREINFO& info = map->texinfos[i];

							info.vS = (scaleMat * vec4(info.vS, 1)).xyz();
							info.vT = (scaleMat * vec4(info.vT, 1)).xyz();

							//float shiftS = info.shiftS;
							//float shiftT = info.shiftT;

							//// magic guess-and-check code that somehow works some of the time
							//// also its shit
							//for (int k = 0; k < 3; k++)
							//{
							//	vec3 stretchDir;
							//	if (k == 0) stretchDir = vec3(1.0f, 0, 0).normalize();
							//	if (k == 1) stretchDir = vec3(0, 1.0f, 0).normalize();
							//	if (k == 2) stretchDir = vec3(0, 0, 1.0f).normalize();

							//	float refDist = 0;
							//	if (k == 0) refDist = scaleFromDist.x;
							//	if (k == 1) refDist = scaleFromDist.y;
							//	if (k == 2) refDist = scaleFromDist.z;

							//	vec3 texFromDir;
							//	if (k == 0) texFromDir = dir * vec3(1, 0, 0);
							//	if (k == 1) texFromDir = dir * vec3(0, 1, 0);
							//	if (k == 2) texFromDir = dir * vec3(0, 0, 1);

							//	float dotS = dotProduct(oldinfo.oldS.normalize(), stretchDir);
							//	float dotT = dotProduct(oldinfo.oldT.normalize(), stretchDir);

							//	float dotSm = dotProduct(texFromDir, info.vS) < 0 ? 1.0f : -1.0f;
							//	float dotTm = dotProduct(texFromDir, info.vT) < 0 ? 1.0f : -1.0f;

							//	// hurr dur oh god im fucking retarded huurr
							//	if (k == 0 && dotProduct(texFromDir, fromDir) < 0 != fromDir.x < 0)
							//	{
							//		dotSm *= -1.0f;
							//		dotTm *= -1.0f;
							//	}
							//	if (k == 1 && dotProduct(texFromDir, fromDir) < 0 != fromDir.y < 0)
							//	{
							//		dotSm *= -1.0f;
							//		dotTm *= -1.0f;
							//	}
							//	if (k == 2 && dotProduct(texFromDir, fromDir) < 0 != fromDir.z < 0)
							//	{
							//		dotSm *= -1.0f;
							//		dotTm *= -1.0f;
							//	}

							//	float vsdiff = info.vS.length() - oldinfo.oldS.length();
							//	float vtdiff = info.vT.length() - oldinfo.oldT.length();

							//	shiftS += (refDist * vsdiff * abs(dotS)) * dotSm;
							//	shiftT += (refDist * vtdiff * abs(dotT)) * dotTm;
							//}

							//info.shiftS = shiftS;
							//info.shiftT = shiftT;

							//map->texinfos[i].vS /= scale_val;
							//map->texinfos[i].vT /= scale_val;
						}
					}
					for (int i = 0; i < map->nodeCount; i++)
					{
						if (modelUsage.nodes[i])
						{
							map->nodes[i].nMaxs *= scale_val;
							map->nodes[i].nMins *= scale_val;
						}
					}
					for (int i = 0; i < map->leafCount; i++)
					{
						if (modelUsage.leaves[i])
						{
							map->leaves[i].nMaxs *= scale_val;
							map->leaves[i].nMins *= scale_val;
						}
					}
					for (int i = 0; i < map->planeCount; i++)
					{
						if (modelUsage.planes[i])
						{
							map->planes[i].fDist *= scale_val;
						}
					}

					rend->reuploadTextures();
					rend->preRenderFaces();
					rend->pushModelUndoState("CREATE SKYBOX", EDIT_MODEL_LUMPS | FL_ENTITIES);
				}


				ImGui::EndMenu();
			}
		}

		ImGui::EndMainMenuBar();
	}

	if (ImGui::BeginViewportSideBar("BottomBar", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetTextLineHeightWithSpacing(), ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar))
	{
		if (ImGui::BeginMenuBar())
		{
			Bsp* selectedMap = app->getSelectedMap();
			ImGui::TextUnformatted(fmt::format("Origin [{:^5},{:^5},{:^5}]", floatRound(cameraOrigin.x), floatRound(cameraOrigin.y), floatRound(cameraOrigin.z)).c_str());

			vec3 hlAngles = cameraAngles;
			hlAngles = hlAngles.unflipUV();
			hlAngles = hlAngles.normalize_angles();
			hlAngles.y -= 90.0f;

			ImGui::TextUnformatted(fmt::format("Angles [{:^4},{:^4},{:^4}]", floatRound(hlAngles.x), floatRound(hlAngles.y), floatRound(hlAngles.z)).c_str());

			if (selectedMap)
			{
				if (rend)
				{
					ImGui::TextUnformatted(fmt::format("Click [{:^5},{:^5},{:^5}]", floatRound(rend->intersectVec.x), floatRound(rend->intersectVec.y), floatRound(rend->intersectVec.z)).c_str());

					ImGui::TextUnformatted(fmt::format("Leaf [{}]", rend->curLeafIdx).c_str());

					ImGui::TextUnformatted(fmt::format("Dist [{:^5}]", floatRound(rend->intersectDist)).c_str());
				}

				if (g_app->pickInfo.selectedEnts.size() == 1 && (size_t)g_app->pickInfo.selectedEnts[0] < selectedMap->ents.size())
				{
					ImGui::TextUnformatted(fmt::format("Classname [{}]", selectedMap->ents[g_app->pickInfo.selectedEnts[0]]->classname).c_str());
				}


				if (DebugKeyPressed && g_app->pickInfo.selectedFaces.size() == 1)
				{
					int face = g_app->pickInfo.selectedFaces[0];
					RenderFace* rface;
					RenderGroup* rgroup;
					rend->getRenderPointers(face, &rface, &rgroup);

					if (rface && rgroup)
					{
						ImGui::TextUnformatted(fmt::format("Rend group [{}]", rface->group).c_str());
					}
				}
			}

			ImGui::EndMenuBar();
		}
		ImGui::End();
	}
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
		style.FrameBorderSize = 1.0f;
		ImGuiContext& g = *GImGui;
		ImVec4 dimColor = style.Colors[ImGuiCol_FrameBg];
		ImVec4 selectColor = style.Colors[ImGuiCol_FrameBgActive];
		float iconWidth = (fontSize / 22.0f) * 32;
		ImVec2 iconSize = ImVec2(iconWidth, iconWidth);
		ImVec2 iconSize_big = ImVec2(iconWidth * 2, iconWidth * 2);

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
		ImGui::PushStyleColor(ImGuiCol_Border, app->pickMode == PICK_OBJECT ? dimColor : selectColor);
		if (ImGui::ImageButton("##pickobj", (ImTextureID)(size_t)objectIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1)))
		{
			if (app->pickMode != PICK_OBJECT)
			{
				app->deselectFaces();
				app->deselectObject();
				app->pickMode = PICK_OBJECT;
				showFaceEditWidget = false;
			}
		}
		ImGui::PopStyleColor(2);
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
		{
			ImGui::BeginTooltip();
			ImGui::ImageButton("##pickobj_big", (ImTextureID)(size_t)objectIconTexture->id, iconSize_big, ImVec2(0, 0), ImVec2(1, 1));
			ImGui::TextUnformatted(get_localized_string(LANG_0607).c_str());
			ImGui::EndTooltip();
		}

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_FACE ? selectColor : dimColor);
		ImGui::PushStyleColor(ImGuiCol_Border, app->pickMode == PICK_FACE ? dimColor : selectColor);
		ImGui::SameLine();
		if (ImGui::ImageButton("##pickface", (ImTextureID)(size_t)faceIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1)))
		{
			if (app->pickMode == PICK_OBJECT && app->pickInfo.selectedEnts.size() > 1)
			{
				app->deselectObject(true);
				pickCount++;
			}
			showFaceEditWidget = true;
			app->pickMode = PICK_FACE;
		}
		ImGui::PopStyleColor(2);
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
		{
			ImGui::BeginTooltip();
			ImGui::ImageButton("##pickface_big", (ImTextureID)(size_t)faceIconTexture->id, iconSize_big, ImVec2(0, 0), ImVec2(1, 1));
			ImGui::TextUnformatted(get_localized_string(LANG_0608).c_str());
			ImGui::EndTooltip();
		}

		ImGui::PushStyleColor(ImGuiCol_Button, app->pickMode == PICK_FACE_LEAF ? selectColor : dimColor);
		ImGui::SameLine();
		if (ImGui::ImageButton("##pickleaf", (ImTextureID)(size_t)leafIconTexture->id, iconSize, ImVec2(0, 0), ImVec2(1, 1)))
		{
			if (app->pickMode == PICK_OBJECT && app->pickInfo.selectedEnts.size() > 1)
			{
				app->deselectObject(true);
				pickCount++;
			}
			showFaceEditWidget = true;
			app->pickMode = PICK_FACE_LEAF;
		}
		ImGui::PopStyleColor(1);
		if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
		{
			ImGui::BeginTooltip();
			ImGui::ImageButton("##pickleaf_big", (ImTextureID)(size_t)leafIconTexture->id, iconSize_big, ImVec2(0, 0), ImVec2(1, 1));
			ImGui::TextUnformatted(get_localized_string("FACE_LEAF_MODE").c_str());
			ImGui::EndTooltip();
		}
	}
	ImGui::End();
}

void Gui::drawFpsOverlay()
{
	ImVec2 window_pos = ImVec2(imgui_io->DisplaySize.x - 10.0f, 35.0f);
	ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);
	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
	ImGui::SetNextWindowBgAlpha(0.35f); // Transparent background
	if (ImGui::Begin(get_localized_string(LANG_0609).c_str(), 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
	{
		//ImGui::TextUnformatted(fmt::format("{} FPS", current_fps).c_str());
		ImGui::TextUnformatted(fmt::format("{:.0f} FPS", imgui_io->Framerate).c_str());
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
		if (map && i > 0 && (map->ents[i]->getBspModelIdx() < 0 || map->ents[i]->isWorldSpawn()))
		{
			selectedEntity = true;
			break;
		}
	}

	bool showStatus = (app->invalidSolid && selectedEntity) || (!app->isTransformableSolid && selectedEntity) || badSurfaceExtents || lightmapTooLarge || app->modelUsesSharedStructures;

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

	if (!app)
		return;

	ImGui::SetNextWindowBgAlpha(0.75f);

	ImGui::SetNextWindowSize(ImVec2(300.f, 400.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200.f, 200.f), ImVec2(app->windowWidth - 40.f, app->windowHeight - 40.f));

	Bsp* map = app->getSelectedMap();
	BspRenderer* renderer = map ? map->getBspRender() : NULL;
	auto entIdx = app->pickInfo.selectedEnts;

	if (ImGui::Begin(fmt::format("{}###DEBUG_WIDGET", get_localized_string(LANG_0624)).c_str(), &showDebugWidget))
	{
		if (ImGui::CollapsingHeader(get_localized_string(LANG_0625).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0366)), floatRound(cameraOrigin.x), floatRound(cameraOrigin.y), floatRound(cameraOrigin.z)).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0367)), floatRound(cameraAngles.x), floatRound(cameraAngles.y), floatRound(cameraAngles.z)).c_str());

			vec3 hlAngles = cameraAngles;
			hlAngles = hlAngles.unflipUV();
			hlAngles = hlAngles.normalize_angles();
			hlAngles.y -= 90.0f;

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string("DEBUG_HL_ANGLES")), floatRound(hlAngles.x), floatRound(hlAngles.y), floatRound(hlAngles.z)).c_str());

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0368)), (unsigned int)app->pickInfo.selectedFaces.size()).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0369)), app->pickMode).c_str());
		}

		if (ImGui::CollapsingHeader("DEBUG INFO", ImGuiTreeNodeFlags_None))
		{
			ImGui::Text(fmt::format("Mouse: {} {}", mousePos.x, mousePos.y).c_str());
			ImGui::Text(fmt::format("Workdir: {}", g_working_dir).c_str());
			if (imgui_io)
			{
				ImGui::Text(fmt::format("Opengl Errors: {} ", app->gl_errors).c_str());
				if (renderer)
					ImGui::Text(fmt::format("lmGen: {}.lmUpload: {}.lm: {}.", renderer->lightmapsGenerated, renderer->lightmapsUploaded, renderer->lightmaps != NULL).c_str());
				ImGui::Text(fmt::format("Mouse left {} right {}", app->curLeftMouse, app->curRightMouse).c_str());
				std::string keysStr;
				for (int key = 0; key < GLFW_KEY_LAST; key++) {
					if (app->pressed[key]) {
						const char* keyName = glfwGetKeyName(key, 0);
						if (keyName != nullptr) {
							keysStr += std::string(keyName) + " ";
						}
						else
						{
							keysStr += "C:" + std::to_string(key) + " ";
						}
					}
				}

				ImGui::Text("Pick count: %d. \nVert pick count: %d", pickCount, vertPickCount);
				ImGui::Text("Model verts: %d. \nModel faces: %d", app->modelVerts.size(), app->modelFaceVerts.size());
				ImGui::Text("KEYS: %s", keysStr.c_str());
				ImGui::Text(fmt::format("Time: {}", (float)app->curTime).c_str());
				ImGui::Text(fmt::format("canControl:{}\noldControl:{}\nNo WantTextInput:{}", app->canControl, app->oldControl, !imgui_io->WantTextInput).c_str());
				ImGui::Text(fmt::format("No WantCaptureMouseUnlessPopupClose:{}", !imgui_io->WantCaptureMouseUnlessPopupClose).c_str());
				ImGui::Text(fmt::format("No WantCaptureMouse:{}", !imgui_io->WantCaptureMouse).c_str());
				//ImGui::Text(fmt::format("BlockMoving:{}", app->blockMoving).c_str());
				ImGui::Text(fmt::format("MoveDir: [{}]", app->getMoveDir().toString()).c_str());

				static double movemulttime = app->curTime;
				static double movemult = (app->curTime - app->oldTime) * app->moveSpeed;
				static vec3 nextOrigin = app->getMoveDir() * (float)(app->curTime - app->oldTime) * app->moveSpeed;

				if (fabs(app->curTime - movemulttime) > 0.5)
				{
					movemult = (app->curTime - app->oldTime) * app->moveSpeed;
					movemulttime = app->curTime;
					nextOrigin = app->getMoveDir() * (float)(app->curTime - app->oldTime) * app->moveSpeed;
				}

				ImGui::Text(fmt::format("MoveDir mult: [{}]", movemult).c_str());
				ImGui::Text(fmt::format("MoveSpeed: [{}]", app->moveSpeed).c_str());
				ImGui::Text(fmt::format("nextOrigin: [{}]", nextOrigin.toString()).c_str());
			}
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
						ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0371)), entIdx[0]).c_str());
					}

					int modelIdx = -1;

					if (entIdx.size())
					{
						modelIdx = map->ents[entIdx[0]]->getBspModelIdx();
					}


					if (modelIdx > 0)
					{
						ImGui::Checkbox(get_localized_string(LANG_0628).c_str(), &app->debugClipnodes);
						ImGui::SliderInt(get_localized_string(LANG_0629).c_str(), &app->debugInt, 0, app->debugIntMax);

						ImGui::Checkbox(get_localized_string(LANG_0630).c_str(), &app->debugNodes);
						ImGui::SliderInt(get_localized_string(LANG_0631).c_str(), &app->debugNode, 0, app->debugNodeMax);
					}

					if (modelIdx >= 0)
					{
						ImGui::TextUnformatted(fmt::format("Model{}.FirstFace:{}", modelIdx, map->models[modelIdx].iFirstFace).c_str());
						ImGui::TextUnformatted(fmt::format("Model{}.NumFace:{}", modelIdx, map->models[modelIdx].nFaces).c_str());
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

		if (map && entIdx.size())
		{
			modelIdx = map->ents[entIdx[0]]->getBspModelIdx();
		}

		std::string bspTreeTitle = "BSP Tree";
		if (modelIdx >= 0)
		{
			bspTreeTitle += " (Model " + std::to_string(modelIdx) + ")";
		}

		if (ImGui::CollapsingHeader((bspTreeTitle + "##bsptree").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (modelIdx < 0 && entIdx.size())
				modelIdx = 0;
			if (modelIdx >= 0)
			{
				if (!map || !renderer)
				{
					ImGui::Text(get_localized_string(LANG_0632).c_str());
				}
				else
				{
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
						int contents = map->pointContents(headNode, renderer->localCameraOrigin, i, nodeBranch, leafIdx, childIdx);

						ImGui::PushStyleColor(ImGuiCol_Text, hullColors[i]);
						if (ImGui::TreeNode(("HULL " + std::to_string(i)).c_str()))
						{
							ImGui::Indent();
							ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0380)), map->getLeafContentsName(contents)).c_str());
							if (i == 0)
							{
								ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0381)), leafIdx).c_str());
							}
							else if (i == 3 && g_app->debugLeafNavMesh) {
								int tmpLeafIdx = map->get_leaf(renderer->localCameraOrigin, 3);
								int leafNavIdx = -1;

								if (tmpLeafIdx >= 0 && tmpLeafIdx < MAX_MAP_CLIPNODE_LEAVES) {
									leafNavIdx = g_app->debugLeafNavMesh->leafMap[tmpLeafIdx];
								}

								ImGui::Text("Nav ID: %d", leafNavIdx);
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
			ImGui::Text(get_localized_string(LANG_0635).c_str(), renderer->intersectVec.x, renderer->intersectVec.y, renderer->intersectVec.z);
			ImGui::Text(get_localized_string(LANG_0636).c_str(), app->debugVec1.x, app->debugVec1.y, app->debugVec1.z);
			ImGui::Text(get_localized_string(LANG_0637).c_str(), app->debugVec2.x, app->debugVec2.y, app->debugVec2.z);
			ImGui::Text(get_localized_string(LANG_0638).c_str(), app->debugVec3.x, app->debugVec3.y, app->debugVec3.z);

			float mb = renderer->undoMemoryUsage / (1024.0f * 1024.0f);
			ImGui::Text(get_localized_string(LANG_0639).c_str(), mb);

			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0388)), app->isTransformableSolid).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0389)), app->isScalingObject).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0390)), app->isMovingOrigin).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0391)), app->isTransformingValid).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0392)), app->isTransformingWorld).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0393)), app->transformMode).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0394)), app->transformTarget).c_str());
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0395)), app->modelUsesSharedStructures).c_str());

			ImGui::Text(fmt::format("showDragAxes {}\nmovingEnt {}\nanyAltPressed {}",
				app->showDragAxes, app->movingEnt, app->anyAltPressed).c_str());

			ImGui::Text(fmt::format("hoverAxis:{}", app->hoverAxis).c_str());

			ImGui::Text(fmt::format("anyVertSelected:{}", app->anyVertSelected).c_str());
			ImGui::Text(fmt::format("anyEdgeSelected:{}", app->anyEdgeSelected).c_str());
			ImGui::Text(fmt::format("hoverEdge:{}", app->hoverEdge).c_str());
			ImGui::Text(fmt::format("hoverVert:{}", app->hoverVert).c_str());
			ImGui::Text(fmt::format("pickClickHeld:{}", app->pickClickHeld).c_str());

			ImGui::Checkbox(get_localized_string(LANG_0640).c_str(), &app->showDragAxes);
		}

		if (map)
		{
			if (ImGui::Button(get_localized_string(LANG_0641).c_str()))
			{
				for (auto& ent : map->ents)
				{
					if (ent->hasKey("classname") && ent->keyvalues["classname"] == "infodecal"
						&& ent->hasKey("texture"))
					{
						map->decalShoot(ent->origin, ent->keyvalues["texture"]);
					}
				}
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
					map->swap_two_models(model1, model2);
				}
			}

			if (ImGui::Button("Select faces pos plane"))
			{

				int leafIdx = 0;
				int planeIdx = -1;

				map->pointLeaf(map->models[0].iHeadnodes[0], cameraOrigin, 0, leafIdx, planeIdx);
				if (planeIdx >= 0)
				{
					auto faces = map->getFacesFromPlane(planeIdx);
					for (auto& f : faces)
					{
						renderer->highlightFace(f, 2);
					}
				}
			}

			ImGui::TextUnformatted("BEST LEAFS:");
			auto leaf_list = map->getLeafsFromPos(cameraOrigin, 32);
			for (auto& f : leaf_list)
			{
				ImGui::TextUnformatted(std::to_string(f).c_str());
			}

			if (ImGui::Button("Select best face"))
			{
				float minDist = 128.0f;
				int minFace = -1;

				for (int f = 0; f < map->faceCount; f++)
				{
					BSPFACE32& face = map->faces[f];

					if (map->texinfos[face.iTextureInfo].nFlags & TEX_SPECIAL) {
						continue;
					}

					auto& faceMath = renderer->faceMaths[f];

					vec3 normal = faceMath.normal.normalize();

					float distanceToPlane = dotProduct(normal, cameraOrigin) - faceMath.fdist;
					float dot = std::fabs(distanceToPlane);

					if (dot > minDist)
					{
						continue;
					}

					bool isInsideFace = true;
					const std::vector<vec3>& vertices = map->get_face_verts(f);

					for (size_t i = 0; i < vertices.size(); i++) {
						const vec3& v0 = vertices[i];
						const vec3& v1 = vertices[(i + 1) % vertices.size()];

						vec3 edge = v1 - v0;

						vec3 edgeNormal = crossProduct(normal, edge).normalize();

						if (dotProduct(edgeNormal, cameraOrigin - v0) > 0) {
							isInsideFace = false;
							break;
						}
					}

					if (!isInsideFace)
					{
						continue;
					}

					if (dot < minDist) {
						minDist = dot;
						minFace = f;
					}
				}
				if (minFace >= 0)
				{
					renderer->highlightFace(minFace, 2);

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
}

void Gui::drawOverviewWidget()
{
	static Bsp* lastMap = NULL;
	static bool orthoMode = true;
	static bool updateFarNear = false;
	static std::string imgFormat = ".tga";
	if (updateFarNear)
	{
		updateFarNear = false;
		ortho_near = (ortho_maxs.z - ortho_mins.z) + cameraOrigin.z;
		ortho_far = (ortho_maxs.z - ortho_mins.z) * 2 + cameraOrigin.z;
	}

	ortho_overview = orthoMode;

	Bsp* map = app->getSelectedMap();

	if (ImGui::Begin("Overview Widget###OVERVIEW_MAKER", &showOverviewWidget, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (lastMap != map)
		{
			lastMap = map;
			if (map)
				map->get_model_vertex_bounds(0, ortho_mins, ortho_maxs);
		}

		if (!map)
		{
			ImGui::Text("No selected map");
			ImGui::End();
			return;
		}

		/*		ImGui::SeparatorText("Custom Window Size");
				ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.3f);
				ImGui::DragFloat("Width", &ortho_custom_w, 1.0f, 256.0f, 2048.0f, "%.0f");
				ImGui::SameLine();
				ImGui::DragFloat("Height", &ortho_custom_h, 1.0f, 256.0f, 2048.0f, "%.0f");
				ImGui::PopItemWidth();
				*/
		ImGui::SeparatorText("Overview Settings");
		ImGui::Checkbox("Show Overview", &orthoMode);
		ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.3f);
		ImGui::DragFloat("Aspect Ratio", &ortho_custom_aspect, 0.01f, 0.5f, 2.0f, "%.2f");
		ImGui::DragFloat("Ortho FOV", &ortho_fov, 0.1f, 0.01f, 200.0f, "%.2f");
		ImGui::DragFloat("Ortho Near", &ortho_near, 1.0f, -1.0f, 8192.0f, "%.2f");
		ImGui::DragFloat("Ortho Far", &ortho_far, 1.0f, -1.0f, FLT_MAX, "%.2f");
		ImGui::PopItemWidth();

		ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.7f);
		ImGui::DragFloat3("Mins", &ortho_mins.x, 1.0f, -FLT_MAX_COORD, FLT_MAX_COORD, "%.0f");
		ImGui::DragFloat3("Maxs", &ortho_maxs.x, 1.0f, -FLT_MAX_COORD, FLT_MAX_COORD, "%.0f");
		ImGui::DragFloat3("Offset", &ortho_offset.x, 1.0f, -FLT_MAX_COORD, FLT_MAX_COORD, "%.0f");
		ImGui::PopItemWidth();

		ImGui::SeparatorText("Fill Overview Mins/Maxs");
		if (ImGui::Button("Fill from Model[0]")) {
			map->get_bounding_box(ortho_mins, ortho_maxs);
		}
		if (ImGui::Button("Fill from Verts")) {
			map->get_model_vertex_bounds(0, ortho_mins, ortho_maxs, true);
		}
		if (ImGui::Button("Fill from Leaves")) {
			ortho_mins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
			ortho_maxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (int i = 1; i < map->leafCount; i++)
			{
				if (map->leaves[i].nContents == CONTENTS_EMPTY || map->leaves[i].nContents == CONTENTS_WATER)
				{
					expandBoundingBox(map->leaves[i].nMins, ortho_mins, ortho_maxs);
					expandBoundingBox(map->leaves[i].nMaxs, ortho_mins, ortho_maxs);
				}
			}
		}

		if (ImGui::Button("Calculate Far/Near")) {
			updateFarNear = true;
		}

		ImGui::SeparatorText("Save to TGA");
		ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.3f);
		ImGui::DragInt("Width###2", &ortho_tga_w, 1.0f, 256, 4096);
		ImGui::SameLine();
		ImGui::DragInt("Height###3", &ortho_tga_h, 1.0f, 256, 4096);
		ImGui::PopItemWidth();

		if (ImGui::Button("Save .tga")) {
			ortho_save_tga = true;
			imgFormat = ".tga";
		}
		ImGui::SameLine();
		if (ImGui::Button("Save .bmp"))
		{
			ortho_save_bmp = true;
			imgFormat = ".bmp";
		}
		ImGui::SameLine();


		float x_size = ortho_maxs.x - ortho_mins.x;
		float y_size = ortho_maxs.y - ortho_mins.y;
		float zoomFriction = ((float)ortho_tga_w / (float)ortho_tga_h);
		float xScale, yScale;
		bool rotated = false;

		if (x_size < y_size) {
			xScale = 8192.0f / (x_size * zoomFriction);
			yScale = 8192.0f / y_size;
		}
		else {
			rotated = true;
			xScale = 8192.0f / x_size;
			yScale = 8192.0f / (y_size * zoomFriction);
		}

		float zoom = (xScale < yScale) ? xScale : yScale;

		vec3 origin = vec3((ortho_mins.x + ortho_maxs.x) / 2.0f + ortho_offset.x,
			(ortho_mins.y + ortho_maxs.y) / 2.0f + ortho_offset.y,
			(ortho_mins.z + ortho_maxs.z) / 2.0f + ortho_offset.z);

		if (ImGui::Button("Save .txt"))
		{
			FILE* overfile = NULL;
			fopen_s(&overfile, (g_working_dir + map->bsp_name + ".txt").c_str(), "wb");
			if (overfile)
			{
				fprintf(overfile, "// overview description file for %s\n\n", map->bsp_name.c_str());
				fprintf(overfile, "global \n{\n");
				fprintf(overfile, "\tZOOM\t%.2f\n", zoom);
				fprintf(overfile, "\tORIGIN\t%.2f\t%.2f\t%.2f\n", origin.x, origin.y, ortho_mins.z + ortho_offset.z);
				fprintf(overfile, "\tROTATED\t%i\n}\n\n", rotated ? 1 : 0);
				fprintf(overfile, "layer \n{\n");
				fprintf(overfile, "\tIMAGE\t\"overviews/%s%s\"\n", map->bsp_name.c_str(), imgFormat.c_str());
				fprintf(overfile, "\tHEIGHT\t%.2f\n}\n", ortho_mins.z + ortho_offset.z);
				fclose(overfile);
				print_log("Saved to {}\n", g_working_dir + map->bsp_name + ".txt");
			}
		}
		ImGui::SeparatorText("DEV INFO");


		ImGui::Text("Overview: Zoom %.2f", zoom);

		ImGui::Text("Height: %2.f", ortho_mins.z + ortho_offset.z);

		ImGui::Text("Map Origin: (%.2f, %.2f, %.2f)",
			(ortho_mins.x + ortho_maxs.x) / 2.0f + ortho_offset.x,
			(ortho_mins.y + ortho_maxs.y) / 2.0f + ortho_offset.y,
			(ortho_mins.z + ortho_maxs.z) / 2.0f + ortho_offset.z);

		ImGui::Text("Z Min: %.2f, Z Max: %.2f", ortho_near, ortho_far);

		ImGui::Text("Rotated: %s", rotated ? "true" : "false");

		ImGui::Text("X Scale: %.2f, Y Scale: %.2f", xScale, yScale);

		/*if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (std::fabs(ortho_custom_w) > EPSILON && ortho_custom_w < 256.0f)
				ortho_custom_w = 256.0f;

			if (std::fabs(ortho_custom_h) > EPSILON && ortho_custom_h < 256.0f)
				ortho_custom_h = 256.0f;
		}
		*/
	}
	ImGui::End();
}


void Gui::drawTextureBrowser()
{
	Bsp* map = app->getSelectedMap();
	BspRenderer* mapRender = map ? map->getBspRender() : NULL;
	ImGui::SetNextWindowSize(ImVec2(610.f, 610.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(300.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));
	//ImGui::SetNextWindowContentSize(ImVec2(550, 0.0f));
	if (ImGui::Begin(fmt::format("{}###TEXTURE_BROWSER", get_localized_string(LANG_0651)).c_str(), &showTextureBrowser, 0))
	{
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
	if (ImGui::Begin(fmt::format("{}###KEYVALUE_WIDGET", get_localized_string(LANG_1103)).c_str(), &showKeyvalueWidget, 0))
	{
		auto entIdx = app->pickInfo.selectedEnts;


		Bsp* map = app->getSelectedMap();
		if (entIdx.size() && app->fgd
			&& !app->isLoading && !app->isModelsReloading && !app->reloading && map)
		{

			//ImGui::TextUnformatted(fmt::format("ENTS {}. FIRST ENT {}.", g_app->pickInfo.selectedEnts.size(), g_app->pickInfo.selectedEnts.size() ? g_app->pickInfo.selectedEnts[0] : -1).c_str());

			Entity* ent = map->ents[entIdx[0]];
			std::string cname = ent->keyvalues["classname"];
			FgdClass* fgdClass = app->fgd->getFgdClass(cname, FGD_CLASS_POINT);
			std::vector<FgdGroup> targetGroup = app->fgd->pointEntGroups;


			if (!fgdClass || (ent->hasKey("model") &&
				(starts_with(ent->keyvalues["model"], '*') || ends_with(toLowerCase(ent->keyvalues["model"]), ".bsp"))))
			{
				FgdClass* tmpfgdClass = app->fgd->getFgdClass(cname, FGD_CLASS_SOLID);
				if (tmpfgdClass)
				{
					targetGroup = app->fgd->solidEntGroups;
					fgdClass = tmpfgdClass;
				}
			}

			ImGui::PushFont(largeFont);
			ImGui::AlignTextToFramePadding();
			ImGui::Text(get_localized_string(LANG_0654).c_str()); ImGui::SameLine();
			if (cname != "worldspawn")
			{
				if (!targetGroup.size())
				{
					ImGui::BeginDisabled();
				}

				if (ImGui::Button((" " + cname + " ").c_str()))
					ImGui::OpenPopup("classname_popup");

				if (!targetGroup.size())
				{
					ImGui::EndDisabled();
				}
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


			if (ImGui::BeginPopup("classname_popup"))
			{
				ImGui::Text(get_localized_string(LANG_0656).c_str());
				ImGui::Separator();

				for (FgdGroup& group : targetGroup)
				{
					if (!group.classes.size())
					{
						ImGui::BeginDisabled();
					}

					if (ImGui::BeginMenu(group.groupName.c_str()))
					{
						for (size_t k = 0; k < group.classes.size(); k++)
						{
							if (ImGui::MenuItem(group.classes[k]->name.c_str()))
							{
								ent->setOrAddKeyvalue("classname", group.classes[k]->name);
								map->getBspRender()->refreshEnt((int)entIdx[0]);
								map->getBspRender()->pushEntityUndoStateDelay("Change Class", (int)entIdx[0], ent);
							}
						}

						ImGui::EndMenu();
					}

					if (!group.classes.size())
					{
						ImGui::EndDisabled();
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
					drawKeyvalueEditor_SmartEditTab((int)entIdx[0]);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_0659).c_str()))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_FlagsTab((int)entIdx[0]);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(get_localized_string(LANG_0660).c_str()))
				{
					ImGui::Dummy(ImVec2(0, 10));
					drawKeyvalueEditor_RawEditTab((int)entIdx[0]);
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();

		}
		else
		{
			if (entIdx.empty())
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
	Entity* sel_ent = map->ents[entIdx];
	std::string cname = sel_ent->keyvalues["classname"];
	FgdClass* fgdClass = app->fgd->getFgdClass(cname);
	ImGuiStyle& style = ImGui::GetStyle();

	ImGui::BeginChild(get_localized_string(LANG_0663).c_str());

	ImGui::Columns(2, get_localized_string(LANG_0664).c_str(), false); // 4-ways, with border

	float paddingx = style.WindowPadding.x + style.FramePadding.x;
	float inputWidth = (ImGui::GetWindowWidth() - (paddingx * 2)) * 0.5f;

	// needed if autoresize is true
	if (ImGui::GetScrollMaxY() > 0)
		inputWidth -= style.ScrollbarSize * 0.5f;

	struct InputDataKey
	{
		std::string key;
		std::string defaultValue;

		InputDataKey()
		{
			key.clear();
			defaultValue.clear();
		}
	};

	if (fgdClass)
	{
		static InputDataKey inputData[128];
		static int lastPickCount = 0;

		if (sel_ent->hasKey("model"))
		{
			bool foundmodel = false;
			for (size_t i = 0; i < fgdClass->keyvalues.size(); i++)
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

		for (size_t i = 0; i < fgdClass->keyvalues.size() && i < 128; i++)
		{
			KeyvalueDef& keyvalue = fgdClass->keyvalues[i];
			std::string key = keyvalue.name;
			if (key == "spawnflags")
			{
				continue;
			}
			std::string value = sel_ent->keyvalues[key];
			std::string niceName = keyvalue.shortDescription;

			if (!nullstrlen(value) && nullstrlen(keyvalue.defaultValue))
			{
				value = keyvalue.defaultValue;
			}

			if (niceName.size() >= MAX_KEY_LEN)
				niceName = niceName.substr(0, MAX_KEY_LEN - 1);
			if (value.size() >= MAX_VAL_LEN)
				value = value.substr(0, MAX_VAL_LEN - 1);

			inputData[i].key = key;
			inputData[i].defaultValue = keyvalue.defaultValue;

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
				ImGui::PushStyleColor(ImGuiCol_Text, { 0.9f,0.4f,0.2f,1.0f });
				ImGui::TextUnformatted(keyvalue.name.c_str());
				ImGui::SameLine();
				ImGui::TextUnformatted(" = ");
				ImGui::SameLine();
				ImGui::TextUnformatted(keyvalue.defaultValue.c_str());
				ImGui::PopStyleColor();
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::SetNextItemWidth(inputWidth);

			if (keyvalue.iType == FGD_KEY_CHOICES && !keyvalue.choices.empty())
			{
				std::string selectedValue = keyvalue.choices[0].name;
				int ikey = str_to_int(value);

				for (size_t k = 0; k < keyvalue.choices.size(); k++)
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
					for (size_t k = 0; k < keyvalue.choices.size(); k++)
					{
						KeyvalueChoice& choice = keyvalue.choices[k];
						bool selected = choice.svalue == value || (value.empty() && choice.svalue == keyvalue.defaultValue);
						bool needrefreshmodel = false;
						if (ImGui::Selectable(choice.name.c_str(), selected))
						{
							if (key == "renderamt")
							{
								if (sel_ent->hasKey("renderamt") && sel_ent->keyvalues["renderamt"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendermode")
							{
								if (sel_ent->hasKey("rendermode") && sel_ent->keyvalues["rendermode"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "renderfx")
							{
								if (sel_ent->hasKey("renderfx") && sel_ent->keyvalues["renderfx"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendercolor")
							{
								if (sel_ent->hasKey("rendercolor") && sel_ent->keyvalues["rendercolor"] != choice.svalue)
								{
									needrefreshmodel = true;
								}
							}
							BspRenderer* render = map->getBspRender();
							if (render)
							{
								if (g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] >= 0)
								{
									for (auto selected_entId : g_app->pickInfo.selectedEnts)
									{
										Entity* selected_ent = map->ents[selected_entId];
										selected_ent->setOrAddKeyvalue(key, choice.svalue);
										map->getBspRender()->refreshEnt((int)selected_entId);

										if (needrefreshmodel)
										{
											if (selected_ent->getBspModelIdx() > 0)
											{
												map->getBspRender()->refreshModel(selected_ent->getBspModelIdx());
												map->getBspRender()->refreshEnt(selected_entId);
											}
										}

										map->getBspRender()->pushEntityUndoStateDelay("Edit Keyvalue", (int)selected_entId, selected_ent);
									}
								}
							}
							pickCount++;
							vertPickCount++;

							g_app->updateEntConnections();
						}

						if (choice.fullDescription.size())
						{
							if (ImGui::IsItemHovered())
							{
								ImGui::BeginTooltip();
								ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
								ImGui::PushStyleColor(ImGuiCol_Text, { 0.9f,0.4f,0.2f,1.0f });
								ImGui::TextUnformatted(choice.fullDescription.c_str());
								ImGui::PopStyleColor();
								if (choice.sdefvalue.size())
								{
									ImGui::TextUnformatted(choice.name.c_str());
									ImGui::SameLine();
									ImGui::TextUnformatted(" = ");
									ImGui::SameLine();
									ImGui::TextUnformatted(choice.sdefvalue.c_str());
								}
								ImGui::PopTextWrapPos();
								ImGui::EndTooltip();
							}
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
						InputDataKey* linputData = (InputDataKey*)data->UserData;

						if (!data->Buf || !nullstrlen(linputData->key))
							return 0;


						bool needReloadModel = false;
						Bsp* map2 = g_app->getSelectedMap();
						if (map2)
						{
							BspRenderer* render = map2->getBspRender();
							if (render)
							{
								if (g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] >= 0)
								{
									for (auto selected_entId : g_app->pickInfo.selectedEnts)
									{
										bool needRefreshModel = false;
										Entity* ent = map2->ents[selected_entId];
										std::string newVal = data->Buf;

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


										if (!nullstrlen(newVal))
										{
											ent->setOrAddKeyvalue(linputData->key, linputData->defaultValue);
										}
										else
										{
											ent->setOrAddKeyvalue(linputData->key, newVal);
										}

										render->refreshEnt((int)selected_entId);

										pickCount++;
										vertPickCount++;

										if (needRefreshModel)
										{
											if (ent->getBspModelIdx() > 0)
											{
												map2->getBspRender()->refreshModel(ent->getBspModelIdx());
											}
										}
										map2->getBspRender()->pushEntityUndoStateDelay("Edit Keyvalue", (int)selected_entId, ent);
									}
								}
							}
						}
						pickCount++;
						vertPickCount++;
						if (needReloadModel)
							g_app->reloadBspModels();

						g_app->updateEntConnections();
						return 1;
					}
				};

				if (sel_ent->keyvalues.count(key))
				{
					std::string* keyval = &sel_ent->keyvalues[key];

					if (keyvalue.iType == FGD_KEY_INTEGER)
					{
						ImGui::InputText(("##inval" + std::to_string(i)).c_str(), keyval,
							ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit,
							InputChangeCallback::keyValueChanged, &inputData[i]);
					}
					else
					{
						ImGui::InputText(("##inval" + std::to_string(i)).c_str(), keyval,
							ImGuiInputTextFlags_CallbackEdit, InputChangeCallback::keyValueChanged, &inputData[i]);
					}
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

			for (auto selected_entId : g_app->pickInfo.selectedEnts)
			{
				Entity* selected_ent = map->ents[selected_entId];
				if (spawnflags != 0)
					selected_ent->setOrAddKeyvalue("spawnflags", std::to_string(spawnflags));
				else
					selected_ent->removeKeyvalue("spawnflags");

				map->getBspRender()->pushEntityUndoStateDelay(checkboxEnabled[i] ? "Enable Flag" : "Disable Flag", (int)selected_entId, selected_ent);
			}
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

struct InputData
{
	int idx;
};

struct TextChangeCallback
{
	static int keyNameChanged(ImGuiInputTextCallbackData* data)
	{
		InputData* inputData = (InputData*)data->UserData;

		Bsp* map = g_app->getSelectedMap();
		if (map)
		{
			BspRenderer* render = map->getBspRender();
			if (render)
			{
				if (g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] >= 0)
				{
					std::string key = map->ents[g_app->pickInfo.selectedEnts[0]]->keyOrder[inputData->idx];
					if (key != data->Buf)
					{
						for (auto entId : g_app->pickInfo.selectedEnts)
						{
							Entity* selent = map->ents[entId];
							if (selent->renameKey(key, data->Buf))
							{
								render->refreshEnt((int)entId);
								if (key == "model" || std::string(data->Buf) == "model")
								{
									g_app->reloadBspModels();
								}

								g_app->updateEntConnections();
								map->getBspRender()->pushEntityUndoStateDelay("Rename Keyvalue", (int)entId, selent);
							}
						}
					}
				}
			}
		}
		return 1;
	}

	static int keyValueChanged(ImGuiInputTextCallbackData* data)
	{
		InputData* inputData = (InputData*)data->UserData;

		Bsp* map2 = g_app->getSelectedMap();
		if (map2)
		{
			BspRenderer* render = map2->getBspRender();
			if (render)
			{
				if (g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] >= 0)
				{
					bool needreloadmodels = false;
					std::string key = map2->ents[g_app->pickInfo.selectedEnts[0]]->keyOrder[inputData->idx];
					int part_vec = -1;

					for (auto entId : g_app->pickInfo.selectedEnts)
					{
						Entity* selent = map2->ents[entId];
						if (selent->keyvalues[key] != data->Buf)
						{
							if (part_vec == -1 && g_app->pickInfo.selectedEnts.size() > 1)
							{
								if (key == "origin")
								{
									vec3 newOrigin = parseVector(data->Buf);
									vec3 oldOrigin = parseVector(selent->keyvalues[key]);
									vec3 testOrigin = newOrigin - oldOrigin;
									if (std::fabs(testOrigin.x) > EPSILON2)
									{
										part_vec = 0;
									}
									else if (std::fabs(testOrigin.y) > EPSILON2)
									{
										part_vec = 1;
									}
									else
									{
										part_vec = 2;
									}
								}
							}

							bool needrefreshmodel = false;
							if (key == "model")
							{
								if (selent->hasKey("model") && selent->keyvalues["model"] != data->Buf)
								{
									selent->setOrAddKeyvalue(key, data->Buf);
									render->refreshEnt((int)entId);
									needreloadmodels = true;
								}
							}
							if (key == "renderamt")
							{
								if (selent->hasKey("renderamt") && selent->keyvalues["renderamt"] != data->Buf)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendermode")
							{
								if (selent->hasKey("rendermode") && selent->keyvalues["rendermode"] != data->Buf)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "renderfx")
							{
								if (selent->hasKey("renderfx") && selent->keyvalues["renderfx"] != data->Buf)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "rendercolor")
							{
								if (selent->hasKey("rendercolor") && selent->keyvalues["rendercolor"] != data->Buf)
								{
									needrefreshmodel = true;
								}
							}
							if (key == "origin" && part_vec != -1)
							{
								vec3 newOrigin = parseVector(data->Buf);
								vec3 oldOrigin = parseVector(selent->keyvalues[key]);
								oldOrigin[part_vec] = newOrigin[part_vec];
								selent->setOrAddKeyvalue("origin", oldOrigin.toKeyvalueString());
							}
							else
							{
								selent->setOrAddKeyvalue(key, data->Buf);
							}
							render->refreshEnt((int)entId);
							pickCount++;
							vertPickCount++;
							g_app->updateEntConnections();
							if (needrefreshmodel)
							{
								if (selent->getBspModelIdx() > 0)
								{
									map2->getBspRender()->refreshModel(selent->getBspModelIdx());
									g_app->updateEntConnections();
								}
							}
							map2->getBspRender()->pushEntityUndoStateDelay("Edit Keyvalue RAW", (int)entId, selent);
						}
					}

					if (needreloadmodels)
					{
						pickCount++;
						vertPickCount++;
						g_app->updateEntConnections();
						g_app->reloadBspModels();
					}
				}
			}
		}

		return 1;
	}
};
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
	for (size_t i = 0; i < ent->keyOrder.size() && i < MAX_KEYS_PER_ENT; i++)
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
				if (n_next >= 0 && (size_t)n_next < ent->keyOrder.size() && n_next < MAX_KEYS_PER_ENT)
				{
					dragIds[i] = dragIds[n_next];
					dragIds[n_next] = item;

					std::string temp = ent->keyOrder[i];
					ent->keyOrder[i] = ent->keyOrder[n_next];
					ent->keyOrder[n_next] = std::move(temp);

					ImGui::ResetMouseDragDelta();
				}
			}

			ImGui::NextColumn();
		}

		{
			bool invalidKey = lastPickCount == pickCount;

			keyIds[i].idx = (int)i;

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
			valueIds[i].idx = (int)i;

			if (hoveredDrag[i])
			{
				ImGui::PushStyleColor(ImGuiCol_FrameBg, dragColor);
			}
			ImGui::SetNextItemWidth(inputWidth);
			ImGui::InputText(("##val" + std::to_string(i)).c_str(), &ent->keyvalues[ent->keyOrder[i]], ImGuiInputTextFlags_CallbackEdit,
				TextChangeCallback::keyValueChanged, &valueIds[i]);
			if (ImGui::IsItemHovered() && ent->keyvalues[ent->keyOrder[i]].size())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(ent->keyvalues[ent->keyOrder[i]].c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

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
				app->updateEntConnections();
				map->getBspRender()->pushEntityUndoStateDelay("Delete Keyvalue RAW", entIdx, ent);
			}
			ImGui::PopStyleColor(3);
			ImGui::NextColumn();
		}
	}

	if (!keyDragging && wasKeyDragging)
	{
		map->getBspRender()->refreshEnt(entIdx);
		map->getBspRender()->pushEntityUndoStateDelay("Move Keyvalue", entIdx, ent);
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
			keyName.clear();
			map->getBspRender()->pushEntityUndoStateDelay("Add Keyvalue", entIdx, ent);
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

	int sequenceCount = map->map_mdl->GetSequenceCount();
	static int MDL_Sequence = map->map_mdl->GetSequence();
	static int prev_MDL_Sequence = MDL_Sequence;


	int bodyCount = map->map_mdl->GetBodyCount();
	static int MDL_Body = map->map_mdl->GetBody();
	static int prev_MDL_Body = MDL_Body;


	int skinCount = map->map_mdl->GetSkinCount();
	static int MDL_Skin = map->map_mdl->GetSkin();
	static int prev_MDL_Skin = MDL_Skin;

	if (ImGui::Begin(fmt::format("{}###MDL_WIDGET", get_localized_string("LANG_MDL_WIDGET")).c_str()))
	{
		ImGui::InputInt("Sequence", &MDL_Sequence);
		ImGui::InputInt("Body", &MDL_Body);
		ImGui::InputInt("Skin", &MDL_Skin);

		if (MDL_Sequence < 0) MDL_Sequence = 0;
		if (MDL_Sequence > sequenceCount) MDL_Sequence = sequenceCount;

		if (MDL_Body < 0) MDL_Body = 0;
		if (MDL_Body > bodyCount) MDL_Body = bodyCount;

		if (MDL_Skin < 0) MDL_Skin = 0;
		if (MDL_Skin > skinCount) MDL_Skin = skinCount;

		if (MDL_Sequence != prev_MDL_Sequence)
		{
			map->map_mdl->SetSequence(MDL_Sequence);
			prev_MDL_Sequence = MDL_Sequence;
		}

		if (MDL_Body != prev_MDL_Body)
		{
			map->map_mdl->SetBody(MDL_Body);
			prev_MDL_Body = MDL_Body;
		}

		if (MDL_Skin != prev_MDL_Skin)
		{
			map->map_mdl->SetSkin(MDL_Skin);
			prev_MDL_Skin = MDL_Skin;
		}
	}
	ImGui::End();
}

void Gui::drawGOTOWidget()
{
	ImGui::SetNextWindowSize(ImVec2(410.f, 210.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(410.f, 340.f), ImVec2(410.f, 340.f));
	static vec3 coordinates = vec3();
	static vec3 angles = vec3();
	float angles_y = 0.0f;
	static int modelid = -1, faceid = -1, entid = -1, leafid = -1;
	static bool use_model_offset = false;

	if (ImGui::Begin(fmt::format("{}###GOTO_WIDGET", get_localized_string(LANG_0676)).c_str(), &showGOTOWidget, 0))
	{
		ImGuiStyle& style = ImGui::GetStyle();
		float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
		float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
		if (showGOTOWidget_update)
		{
			entid = g_app->pickInfo.selectedEnts.size() && g_app->pickInfo.selectedEnts[0] > 0 ? (int)g_app->pickInfo.selectedEnts[0] : -1;
			coordinates = cameraOrigin;
			angles = cameraAngles;
			angles = angles.normalize_angles();
			angles.z -= 90.0f;
			angles.y = angles.z;
			angles.z = 0.0f;
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
		ImGui::DragFloat(get_localized_string(LANG_0683).c_str(), &angles.x, 0.1f, 0, 0, "Pitch: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0682).c_str(), &angles.z, 0.1f, 0, 0, "Yaw: %.0f");
		ImGui::SameLine();
		ImGui::DragFloat(get_localized_string(LANG_0684).c_str(), &angles_y, 0.1f, 0, 0, "Roll: %.0f");
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
		if (map && ImGui::Button("Go to"))
		{
			cameraOrigin = coordinates;
			map->getBspRender()->renderCameraOrigin = cameraOrigin;

			cameraAngles = angles.flipUV();
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

			ImGui::SameLine();
			ImGui::Checkbox("Face of model##face_app_mdl", &use_model_offset);
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted("Use model first face as offset.");
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			ImGui::DragInt(get_localized_string(LANG_0687).c_str(), &entid);
			ImGui::DragInt("Leaf", &leafid);
			ImGui::PopItemWidth();
			if (ImGui::Button("Go to##2"))
			{
				if (modelid >= 0 && modelid < map->modelCount && faceid < 0)
				{
					app->pickMode = PICK_OBJECT;
					for (int i = 0; i < (int)map->ents.size(); i++)
					{
						if (map->ents[i]->getBspModelIdx() == modelid)
						{
							app->selectEnt(map, i);
							app->goToEnt(map, i);
							break;
						}
					}
				}
				else if (faceid >= 0 && faceid < map->faceCount)
				{
					app->pickMode = PICK_FACE;
					app->goToFace(map, faceid);
					int modelIdx = use_model_offset && modelid >= 0 ? modelid : map->get_model_from_face(faceid);
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
					if (use_model_offset && modelid >= 0 && modelid < map->modelCount)
					{
						app->selectFace(map, faceid + map->models[modelid].iFirstFace);
					}
					else
					{
						app->selectFace(map, faceid);
					}
				}
				else if (leafid > 0 && leafid < (int)map->leafCount)
				{
					app->pickMode = PICK_FACE;
					BSPLEAF32& leaf = map->leaves[leafid];
					app->goToCoords(getCenter(leaf.nMins, leaf.nMaxs));
				}
				else if (entid > 0 && entid < (int)map->ents.size())
				{
					app->pickMode = PICK_OBJECT;
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
	Entity* ent = NULL;
	int modelIdx = -1;
	auto entIdx = app->pickInfo.selectedEnts;
	Bsp* map = app->getSelectedMap();

	if (map && entIdx.size())
	{
		ent = map->ents[entIdx[0]];
		modelIdx = ent->getBspModelIdx();
	}

	ImGui::SetNextWindowSize(ImVec2(440.f, 450.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(430, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));


	static float new_x, new_y, new_z;
	static float new_scale_x, new_scale_y, new_scale_z;

	static float default_x, default_y, default_z;
	static float default_scale_x, default_scale_y, default_scale_z;



	static int lastPickCount = -1;
	static int lastVertPickCount = -1;
	static bool oldSnappingEnabled = app->gridSnappingEnabled;

	if (ImGui::Begin(fmt::format("{}###TRANSFORM_WIDGET", get_localized_string(LANG_0688)).c_str(), &showTransformWidget, 0))
	{
		if (!ent || modelIdx == 0)
		{
			ImGui::Text(get_localized_string(LANG_1180).c_str());
		}
		else
		{
			ImGuiStyle& style = ImGui::GetStyle();

			TransformAxes& activeAxes = *(app->transformMode == TRANSFORM_MODE_SCALE ? &app->scaleAxes : &app->moveAxes);

			int currentTransformMode = app->transformMode;
			int currentTransformTarget = app->transformTarget;

			if (updateTransformWidget)
			{
				if (app->transformTarget == TRANSFORM_VERTEX)
				{
					new_x = activeAxes.origin.x;
					new_y = activeAxes.origin.y;
					new_z = activeAxes.origin.z;
				}
				else if (app->transformTarget == TRANSFORM_ORIGIN)
				{
					if (modelIdx > 0 && modelIdx < map->modelCount)
					{
						new_x = map->models[modelIdx].vOrigin.x;
						new_y = map->models[modelIdx].vOrigin.y;
						new_z = map->models[modelIdx].vOrigin.z;
					}
				}
				else
				{
					new_x = ent->origin.x;
					new_y = ent->origin.y;
					new_z = ent->origin.z;
				}

				if (app->transformTarget == TRANSFORM_VERTEX)
				{
					new_scale_x = new_scale_y = new_scale_z = 1.0f;
				}
				else
				{
					if (modelIdx <= 0)
					{
						new_scale_x = new_scale_y = new_scale_z = 0.0f;
					}
					else
					{
						new_scale_x = app->selectionSize.x;
						new_scale_y = app->selectionSize.y;
						new_scale_z = app->selectionSize.z;
					}
				}

				default_scale_x = new_scale_x;
				default_scale_y = new_scale_y;
				default_scale_z = new_scale_z;

				default_x = new_x;
				default_y = new_y;
				default_z = new_z;

				updateTransformWidget = false;
			}

			oldSnappingEnabled = app->gridSnappingEnabled;
			lastVertPickCount = vertPickCount;
			lastPickCount = pickCount;

			guiHoverAxis = -1;

			float padding = style.WindowPadding.x * 2 + style.FramePadding.x * 2;
			float inputWidth = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.33f;
			float inputWidth4 = (ImGui::GetWindowWidth() - (padding + style.ScrollbarSize)) * 0.25f;

			float dragPow = app->gridSnappingEnabled ? app->snapSize : 0.02f;

			static double LastTransformUpdateTime = 0.0;

			ImGui::Text(get_localized_string(LANG_0689).c_str());
			ImGui::PushItemWidth(inputWidth);

			ImGui::DragFloat(get_localized_string(LANG_1107).c_str(), &new_x, dragPow, -FLT_MAX_COORD, FLT_MAX_COORD, "Y: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;
			ImGui::SameLine();

			ImGui::DragFloat(get_localized_string(LANG_1108).c_str(), &new_y, dragPow, -FLT_MAX_COORD, FLT_MAX_COORD, "X: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;
			ImGui::SameLine();

			ImGui::DragFloat(get_localized_string(LANG_1109).c_str(), &new_z, dragPow, -FLT_MAX_COORD, FLT_MAX_COORD, "Z: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;

			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y));

			ImGui::Text(get_localized_string(LANG_0690).c_str());
			ImGui::PushItemWidth(inputWidth);

			if (!app->isTransformableSolid || app->modelUsesSharedStructures || app->transformMode != TRANSFORM_MODE_SCALE)
			{
				ImGui::BeginDisabled();
			}

			ImGui::DragFloat(get_localized_string(LANG_0691).c_str(), &new_scale_x, dragPow, 0, 0, "Y: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 0;

			ImGui::SameLine();

			ImGui::DragFloat(get_localized_string(LANG_0692).c_str(), &new_scale_y, dragPow, 0, 0, "X: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 1;

			ImGui::SameLine();

			ImGui::DragFloat(get_localized_string(LANG_0693).c_str(), &new_scale_z, dragPow, 0, 0, "Z: %.2f");

			if (ImGui::IsItemHovered() || ImGui::IsItemActive())
				guiHoverAxis = 2;

			if (!app->isTransformableSolid || app->modelUsesSharedStructures || app->transformMode != TRANSFORM_MODE_SCALE)
			{
				ImGui::EndDisabled();
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
			if (modelIdx < 0)
			{
				ImGui::BeginDisabled();
				if (app->transformTarget == TRANSFORM_ORIGIN
					|| app->transformTarget == TRANSFORM_VERTEX)
				{
					app->transformTarget = TRANSFORM_OBJECT;
				}
			}
			if (!app->isTransformableSolid || app->modelUsesSharedStructures)
			{
				if (app->transformTarget == TRANSFORM_VERTEX)
				{
					app->transformTarget = TRANSFORM_OBJECT;
				}
				ImGui::BeginDisabled();
			}
			if (ImGui::RadioButton(get_localized_string(LANG_0696).c_str(), &app->transformTarget, TRANSFORM_VERTEX))
			{
				pickCount++;
				vertPickCount++;
			}
			if (!app->isTransformableSolid || app->modelUsesSharedStructures)
			{
				ImGui::EndDisabled();
			}

			if (app->transformMode == TRANSFORM_MODE_SCALE)
			{
				ImGui::BeginDisabled();
				if (app->transformTarget == TRANSFORM_ORIGIN)
				{
					app->transformTarget = TRANSFORM_OBJECT;
				}
			}

			ImGui::NextColumn();

			if (ImGui::RadioButton(get_localized_string(LANG_0697).c_str(), &app->transformTarget, TRANSFORM_ORIGIN))
			{
				pickCount++;
				vertPickCount++;
			}
			if (modelIdx < 0)
			{
				ImGui::EndDisabled();
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
			ImGui::RadioButton(get_localized_string(LANG_1110).c_str(), &app->transformMode, TRANSFORM_MODE_NONE);
			ImGui::NextColumn();
			ImGui::RadioButton(get_localized_string(LANG_1111).c_str(), &app->transformMode, TRANSFORM_MODE_MOVE);
			ImGui::NextColumn();
			if (modelIdx < 0 || !app->isTransformableSolid || app->modelUsesSharedStructures)
			{
				if (app->transformMode == TRANSFORM_MODE_SCALE)
					app->transformMode = TRANSFORM_MODE_MOVE;
				ImGui::BeginDisabled();
			}
			ImGui::RadioButton(get_localized_string(LANG_1112).c_str(), &app->transformMode, TRANSFORM_MODE_SCALE);
			ImGui::NextColumn();
			if (modelIdx < 0 || !app->isTransformableSolid || app->modelUsesSharedStructures)
			{
				ImGui::EndDisabled();
			}
			ImGui::Columns(1);

			const char* element_names[] = { "0", "0.01", "0.1", "0.5", "1", "2", "4", "8", "16", "32", "64" };
			const int grid_snap_modes = sizeof(element_names) / sizeof(element_names[0]);
			const float element_values[grid_snap_modes] = { 0.00001f, 0.01f, 0.1f,0.5f,1.f,2.f,4.f,8.f,16.f,32.f,64.f };

			static int current_element = app->gridSnapLevel;

			ImGui::Columns(2, 0, false);
			ImGui::SetColumnWidth(0, inputWidth4);
			ImGui::SetColumnWidth(1, inputWidth4 * 3);
			ImGui::Text(get_localized_string(LANG_0699).c_str()); ImGui::NextColumn();
			ImGui::SetNextItemWidth(inputWidth4 * 3);

			if (ImGui::SliderInt(get_localized_string(LANG_0700).c_str(), &current_element, 0, grid_snap_modes - 1, element_names[current_element]))
			{
				app->gridSnapLevel = current_element - 1;
				app->gridSnappingEnabled = current_element != 0;
				app->snapSize = element_values[current_element];
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
			if (app->transformMode != TRANSFORM_MODE_MOVE || app->transformTarget != TRANSFORM_OBJECT || app->modelUsesSharedStructures)
				ImGui::BeginDisabled();
			ImGui::Checkbox(get_localized_string(LANG_0703).c_str(), &app->moveOrigin);
			if (app->transformMode != TRANSFORM_MODE_MOVE || app->transformTarget != TRANSFORM_OBJECT || app->modelUsesSharedStructures)
				ImGui::EndDisabled();

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_::ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(get_localized_string(LANG_0705).c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			ImGui::PopItemWidth();

			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
			ImGui::Separator();/*
			ImGui::Dummy(ImVec2(0, style.FramePadding.y * 2));
			ImGui::Text(("Size: " + app->selectionSize.toKeyvalueString(false, "w ", "l ", "h")).c_str());
			ImGui::Separator();*/

			ImGui::Text(fmt::format("Entity origin: {:.2f} {:.2f} {:.2f}", ent->origin.x, ent->origin.y, ent->origin.z).c_str());

			if (modelIdx >= 0 && map)
			{
				ImGui::Text(fmt::format("Model origin: {:.2f} {:.2f} {:.2f}", map->models[modelIdx].vOrigin.x, map->models[modelIdx].vOrigin.y, map->models[modelIdx].vOrigin.z).c_str());
				vec3 modelCenter = getCenter(map->models[modelIdx].nMins, map->models[modelIdx].nMaxs);
				ImGui::Text(fmt::format("Model center: {:.2f} {:.2f} {:.2f}", modelCenter.x, modelCenter.y, modelCenter.z).c_str());
				ImGui::Text(fmt::format("Model bounds: \n{:.2f} {:.2f} {:.2f} / {:.2f} {:.2f} {:.2f}", map->models[modelIdx].nMins.x, map->models[modelIdx].nMins.y, map->models[modelIdx].nMins.z, map->models[modelIdx].nMaxs.x, map->models[modelIdx].nMaxs.y, map->models[modelIdx].nMaxs.z).c_str());
			}

			if (currentTransformMode != app->transformMode || currentTransformTarget != app->transformTarget)
			{
				pickCount++;
				vertPickCount++;
			}

			bool needUpdate = new_scale_x != default_scale_x ||
				new_scale_y != default_scale_y ||
				new_scale_z != default_scale_z ||
				new_x != default_x || new_y != default_y || new_z != default_z;

			if (needUpdate && app->curTime - LastTransformUpdateTime < 1.0)
			{
				needUpdate = false;
			}

			if (needUpdate)
			{
				updateTransformWidget = true;
				LastTransformUpdateTime = app->curTime;
				if (app->transformTarget == TRANSFORM_VERTEX)
				{
					vec3 org1 = vec3(default_x, default_y, default_z);
					vec3 org2 = vec3(new_x, new_y, new_z);
					vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
					vec3 delta2 = org2 - org1;
					if (!delta.IsZero() && !delta2.IsZero())
					{
						app->moveSelectedVerts(delta);

						updateTransformWidget = true;
						vertPickCount++;
					}
				}
				else if (app->transformTarget == TRANSFORM_OBJECT)
				{
					vec3 org1 = vec3(default_x, default_y, default_z);
					vec3 org2 = vec3(new_x, new_y, new_z);
					vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
					vec3 delta2 = org2 - org1;
					if (!delta.IsZero() && !delta2.IsZero())
					{
						ent->setOrAddKeyvalue("origin", org2.toKeyvalueString());
						map->getBspRender()->refreshEnt((int)entIdx[0]);
						app->updateEntConnectionPositions();

						updateTransformWidget = true;
						pickCount++;
					}
				}
				else if (app->transformTarget == TRANSFORM_ORIGIN)
				{
					vec3 org1 = vec3(default_x, default_y, default_z);
					vec3 org2 = vec3(new_x, new_y, new_z);
					vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
					vec3 delta2 = org2 - org1;
					if (!delta.IsZero() && !delta2.IsZero())
					{
						if (modelIdx > 0 && modelIdx < map->modelCount)
						{
							map->models[modelIdx].vOrigin = org2;
						}

						updateTransformWidget = true;
						pickCount++;
					}
				}
				if (app->isTransformableSolid && !app->modelUsesSharedStructures && modelIdx > 0)
				{
					if (app->transformTarget == TRANSFORM_VERTEX)
					{
						vec3 org1 = vec3(1.0f, 1.0f, 1.0f);
						vec3 org2 = vec3(new_scale_x, new_scale_y, new_scale_z);
						vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
						vec3 delta2 = org2 - org1;
						if (!delta.IsZero() && !delta2.IsZero())
						{
							app->scaleSelectedVerts(map, modelIdx, new_scale_x, new_scale_y, new_scale_z);

							updateTransformWidget = true;
							vertPickCount++;
						}
					}
					else
					{
						vec3 org1 = vec3(default_scale_x, default_scale_y, default_scale_z);
						vec3 org2 = vec3(new_scale_x, new_scale_y, new_scale_z);
						vec3 delta = app->gridSnappingEnabled ? app->snapToGrid(org2 - org1) : org2 - org1;
						vec3 delta2 = org2 - org1;
						if (!delta.IsZero() && !delta2.IsZero())
						{
							app->scaleSelectedObject(map, modelIdx, delta.x, delta.y, delta.z);
							map->getBspRender()->refreshModel(modelIdx);
							map->getBspRender()->refreshModelClipnodes(modelIdx);
							app->applyTransform(map, true);

							updateTransformWidget = true;
							vertPickCount++;
							pickCount++;
						}
					}

					app->updateSelectionSize(map, modelIdx);
				}
			}
		}
	}
	ImGui::End();
}

void Gui::loadFonts()
{
	const std::string fontPath = "./fonts/";
	const std::string mainFont = "calibri.ttf";
	std::vector<std::string> fontFiles;
	ImFontConfig config;
	config.SizePixels = fontSize * 2.0f;
	config.OversampleH = 3;
	config.OversampleV = 1;
	config.RasterizerMultiply = 1.5f;
	config.PixelSnapH = true;

	if (!fs::exists(fontPath) || !fs::is_directory(fontPath)) {
		print_log(PRINT_RED, "Font directory does not exist or is not accessible.\n");
		return;
	}

	for (const auto& entry : fs::directory_iterator(fontPath)) {
		if (entry.is_regular_file()) {
			auto extension = entry.path().extension().string();
			extension = toLowerCase(extension);
			if (extension == ".ttf" || extension == ".ttc") {
				fontFiles.emplace_back(entry.path().string());
			}
		}
	}

	std::sort(fontFiles.begin(), fontFiles.end(), [&](const std::string& a, const std::string& b) {
		bool isA = a.find(mainFont) != std::string::npos;
		bool isB = b.find(mainFont) != std::string::npos;
		if (isA && !isB) return true;
		if (!isA && isB) return false;
		return a < b;
		});

	ImVector<ImWchar> glyphRanges;
	ImFontGlyphRangesBuilder builder;
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesDefault());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesCyrillic());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesChineseFull());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesChineseSimplifiedCommon());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesGreek());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesKorean());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesJapanese());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesThai());
	builder.AddRanges(imgui_io->Fonts->GetGlyphRangesVietnamese());
	builder.BuildRanges(&glyphRanges);

	config.GlyphRanges = glyphRanges.Data;

	ImFont* tmpFont = nullptr;

	for (const auto& fontFile : fontFiles)
	{
		try
		{
			auto font = imgui_io->Fonts->AddFontFromFileTTF(fontFile.c_str(), fontSize, &config, glyphRanges.Data);
			if (!font)
			{
				print_log(PRINT_RED, "Invalid {} font.\n", fontFile);
			}
			else
			{
				tmpFont = font;
			}
			if (tmpFont)
				config.MergeMode = true;
		}
		catch (...)
		{
			print_log(PRINT_RED, "Invalid {} font.\n", fontFile);
		}
	}

	imgui_io->Fonts->Build();

	defaultFont = tmpFont;
	smallFont = tmpFont;
	consoleFont = tmpFont;
	largeFont = tmpFont;
	consoleFontLarge = tmpFont;
}

void Gui::drawLog()
{
	static bool AutoScroll = true;
	static bool scroll_to_bottom = false;

	ImGui::SetNextWindowSize(ImVec2(750.f, 300.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(200.f, 100.f), ImVec2(FLT_MAX, app->windowHeight - 40.f));
	if (!ImGui::Begin(fmt::format("{}###LOG_WIDGET", get_localized_string(LANG_1164)).c_str(), &showLogWidget))
	{
		ImGui::End();
		return;
	}

	static std::vector<std::string> log_buffer_copy;
	static std::vector<unsigned int> color_buffer_copy;

	g_mutex_list[0].lock();
	bool logUpdated = log_buffer_copy.size() != g_log_buffer.size();
	if (logUpdated)
	{
		log_buffer_copy = g_log_buffer;
		color_buffer_copy = g_color_buffer;
		scroll_to_bottom = true;
	}
	g_mutex_list[0].unlock();

	ImGui::BeginChild(get_localized_string(LANG_0706).c_str(), ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

	bool copy = false;
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

		}
		ImGui::EndPopup();
	}

	ImGui::PushFont(consoleFont);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

	if (copy)
	{
		std::string logStr;
		for (const auto& str : log_buffer_copy)
		{
			logStr += str + "\n";
		}
		ImGui::SetClipboardText(logStr.c_str());
	}

	ImGuiListClipper clipper;
	clipper.Begin((int)log_buffer_copy.size());
	while (clipper.Step())
	{
		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, imguiColorFromConsole(color_buffer_copy[i]));
			ImGui::TextUnformatted(log_buffer_copy[i].c_str());
			ImGui::PopStyleColor();
		}
	}
	clipper.End();

	if (AutoScroll && scroll_to_bottom)
	{
		ImGui::SetScrollHereY(1.0f);
		scroll_to_bottom = false;
	}

	ImGui::PopFont();
	ImGui::PopStyleVar();

	ImGui::EndChild();
	ImGui::End();
}

void Gui::drawSettings()
{
	ImGui::SetNextWindowSize(ImVec2(790.f, 340.f), ImGuiCond_FirstUseEver);

	bool oldShowSettings = showSettingsWidget;
	bool apply_settings_pressed = false;
	static std::string langForSelect = g_settings.selected_lang;
	static std::string palForSelect = g_settings.palette_name;

	if (ImGui::Begin(fmt::format("{}###SETTING_WIDGET", get_localized_string(LANG_1114)).c_str(), &showSettingsWidget))
	{
		ImGuiContext& g = *GImGui;
		const int settings_tabs = 7;

		static int resSelected = 0;
		static int fgdSelected = 0;


		std::string tab_titles[settings_tabs] = {
			get_localized_string("LANG_SETTINGS_GENERAL"),
			get_localized_string("LANG_SETTINGS_FGDPATH"),
			get_localized_string("LANG_SETTINGS_WADPATH"),
			get_localized_string("LANG_SETTINGS_OPTIMIZE"),
			get_localized_string("LANG_SETTINGS_LIMITS"),
			get_localized_string("LANG_SETTINGS_RENDER"),
			get_localized_string("LANG_SETTINGS_CONTROL")
		};

		// left
		ImGui::BeginChild(get_localized_string(LANG_0709).c_str(), ImVec2(150, 0), true);

		for (int i = 0; i < settings_tabs; i++)
		{
			if (ImGui::Selectable(tab_titles[i].c_str(), settingsTab == i))
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
		ImGui::Text(tab_titles[settingsTab].c_str());
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
				g_settings.gamedir = stripFileName(res.string());
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("WorkingDir"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.workingdir = stripFileName(res.string());
				g_settings.lastdir = stripFileName(res.string());
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
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		if (ifd::FileDialog::Instance().IsDone("radPath"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				g_settings.rad_path = res.string();
				g_settings.lastdir = stripFileName(res.string());
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
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		ImGui::BeginChild(get_localized_string(LANG_0712).c_str());
		if (settingsTab == 0)
		{
			ImGui::Text(get_localized_string(LANG_0713).c_str());
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText(get_localized_string(LANG_0714).c_str(), &g_settings.gamedir);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.gamedir.size())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(g_settings.gamedir.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(delWidth);
			if (ImGui::Button(get_localized_string(LANG_0715).c_str()))
			{
				ifd::FileDialog::Instance().Open("GameDir", "Select game dir", std::string(), false, g_settings.lastdir);
			}
			ImGui::Text(get_localized_string(LANG_0716).c_str());
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText(get_localized_string(LANG_0717).c_str(), &g_settings.workingdir);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.workingdir.size())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(g_settings.workingdir.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}
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

			ImGui::Checkbox(get_localized_string(LANG_0724).c_str(), &g_settings.savebackup);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0725).c_str());
				ImGui::EndTooltip();
			}

			ImGui::Checkbox(get_localized_string(LANG_0726).c_str(), &g_settings.save_crc);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0727).c_str());
				ImGui::EndTooltip();
			}

			ImGui::SameLine();

			ImGui::Checkbox(get_localized_string(LANG_0728).c_str(), &g_settings.auto_import_ent);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(get_localized_string(LANG_0729).c_str());
				ImGui::EndTooltip();
			}

			ImGui::Checkbox(get_localized_string(LANG_0730).c_str(), &g_settings.same_dir_for_ent);
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

			ImGui::Checkbox(get_localized_string(LANG_0734).c_str(), &g_settings.default_is_empty);
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


			ImGui::Checkbox("Save map cam pos", &g_settings.save_cam);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Save camera position to map and load it at open.");
				ImGui::EndTooltip();
			}


			ImGui::Separator();
			ImGui::TextUnformatted("Language:");
			ImGui::SameLine();
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
			ImGui::TextUnformatted("Palette:");
			ImGui::SameLine();
			if (ImGui::BeginCombo("##pal", palForSelect.c_str()))
			{
				for (const auto& s : g_settings.palettes)
				{
					if (ImGui::Selectable(s.name.c_str(), s.name == palForSelect))
					{
						palForSelect = s.name;
					}
				}
				ImGui::EndCombo();
			}
			ImGui::Separator();


			ImGui::TextUnformatted("RAD Executable:");
			ImGui::SetNextItemWidth(pathWidth * 0.80f);
			ImGui::InputText("##hl_rad", &g_settings.rad_path);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.rad_path.size())
			{
				ImGui::BeginTooltip();
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
				ImGui::TextUnformatted(g_settings.rad_path.c_str());
				ImGui::PopTextWrapPos();
				ImGui::EndTooltip();
			}

			ImGui::SetNextItemWidth(delWidth);
			if (ImGui::Button("...##hlrad_path"))
			{
				ifd::FileDialog::Instance().Open("radPath", "Select rad executable path", "*.*", false, g_settings.lastdir);
			}

			ImGui::Text("RAD options:");
			ImGui::SetNextItemWidth(pathWidth);
			ImGui::InputText("##hlrad_options", &g_settings.rad_options);

			ImGui::Separator();

			if (ImGui::Button(get_localized_string(LANG_0739).c_str()))
			{
				g_settings.loadDefaultSettings();
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
			for (size_t i = 0; i < g_settings.fgdPaths.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth * 0.20f);
				ImGui::Checkbox((std::string("##enablefgd") + std::to_string(i)).c_str(), &g_settings.fgdPaths[i].enabled);

				ImGui::SameLine();

				ImGui::SetNextItemWidth(pathWidth * 0.80f);
				ImGui::InputText(("##fgd" + std::to_string(i)).c_str(), &g_settings.fgdPaths[i].path);
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.fgdPaths[i].path.size())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted(g_settings.fgdPaths[i].path.c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				if (ImGui::Button(("...##fgdOpen" + std::to_string(i)).c_str()))
				{
					fgdSelected = (int)i;
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
				g_settings.fgdPaths.emplace_back(std::string(), true);
			}
		}
		else if (settingsTab == 2)
		{
			for (size_t i = 0; i < g_settings.resPaths.size(); i++)
			{
				ImGui::SetNextItemWidth(pathWidth * 0.20f);
				ImGui::Checkbox((std::string("##enableres") + std::to_string(i)).c_str(), &g_settings.resPaths[i].enabled);

				ImGui::SameLine();

				ImGui::SetNextItemWidth(pathWidth * 0.80f);
				ImGui::InputText(("##res" + std::to_string(i)).c_str(), &g_settings.resPaths[i].path);
				if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay && g_settings.resPaths[i].path.size())
				{
					ImGui::BeginTooltip();
					ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
					ImGui::TextUnformatted(g_settings.resPaths[i].path.c_str());
					ImGui::PopTextWrapPos();
					ImGui::EndTooltip();
				}
				ImGui::SameLine();

				ImGui::SetNextItemWidth(delWidth);
				if (ImGui::Button(("...##resOpen" + std::to_string(i)).c_str()))
				{
					resSelected = (int)i;
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
				g_settings.resPaths.emplace_back(std::string(), true);
			}
		}
		else if (settingsTab == 3)
		{
			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox(get_localized_string(LANG_0743).c_str(), &g_settings.strip_wad_path);
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

			ImGui::SetNextItemWidth(pathWidth / 2);
			ImGui::Checkbox("Merge edges [WIP]", &g_settings.merge_edges);
			if (ImGui::IsItemHovered() && g.HoveredIdTimer > g_tooltip_delay) {
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Warning! This option can add visual glitches to map.");
				ImGui::EndTooltip();
			}

			ImGui::SetNextItemWidth(pathWidth);
			ImGui::Text(get_localized_string(LANG_0749).c_str());

			for (size_t i = 0; i < g_settings.conditionalPointEntTriggers.size(); i++)
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

			for (size_t i = 0; i < g_settings.entsThatNeverNeedAnyHulls.size(); i++)
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

			for (size_t i = 0; i < g_settings.entsThatNeverNeedCollision.size(); i++)
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

			for (size_t i = 0; i < g_settings.passableEnts.size(); i++)
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

			for (size_t i = 0; i < g_settings.playerOnlyTriggers.size(); i++)
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

			for (size_t i = 0; i < g_settings.monsterOnlyTriggers.size(); i++)
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
			ImGui::DragInt("MAX SURFACE EXTENTS", (int*)&MAX_SURFACE_EXTENT, 1, 4, 1024, "%i");
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
			if (!g_settings.vsync)
			{
				ImGui::SameLine();
				if (ImGui::DragInt("FPS LIMIT", &g_settings.fpslimit, 5, 30, 1000, "%u"))
				{
					if (g_settings.fpslimit > 2000)
						g_settings.fpslimit = 2000;
				}
				if (g_settings.fpslimit < 15)
					g_settings.fpslimit = 15;
			}
			ImGui::DragFloat(get_localized_string(LANG_0776).c_str(), &app->fov, 0.1f, 1.0f, 150.0f, get_localized_string(LANG_0777).c_str());
			ImGui::DragFloat(get_localized_string(LANG_0778).c_str(), &app->zFar, 10.0f, -FLT_MAX_COORD, FLT_MAX_COORD, "%.0f", ImGuiSliderFlags_Logarithmic);
			ImGui::Separator();

			bool renderTextures = g_render_flags & RENDER_TEXTURES;
			bool renderTexturesFilter = !(g_render_flags & RENDER_TEXTURES_NOFILTER);
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
			bool renderSelectedAtTop = g_render_flags & RENDER_SELECTED_AT_TOP;
			bool renderMapBoundary = g_render_flags & RENDER_MAP_BOUNDARY;

			ImGui::Text(get_localized_string(LANG_0779).c_str());

			ImGui::Columns(2, 0, false);

			if (ImGui::Checkbox(get_localized_string(LANG_0780).c_str(), &renderTextures))
			{
				g_render_flags ^= RENDER_TEXTURES;
			}
			if (ImGui::Checkbox("Texture Filter", &renderTexturesFilter))
			{
				g_render_flags ^= RENDER_TEXTURES_NOFILTER;
				for (auto& tex : g_all_Textures)
				{
					bool filternoneed = g_render_flags & RENDER_TEXTURES_NOFILTER;
					if (tex->type >= 0 && tex->type != tex->TYPE_LIGHTMAP)
					{
						tex->farFilter = tex->nearFilter = !filternoneed ? GL_LINEAR : GL_NEAREST;
						tex->upload(tex->type);
					}
				}
			}
			if (ImGui::Checkbox(get_localized_string(LANG_0781).c_str(), &renderLightmaps))
			{
				g_render_flags ^= RENDER_LIGHTMAPS;
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

			if (ImGui::Checkbox("Selected at top", &renderSelectedAtTop))
			{
				g_render_flags ^= RENDER_SELECTED_AT_TOP;
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
				for (size_t i = 0; i < mapRenderers.size(); i++)
				{
					mapRenderers[i]->updateClipnodeOpacity(transparentNodes ? 128 : 255);
				}
			}
			if (ImGui::Checkbox("Map boundary", &renderMapBoundary))
			{
				g_render_flags ^= RENDER_MAP_BOUNDARY;
			}

			ImGui::Columns(1);

			ImGui::Separator();
			ImGui::Text(get_localized_string(LANG_0793).c_str());

			for (size_t i = 0; i < g_settings.transparentTextures.size(); i++)
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

			for (size_t i = 0; i < g_settings.transparentEntities.size(); i++)
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

			for (size_t i = 0; i < g_settings.entsNegativePitchPrefix.size(); i++)
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
		g_settings.palette_name = palForSelect;
		set_localize_lang(g_settings.selected_lang);
		g_settings.saveSettings();
		if (!app->reloading)
		{
			app->reloading = true;
			app->loadFgds();
			app->postLoadFgds();
			for (size_t i = 0; i < mapRenderers.size(); i++)
			{
				BspRenderer* mapRender = mapRenderers[i];
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
	if (ImGui::Begin(fmt::format("{}###HELP_WIDGET", get_localized_string(LANG_1117)).c_str(), &showHelpWidget))
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
				ImGui::BulletText("Press CTRL+ALT+A to select all faces same texture.");
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
	ImGui::SetNextWindowSize(ImVec2(650.f, 160.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(fmt::format("{}###ABOUT_WIDGET", get_localized_string(LANG_1119)).c_str(), &showAboutWidget))
	{
		ImGui::InputText(get_localized_string(LANG_0822).c_str(), &g_version_string, ImGuiInputTextFlags_ReadOnly);

		static char author[] = "w00tguy(bspguy), karaulov(newbspguy)";
		ImGui::InputText(get_localized_string(LANG_0823).c_str(), author, strlen(author), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(author);
			ImGui::EndTooltip();
		}

		static char url[] = "https://github.com/wootguy/bspguy";
		ImGui::InputText(get_localized_string(LANG_0824).c_str(), url, strlen(url), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(url);
			ImGui::EndTooltip();
		}

		static char url2[] = "https://github.com/UnrealKaraulov/newbspguy";
		ImGui::InputText((get_localized_string(LANG_0824) + "##2").c_str(), url2, strlen(url2), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(url2);
			ImGui::EndTooltip();
		}

		static char help1[] = "https://t.me/ninjac0w\nhttps://github.com/Qwertyus3D\nhttps://hlfx.ru/forum/member.php?action=getinfo&userid=3\ntwhl community\netc";
		ImGui::InputTextMultiline("Special thanks to:", help1, strlen(help1), ImVec2(0, 45), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(help1);
			ImGui::EndTooltip();
		}

		static char bad1[] = "Empty";
		ImGui::InputText("Very bad objects:", bad1, strlen(bad1), ImGuiInputTextFlags_ReadOnly);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(bad1);
			ImGui::EndTooltip();
		}
	}

	ImGui::End();
}

void Gui::drawMergeWindow()
{
	ImGui::SetNextWindowSize(ImVec2(600.f, 250.f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(600.f, 250.f), ImVec2(600.f, 500.f));
	static std::string outPath = "outbsp.bsp";
	static std::vector<std::string> inPaths;
	static bool DeleteUnusedInfo = true;
	static bool Optimize = false;
	static bool DeleteHull2 = false;
	static bool NoRipent = false;
	static bool NoStyles = false;
	static bool NoScript = false;

	bool addNew = false;

	static int select_path = 0;

	if (inPaths.size() < 1)
	{
		inPaths.emplace_back("");
	}

	if (ImGui::Begin(fmt::format("{}###MERGE_WIDGET", get_localized_string(LANG_0825)).c_str(), &showMergeMapWidget))
	{
		if (ifd::FileDialog::Instance().IsDone("BspMergeDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				inPaths[select_path] = res.string();
				g_settings.lastdir = stripFileName(res.string());
			}
			ifd::FileDialog::Instance().Close();
		}

		for (size_t i = 0; i < inPaths.size(); i++)
		{
			std::string& s = inPaths[i];
			ImGui::SetNextItemWidth(280);
			ImGui::InputText(fmt::format(fmt::runtime("##inpath{}"), i).c_str(), &s);
			ImGui::SameLine();
			if (ImGui::Button((get_localized_string(LANG_0834) + "##" + std::to_string(i)).c_str()))
			{
				select_path = (int)i;
				ifd::FileDialog::Instance().Open("BspMergeDialog", "Opep bsp model", "BSP file (*.bsp){.bsp},.*", false, g_settings.lastdir);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(fmt::format(fmt::runtime(get_localized_string(LANG_0826)), i).c_str());

			if (s.length() > 1 && i + 1 == inPaths.size())
			{
				addNew = true;
			}
		}

		ImGui::SetNextItemWidth(280);
		ImGui::InputText(get_localized_string(LANG_0828).c_str(), &outPath);

		ImGui::Checkbox(get_localized_string(LANG_0829).c_str(), &DeleteUnusedInfo);
		ImGui::Checkbox(get_localized_string(LANG_1121).c_str(), &Optimize);
		ImGui::Checkbox(get_localized_string(LANG_0830).c_str(), &DeleteHull2);
		ImGui::Checkbox(get_localized_string(LANG_0831).c_str(), &NoRipent);
		ImGui::Checkbox(get_localized_string(LANG_0832).c_str(), &NoScript);
		ImGui::Checkbox("Skip lightstyles merging", &NoStyles);

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
				for (size_t i = 0; i < maps.size(); i++)
				{
					print_log(get_localized_string(LANG_1057), maps[i]->bsp_name);
					if (DeleteUnusedInfo)
					{
						print_log(get_localized_string(LANG_1058));
						STRUCTCOUNT removed = maps[i]->remove_unused_model_structures();
						g_progress.clear();
						g_progress = ProgressMeter();
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
				MergeResult result = merger.merge(maps, vec3(), outPath, NoRipent, NoScript, false, NoStyles);

				print_log("\n");
				if (result.map && result.map->isValid())
				{
					result.map->write(outPath);
					print_log("\n");
					result.map->print_info(false, 0, 0);

					app->clearMaps();

					fixupPath(outPath, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP, FIXUPPATH_SLASH::FIXUPPATH_SLASH_SKIP);

					if (fileExists(outPath))
					{
						app->addMap(new Bsp(outPath));
					}
					else
					{
						print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0398));
						app->addMap(new Bsp());
					}
				}

				for (auto& map : maps)
					delete map;
				maps.clear();
			}
			showMergeMapWidget = false;
		}
	}

	ImGui::End();

	if (addNew)
	{
		inPaths.emplace_back(std::string(""));
	}
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

	if (ImGui::Begin(fmt::format("{}###IMPORT_WIDGET", title).c_str(), &showImportMapWidget))
	{
		if (ifd::FileDialog::Instance().IsDone("BspOpenDialog"))
		{
			if (ifd::FileDialog::Instance().HasResult())
			{
				std::filesystem::path res = ifd::FileDialog::Instance().GetResult();
				mapPath = res.string();
				g_settings.lastdir = stripFileName(res.string());
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
					Bsp* map = app->getSelectedMap();
					if (map)
					{
						int import_model = ImportModel(map, mapPath);
						Entity* newEnt = new Entity("func_wall");
						newEnt->addKeyvalue("model", "*" + std::to_string(import_model));
						map->ents.push_back(newEnt);
						map->update_ent_lump();
						if (map->ents.size() > 0)
						{
							map->getBspRender()->refreshEnt((int)(map->ents.size()) - 1);
						}
					}
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
							app->deselectObject();
							map->ents.push_back(new Entity("func_breakable"));
							map->ents[map->ents.size() - 1]->setOrAddKeyvalue("gibmodel", std::string("models/") + basename(mapPath));
							map->ents[map->ents.size() - 1]->setOrAddKeyvalue("model", std::string("models/") + basename(mapPath));
							map->ents[map->ents.size() - 1]->setOrAddKeyvalue("spawnflags", "1");
							map->update_ent_lump();
							map->update_lump_pointers();
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

	static Bsp* oldMap = NULL;

	if (map != oldMap)
	{
		reloadLimits();
		oldMap = map;
	}


	if (ImGui::Begin(fmt::format("{}###LIMITS_WIDGET", title).c_str(), &showLimitsWidget))
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
						stats.emplace_back(calcStat("GL_TEXTURES", (unsigned int)g_all_Textures.size(), 0, false));
						stats.emplace_back(calcStat("models", map->modelCount, MAX_MAP_MODELS, false));
						stats.emplace_back(calcStat("planes", map->planeCount, MAX_MAP_PLANES, false));
						stats.emplace_back(calcStat("vertexes", map->vertCount, MAX_MAP_VERTS, false));
						stats.emplace_back(calcStat("nodes", map->nodeCount, MAX_MAP_NODES, false));
						stats.emplace_back(calcStat("texinfos", map->texinfoCount, MAX_MAP_TEXINFOS, false));
						stats.emplace_back(calcStat("faces", map->faceCount, MAX_MAP_FACES, false));
						stats.emplace_back(calcStat("clipnodes", map->clipnodeCount, map->is_32bit_clipnodes ? INT_MAX : MAX_MAP_CLIPNODES, false));
						stats.emplace_back(calcStat("leaves", map->leafCount, MAX_MAP_LEAVES, false));
						stats.emplace_back(calcStat("marksurfaces", map->marksurfCount, MAX_MAP_MARKSURFS, false));
						stats.emplace_back(calcStat("surfedges", map->surfedgeCount, MAX_MAP_SURFEDGES, false));
						stats.emplace_back(calcStat("edges", map->edgeCount, MAX_MAP_EDGES, false));
						stats.emplace_back(calcStat("textures", map->textureCount, MAX_MAP_TEXTURES, false));
						stats.emplace_back(calcStat("texturedata", map->textureDataLength, INT_MAX, true));
						stats.emplace_back(calcStat("lightdata", map->lightDataLength, MAX_MAP_LIGHTDATA, true));
						stats.emplace_back(calcStat("visdata", map->visDataLength, MAX_MAP_VISDATA, true));
						stats.emplace_back(calcStat("entities", (unsigned int)map->ents.size(), MAX_MAP_ENTS, false));
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

					for (size_t i = 0; i < stats.size(); i++)
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
		for (size_t i = 0; i < modelInfos.size(); i++)
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

	for (size_t i = 0; i < limitModels[sortMode].size(); i++)
	{
		if (modelInfos[i].val == "0")
		{
			break;
		}

		std::string cname = modelInfos[i].classname + "##" + "select" + std::to_string(i);
		int flags = ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns;
		if (ImGui::Selectable(cname.c_str(), app->pickInfo.IsSelectedEnt(modelInfos[i].entIdx), flags))
		{
			int entIdx = modelInfos[i].entIdx;
			if ((size_t)entIdx < map->ents.size())
			{
				app->pickInfo.SetSelectedEnt(entIdx);
				// map should already be valid if limits are showing

				if (ImGui::IsMouseDoubleClicked(0))
				{
					app->goToEnt(map, (int)entIdx);
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

	if (ImGui::Begin(fmt::format("{}###ENTITY_WIDGET", title).c_str(), &showEntityReport))
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

			float footerHeight = ImGui::GetFrameHeightWithSpacing() * 5.f + 16.f;
			ImGui::BeginChild(get_localized_string(LANG_0848).c_str(), ImVec2(0.f, -footerHeight));

			if (filterNeeded)
			{
				visibleEnts.clear();
				while ((int)keyFilter.size() < MAX_FILTERS)
					keyFilter.emplace_back(std::string());
				while ((int)valueFilter.size() < MAX_FILTERS)
					valueFilter.emplace_back(std::string());

				for (size_t i = 1; i < map->ents.size(); i++)
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
							for (size_t c = 0; c < ent->keyOrder.size(); c++)
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
							for (size_t c = 0; c < ent->keyOrder.size(); c++)
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
						visibleEnts.push_back((int)i);
					}
				}

				selectedItems.clear();
				selectedItems.resize(visibleEnts.size());
				for (size_t k = 0; k < selectedItems.size(); k++)
				{
					if (selectAllItems)
					{
						selectedItems[k] = true;
						if (!app->pickInfo.IsSelectedEnt(visibleEnts[k]))
						{
							app->selectEnt(map, (int)visibleEnts[k], true);
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
				for (int line = clipper.DisplayStart; line < clipper.DisplayEnd && line < (int)visibleEnts.size() && visibleEnts[line] < (int)map->ents.size(); line++)
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
					bool isShiftPressed = g_app->pressed[GLFW_KEY_LEFT_SHIFT] || g_app->pressed[GLFW_KEY_RIGHT_SHIFT];
					bool isCtrlPressed = g_app->pressed[GLFW_KEY_LEFT_CONTROL] || g_app->pressed[GLFW_KEY_RIGHT_CONTROL];

					if (isSelectableSelected || isForceOpen)
					{
						if (isForceOpen)
						{
							needhover = false;
							ImGui::OpenPopup("ent_context");
						}
						if (isCtrlPressed)
						{
							selectedItems[i] = !selectedItems[i];
							lastSelect = i;
							app->pickInfo.selectedEnts.clear();
							for (size_t k = 0; k < selectedItems.size(); k++)
							{
								if (selectedItems[k])
								{
									app->selectEnt(map, (int)visibleEnts[k], true);
								}
							}
						}
						else if (isShiftPressed)
						{
							if (lastSelect >= 0)
							{
								int begin = i > lastSelect ? lastSelect : i;
								int end = i > lastSelect ? i : lastSelect;
								for (size_t k = 0; k < selectedItems.size(); k++)
									selectedItems[k] = false;
								for (int k = begin; k < end; k++)
									selectedItems[k] = true;
								selectedItems[lastSelect] = true;
								selectedItems[i] = true;
							}


							app->pickInfo.selectedEnts.clear();
							for (size_t k = 0; k < selectedItems.size(); k++)
							{
								if (selectedItems[k])
								{
									app->selectEnt(map, (int)visibleEnts[k], true);
								}
							}
						}
						else
						{
							if (!selectedItems[i] || !isForceOpen)
							{
								for (size_t k = 0; k < selectedItems.size(); k++)
									selectedItems[k] = false;
								if (i < 0)
									i = 0;
								app->pickInfo.selectedEnts.clear();
								app->selectEnt(map, (int)visibleEnts[i], true);
							}
							selectedItems[i] = true;
							lastSelect = i;
							if (ImGui::IsMouseDoubleClicked(0) || app->pressed[GLFW_KEY_SPACE])
							{
								app->goToEnt(map, (int)visibleEnts[i]);
							}
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
			if (!map->is_mdl_model)
			{
				drawBspContexMenu();
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

					for (size_t i = 1; i < map->ents.size(); i++)
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

				for (size_t k = 0; k < usedClasses.size(); k++)
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
						for (size_t i = 0; i < app->fgd->existsFlagNames.size(); i++)
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

			while ((int)keyFilter.size() < MAX_FILTERS)
				keyFilter.emplace_back(std::string());
			while ((int)valueFilter.size() < MAX_FILTERS)
				valueFilter.emplace_back(std::string());

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

			if (app->pickInfo.selectedEnts.size() != 1)
			{
				ImGui::BeginDisabled();
			}

			if (ImGui::Button(get_localized_string(LANG_0858).c_str()))
			{
				app->goToEnt(map, app->pickInfo.selectedEnts[0]);
			}

			if (app->pickInfo.selectedEnts.size() != 1)
			{
				ImGui::EndDisabled();
			}

			ImGui::SameLine();

			if (ImGui::Button(get_localized_string(LANG_0859).c_str()) && app->pickInfo.selectedEnts.size())
			{
				startFrom = (app->pickInfo.selectedEnts[0] - 8) * clipper.ItemsHeight;
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

			while (offset2 >= (int)colordata.size())
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

	for (int lightId = 0; lightId < MAX_LIGHTMAPS; lightId++)
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
				if (map->faces[faceIdx].nLightmapOffset < 0 || map->faces[faceIdx].nStyles[lightId] == 255)
					continue;

				int size[2];
				map->GetFaceLightmapSize((int)faceIdx, size);

				int sizeX = size[0], sizeY = size[1];

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


	for (int lightId = 0; lightId < MAX_LIGHTMAPS; lightId++)
	{
		colordata = std::vector<COLOR3>();
		int current_x = 0;
		int current_y = 0;
		int max_y_found = 0;

		bool found_any_lightmap = false;

		//print_log(get_localized_string(LANG_0411),lightId);
		for (int faceIdx : faces_to_export)
		{
			if (map->faces[faceIdx].nLightmapOffset < 0 || map->faces[faceIdx].nStyles[lightId] == 255)
				continue;

			int size[2];
			map->GetFaceLightmapSize((int)faceIdx, size);

			int sizeX = size[0], sizeY = size[1];


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

void ExportLightmap(const BSPFACE32& face, int faceIdx, Bsp* map)
{
	int size[2];
	map->GetFaceLightmapSize(faceIdx, size);
	std::string filename;

	for (int i = 0; i < MAX_LIGHTMAPS; i++)
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

void ImportLightmap(const BSPFACE32& face, int faceIdx, Bsp* map)
{
	std::string filename;
	int size[2];
	map->GetFaceLightmapSize(faceIdx, size);
	for (int i = 0; i < MAX_LIGHTMAPS; i++)
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
	static Texture* currentlightMap[MAX_LIGHTMAPS] = { NULL };
	static float windowWidth = 500.0f;
	static float windowHeight = 600.0f;
	static int lightmap_count = 0;
	static bool needPickColor = false;
	const char* light_names[] =
	{
		"ALL",
		"Main light",
		"Light 1",
		"Light 2",
		"Light 3"
	};

	static int light_offsets[] =
	{
		0,
		0,
		0,
		0,
		0
	};

	ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, windowHeight), ImVec2(windowWidth, -1.0f));

	if (ImGui::Begin(fmt::format("{}###LIGHTMAP_WIDGET", get_localized_string(LANG_0599)).c_str(), &showLightmapEditorWidget))
	{
		if (needPickColor)
		{
			ImGui::TextDisabled(get_localized_string(LANG_0863).c_str());
		}
		Bsp* map = app->getSelectedMap();
		if (map)
		{
			BspRenderer* renderer = map->getBspRender();
			int faceIdx = app->pickInfo.selectedFaces.size() ? (int)app->pickInfo.selectedFaces[0] : -1;
			BSPFACE32* face = NULL;
			int size[2]{};
			if (faceIdx >= 0)
			{
				face = &map->faces[faceIdx];
				map->GetFaceLightmapSize(faceIdx, size);
			}
			else
			{
				lightmap_count = 0;
			}
			if (showLightmapEditorUpdate && face)
			{
				lightmap_count = 0;

				for (int i = 0; i < MAX_LIGHTMAPS; i++)
				{
					if (currentlightMap[i])
						delete currentlightMap[i];
					currentlightMap[i] = NULL;
				}

				for (int i = 0; i < MAX_LIGHTMAPS; i++)
				{
					if (face->nStyles[i] == 255)
						continue;
					int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
					currentlightMap[i] = new Texture(size[0], size[1], new unsigned char[lightmapSz], "LIGHTMAP");
					int offset = face->nLightmapOffset + i * lightmapSz;
					light_offsets[i] = offset;
					if (!map->lightdata || offset + lightmapSz > map->lightDataLength)
						memset(currentlightMap[i]->get_data(), 255, lightmapSz);
					else
						memcpy(currentlightMap[i]->get_data(), map->lightdata + offset, lightmapSz);
					currentlightMap[i]->upload(Texture::TEXTURE_TYPE::TYPE_LIGHTMAP);
					lightmap_count++;
					//print_log(get_localized_string(LANG_0418),i,offset);
				}

				windowWidth = lightmap_count > 1 ? 500.f : 290.f;
				showLightmapEditorUpdate = false;
			}
			ImVec2 imgSize = ImVec2(200, 200);
			for (int i = 0; i < lightmap_count; i++)
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
					ImGui::Separator();
					ImGui::TextDisabled(fmt::format("Offest:{}", light_offsets[i]).c_str());
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
					ImGui::Separator();
					ImGui::TextDisabled(fmt::format("Offest:{}", light_offsets[i]).c_str());
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
					if (offset >= currentlightMap[i]->width * currentlightMap[i]->height * (int)sizeof(COLOR3))
						offset = (currentlightMap[i]->width * currentlightMap[i]->height * (int)sizeof(COLOR3)) - 1;
					if (offset < 0)
						offset = 0;

					COLOR3* lighdata = (COLOR3*)currentlightMap[i]->get_data();

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
						currentlightMap[i]->upload(Texture::TEXTURE_TYPE::TYPE_LIGHTMAP);
					}
				}
			}
			if (face)
			{
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
			}
			ImGui::SetNextItemWidth(100.f);
			ImGui::Checkbox(light_names[1], &renderer->lightEnableFlags[0]);
			ImGui::SameLine();
			ImGui::Checkbox(light_names[2], &renderer->lightEnableFlags[1]);
			ImGui::Checkbox(light_names[3], &renderer->lightEnableFlags[2]);
			ImGui::SameLine();
			ImGui::Dummy({ 22,0 });
			ImGui::SameLine();
			ImGui::Checkbox(light_names[4], &renderer->lightEnableFlags[3]);
			ImGui::Separator();

			if (face)
			{
				if (ImGui::Button(get_localized_string(LANG_1126).c_str(), ImVec2(120, 0)))
				{
					for (int i = 0; i < MAX_LIGHTMAPS; i++)
					{
						if (face->nStyles[i] == 255 || !currentlightMap[i])
							continue;
						int lightmapSz = size[0] * size[1] * sizeof(COLOR3);
						int offset = face->nLightmapOffset + i * lightmapSz;
						memcpy(map->lightdata + offset, currentlightMap[i]->get_data(), lightmapSz);
					}
					map->resize_all_lightmaps(true);
					renderer->pushModelUndoState(get_localized_string(LANG_0599), FL_LIGHTING);
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
					ExportLightmap(*face, faceIdx, map);
				}
				ImGui::SameLine();
				if (ImGui::Button(get_localized_string(LANG_1129).c_str(), ImVec2(120, 0)))
				{
					print_log(get_localized_string(LANG_0421));
					ImportLightmap(*face, faceIdx, map);
					showLightmapEditorUpdate = true;
					renderer->reloadLightmaps();
				}
				ImGui::Separator();
			}

			//ImGui::Text(get_localized_string(LANG_0866).c_str());
			//ImGui::Separator();
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
				renderer->reloadLightmaps();
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
	static float scroll_x = 0.0f;
	static float scroll_y = 0.0f;

	ImGui::SetNextWindowScroll(ImVec2(scroll_x, scroll_y));

	ImGui::SetNextWindowSize(ImVec2(300.f, 570.f), ImGuiCond_FirstUseEver);
	//ImGui::SetNextWindowSize(ImVec2(400, 600));
	bool beginFaceEditor = ImGui::Begin(fmt::format("{} {}###FACE_EDITOR_WIDGET", get_localized_string(LANG_0870),
		app->pickInfo.selectedFaces.size() != 1 ? std::string() : std::to_string(app->pickInfo.selectedFaces[0])).c_str(), &showFaceEditWidget);

	if (beginFaceEditor && app->pickMode != PICK_FACE_LEAF)
	{
		static float scaleX, scaleY, shiftX, shiftY;
		static std::vector<std::array<int, 2>> lightmapSizes{};
		static float rotateX, rotateY;
		static bool lockRotate = true;
		static int bestplane;
		static bool isSpecial;
		static int width, height;
		static std::vector<vec3> edgeVerts;
		static ImTextureID textureId = NULL; // OpenGL ID
		static char textureName[MAXTEXTURENAME];
		static char textureName2[MAXTEXTURENAME];
		static int lastPickCount = -1;
		static int miptex = 0;
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

		static int tmpStyles[MAX_LIGHTMAPS] = { 255,255,255,255 };
		static bool stylesChanged = false;

		Bsp* map = app->getSelectedMap();
		if (!map || app->pickMode == PICK_OBJECT || app->pickInfo.selectedFaces.empty())
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

		if (lastPickCount != pickCount && app->pickMode != PICK_OBJECT)
		{
			edgeVerts.clear();
			if (app->pickInfo.selectedFaces.size())
			{
				int faceIdx = (int)app->pickInfo.selectedFaces[0];
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
							width = tex.nWidth;
							height = tex.nHeight;
							memcpy(textureName, tex.szName, MAXTEXTURENAME);
							memcpy(textureName2, tex.szName, MAXTEXTURENAME);
						}
						else
						{
							textureName[0] = '\0';
							textureName2[0] = '\0';
						}
					}
					else
					{
						textureName[0] = '\0';
						textureName2[0] = '\0';
					}

					miptex = texinfo.iMiptex;

					vec3 xv, yv;
					bestplane = TextureAxisFromPlane(plane, xv, yv);

					rotateX = AngleFromTextureAxis(texinfo.vS, true, bestplane);
					rotateY = AngleFromTextureAxis(texinfo.vT, false, bestplane);

					scaleX = 1.0f / texinfo.vS.length();
					scaleY = 1.0f / texinfo.vT.length();

					shiftX = texinfo.shiftS;
					shiftY = texinfo.shiftT;

					isSpecial = texinfo.nFlags & TEX_SPECIAL;


					textureId = (ImTextureID)(size_t)mapRenderer->getFaceTextureId(faceIdx);
					validTexture = true;

					for (int i = 0; i < MAX_LIGHTMAPS; i++)
					{
						tmpStyles[i] = face.nStyles[i];
					}

					lightmapSizes.clear();

					int lmSize[2];
					map->GetFaceLightmapSize(faceIdx, lmSize);
					lightmapSizes.push_back({ lmSize[0],lmSize[1] });


					// show default values if not all faces share the same values
					for (size_t i = 1; i < app->pickInfo.selectedFaces.size(); i++)
					{
						int faceIdx2 = app->pickInfo.selectedFaces[i];
						map->GetFaceLightmapSize((int)faceIdx2, lmSize);
						lightmapSizes.push_back({ lmSize[0],lmSize[1] });
						BSPFACE32& face2 = map->faces[faceIdx2];
						BSPTEXTUREINFO& texinfo2 = map->texinfos[face2.iTextureInfo];

						if (scaleX != 1.0f / texinfo2.vS.length()) scaleX = 1.0f;
						if (scaleY != 1.0f / texinfo2.vT.length()) scaleY = 1.0f;

						if (shiftX != texinfo2.shiftS) shiftX = 0;
						if (shiftY != texinfo2.shiftT) shiftY = 0;

						if (isSpecial == !(texinfo2.nFlags & TEX_SPECIAL)) isSpecial = false;

						if (texinfo2.iMiptex != miptex)
						{
							validTexture = false;
							textureId = NULL;
							width = 0;
							height = 0;
							textureName[0] = '\0';
						}
					}
					for (int e = face.iFirstEdge; e < face.iFirstEdge + face.nEdges; e++)
					{
						int edgeIdx = map->surfedges[e];
						BSPEDGE32 edge = map->edges[abs(edgeIdx)];
						vec3 v = edgeIdx > 0 ? map->verts[edge.iVertex[0]] : map->verts[edge.iVertex[1]];
						edgeVerts.push_back(v);
					}
				}
			}
			else
			{
				scaleX = scaleY = shiftX = shiftY = 0.0f;
				width = height = 0;
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
			ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0422)), lightmapSizes[0][0], lightmapSizes[0][1], lightmapSizes[0][0] * lightmapSizes[0][1]).c_str());


		ImGui::Text(get_localized_string(LANG_1169).c_str());
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

						vec3& vec = edgeIdx > 0 ? map->verts[edge.iVertex[0]] : map->verts[edge.iVertex[1]];

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

		ImGui::InputText(get_localized_string(LANG_0892).c_str(), textureName2, MAXTEXTURENAME);
		ImGui::SameLine();
		ImGui::Text(fmt::format("#{}", miptex).c_str());

		ImGui::SameLine();

		if (ImGui::Button("APPLY"))
		{
			if (strcasecmp(textureName, textureName2) != 0)
			{
				textureChanged = true;
				memcpy(textureName, textureName2, MAXTEXTURENAME);
			}
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted("Apply texture or create one new.");
			ImGui::EndTooltip();
		}

		if (!validTexture)
		{
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::Text(fmt::format(fmt::runtime(get_localized_string(LANG_0893)), width, height).c_str());


		ImVec2 imgSize = ImVec2(inputWidth * 2 - 2, inputWidth * 2 - 2);
		if (ImGui::ImageButton("##show_texbrowser", textureId, imgSize, ImVec2(0, 0), ImVec2(1, 1)))
		{
			showTextureBrowser = true;
		}

		ImGui::PushItemWidth(inputWidth);


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


			if (ImGui::Button("COPY VERTS"))
			{
				std::string outstr = "";
				for (auto& s : edgeVerts)
				{
					outstr += s.toKeyvalueString() + "\n";
				}
				ImGui::SetClipboardText(outstr.c_str());
			}
		}

		ImGui::PopItemWidth();

		if (!ImGui::IsMouseDown(ImGuiMouseButton_::ImGuiMouseButton_Left) &&
			(pasteTextureNow || updatedFaceVec || scaledX || scaledY || shiftedX || shiftedY || textureChanged || stylesChanged || toggledFlags || updatedTexVec || mergeFaceVec))
		{
			if (pasteTextureNow)
			{
				textureChanged = true;
				pasteTextureNow = false;
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
							mapRenderer->reuploadTextures();
							mapRenderer->preRenderFaces();

							delete[] imageData;
							delete wadTex;
						}
					}
				}
				if (!validTexture)
				{
					validTexture = true;
					COLOR3 rndColor;
					rndColor.r = 50 + rand() % 206;
					rndColor.g = 50 + rand() % 206;
					rndColor.b = 50 + rand() % 206;
					std::vector<COLOR3> img(width * height, rndColor);
					newMiptex = map->add_texture(textureName, (unsigned char*)img.data(), width, height);
					mapRenderer->reuploadTextures();
					mapRenderer->preRenderFaces();
				}
			}

			std::set<int> modelRefreshes;
			for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
			{
				int faceIdx = app->pickInfo.selectedFaces[i];

				BSPFACE32& face = map->faces[faceIdx];
				BSPTEXTUREINFO* texinfo = map->get_unique_texinfo((int)faceIdx);

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
					for (int n = 0; n < MAX_LIGHTMAPS; n++)
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
					if (!isSpecial)
						texinfo->nFlags &= ~TEX_SPECIAL;
					else
						texinfo->nFlags |= TEX_SPECIAL;
				}

				if ((textureChanged || toggledFlags || updatedFaceVec || stylesChanged) && validTexture)
				{
					int modelIdx = map->get_model_from_face((int)faceIdx);
					if (textureChanged)
						texinfo->iMiptex = newMiptex;
					if (modelIdx >= 0 && !modelRefreshes.count(modelIdx))
						modelRefreshes.insert(modelIdx);
				}

				mapRenderer->updateFaceUVs((int)faceIdx);


				if ((updatedFaceVec || scaledX || scaledY || shiftedX || shiftedY || stylesChanged
					|| pasteTextureNow || updatedTexVec || mergeFaceVec))
				{
					for (size_t n = 0; n < app->pickInfo.selectedFaces.size(); n++)
					{
						int lmSize[2];
						map->GetFaceLightmapSize((int)app->pickInfo.selectedFaces[n], lmSize);
						if (lmSize[0] != lightmapSizes[n][0] ||
							lmSize[1] != lightmapSizes[n][1])
						{
							print_log(PRINT_GREEN | PRINT_RED | PRINT_INTENSITY, "Warning need resize lightmap face {} from {}x{} to {}x{}\n",
								app->pickInfo.selectedFaces[n], lightmapSizes[n][0], lightmapSizes[n][1], lmSize[0], lmSize[1]);
						}
					}
				}
			}

			if (updatedFaceVec && app->pickInfo.selectedFaces.size() == 1)
			{
				int faceIdx = (int)app->pickInfo.selectedFaces[0];
				int vecId = 0;
				for (int e = map->faces[faceIdx].iFirstEdge; e < map->faces[faceIdx].iFirstEdge + map->faces[faceIdx].nEdges; e++, vecId++)
				{
					int edgeIdx = map->surfedges[e];
					BSPEDGE32 edge = map->edges[abs(edgeIdx)];
					vec3& v = edgeIdx > 0 ? map->verts[edge.iVertex[0]] : map->verts[edge.iVertex[1]];
					v = edgeVerts[vecId];
				}
			}

			if ((textureChanged || toggledFlags || updatedFaceVec || stylesChanged) && app->pickInfo.selectedFaces.size())
			{
				textureId = (ImTextureID)(size_t)mapRenderer->getFaceTextureId((int)app->pickInfo.selectedFaces[0]);

				memcpy(textureName2, textureName, MAXTEXTURENAME);

				for (auto it = modelRefreshes.begin(); it != modelRefreshes.end(); it++)
				{
					mapRenderer->refreshModel(*it);
				}
				for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
				{
					mapRenderer->highlightFace((int)app->pickInfo.selectedFaces[i], 1);
				}
			}

			if (mergeFaceVec)
			{
				map->remove_unused_model_structures(CLEAN_VERTICES);

				app->reloading = true;
				map->getBspRender()->reload();
				app->reloading = false;
			}

			checkFaceErrors();

			if (updatedFaceVec)
			{
				targetLumps = FL_PLANES | FL_TEXTURES | FL_VERTICES | FL_NODES | FL_TEXINFO | FL_FACES | FL_LIGHTING | FL_CLIPNODES | FL_LEAVES | FL_EDGES | FL_SURFEDGES | FL_MODELS;
			}

			map->resize_all_lightmaps(true);
			mapRenderer->loadLightmaps();

			reloadLimits();

			if (updatedTexVec)
			{
				pickCount++;
				vertPickCount++;
			}

			mergeFaceVec = updatedFaceVec = scaledX = scaledY = shiftedX = shiftedY =
				textureChanged = toggledFlags = updatedTexVec = stylesChanged = false;

			map->getBspRender()->pushModelUndoState(targetEditName, targetLumps);
		}

		pasteTextureNow = false;
	}
	else
	{
		Bsp* map = app->getSelectedMap();
		if (!map || app->pickMode == PICK_OBJECT)
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


		static int last_leaf = -1;
		static bool new_last_leaf = false;
		static int last_leaf_mdl = 0;
		static std::vector<int> vis_leafs;
		static std::vector<int> invis_leafs;
		static bool leaf_decompress = false;
		static unsigned char* visData = NULL;
		static bool vis_debugger_press = false;
		static std::vector<int> face_leaf_list;
		static std::vector<int> leaf_faces;
		static bool auto_update_leaf = true;
		static std::vector<int> last_faces;

		int rowSize = (((map->leafCount - 1) + 63) & ~63) >> 3;
		if (leaf_decompress && last_leaf != -1 && last_leaf < map->leafCount)
		{
			if (visData)
			{
				delete[] visData;
				visData = NULL;
			}
			visData = new unsigned char[rowSize];
			memset(visData, 0, rowSize);
			if (map->leaves[last_leaf].nVisOffset >= 0)
				DecompressVis(map->visdata + map->leaves[last_leaf].nVisOffset, visData, rowSize, map->leafCount - 1, map->visDataLength - map->leaves[last_leaf].nVisOffset);
			vis_leafs.clear();
			invis_leafs.clear();
			std::vector<int> visLeafs;
			map->modelLeafs(0, visLeafs);

			for (auto l : visLeafs)
			{
				if (l == 0)
					continue;
				if (CHECKVISBIT(visData, l - 1))
				{
					vis_leafs.push_back(l);
				}
				else
				{
					invis_leafs.push_back(l);
				}
			}
		}
		leaf_decompress = false;

		if ((last_leaf != mapRenderer->curLeafIdx && auto_update_leaf) || new_last_leaf)
		{
			if (auto_update_leaf)
			{
				if (last_leaf != mapRenderer->curLeafIdx)
				{
					last_leaf = mapRenderer->curLeafIdx;
					leaf_decompress = true;
				}
			}
			if (new_last_leaf)
			{
				leaf_decompress = true;
			}

			if (last_leaf < 0 && last_leaf >= map->leafCount)
			{
				leaf_decompress = false;
			}
			else
			{
				BSPLEAF32& tmpLeaf = map->leaves[last_leaf];

				mapRenderer->leafCube->mins = tmpLeaf.nMins;
				mapRenderer->leafCube->maxs = tmpLeaf.nMaxs;

				g_app->pointEntRenderer->genCubeBuffers(mapRenderer->leafCube);
				std::vector<int> leafNodes;
				map->get_leaf_nodes(last_leaf, leafNodes);

				if (leafNodes.size())
				{
					BSPNODE32 node = map->nodes[leafNodes[0]];

					mapRenderer->nodeCube->mins = node.nMins;
					mapRenderer->nodeCube->maxs = node.nMaxs;

					g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodeCube);

					//BSPPLANE plane = map->planes[node.iPlane];

					//float d = dotProduct(plane.vNormal, cameraOrigin) - plane.fDist;

					//mapRenderer->nodePlaneCube->mins = node.nMins;
					//mapRenderer->nodePlaneCube->maxs = node.nMaxs;

					//mapRenderer->nodePlaneCube->mins += plane.vNormal * d;
					//mapRenderer->nodePlaneCube->maxs += plane.vNormal * d;

					//g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodePlaneCube);
				}

				leaf_faces = map->getLeafFaces(last_leaf);
				last_leaf_mdl = map->get_model_from_leaf(last_leaf);
			}
			new_last_leaf = false;
		}

		if (leaf_decompress)
		{
			ImGui::TextUnformatted("Decompressing...");
		}
		else
		{
			ImGuiStyle& style = ImGui::GetStyle();

			if (!app->pickInfo.selectedFaces.empty())
			{
				if (last_faces != app->pickInfo.selectedFaces)
				{
					if (vis_debugger_press)
					{
						vis_debugger_press = false;
						mapRenderer->preRenderFaces();
					}
					face_leaf_list = map->getFaceLeafs((int)app->pickInfo.selectedFaces[0]);
				}

				last_faces = app->pickInfo.selectedFaces;
				ImVec4 errorColor = { 1.0, 0.0, 0.0, 1.0 };
				ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
				ImGui::TextUnformatted("Faces");

				if (ImGui::Button("DELETE"))
				{
					std::sort(app->pickInfo.selectedFaces.begin(), app->pickInfo.selectedFaces.end());

					while (app->pickInfo.selectedFaces.size())
					{
						map->remove_face((int)app->pickInfo.selectedFaces[app->pickInfo.selectedFaces.size() - 1]);
						app->pickInfo.selectedFaces.pop_back();
					}

					mapRenderer->loadLightmaps();
					mapRenderer->preRenderFaces();

					map->update_ent_lump();
					map->update_lump_pointers();

					mapRenderer->pushModelUndoState("DELETE FACES", EDIT_MODEL_LUMPS);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces now totally removed from map!");
					ImGui::EndTooltip();
				}
				ImGui::SameLine();

				if (ImGui::Button("REMOVE PVS"))
				{
					auto selected_faces = app->pickInfo.selectedFaces;
					std::sort(selected_faces.begin(), selected_faces.end());

					while (selected_faces.size())
					{
						map->leaf_del_face((int)selected_faces[selected_faces.size() - 1], -1);
						selected_faces.pop_back();
					}

					mapRenderer->preRenderFaces();

					map->update_ent_lump();
					map->update_lump_pointers();

					mapRenderer->pushModelUndoState("REMOVE FACES FROM PVS", EDIT_MODEL_LUMPS);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces will be removed from leaves and make this faces invisible!");
					ImGui::EndTooltip();
				}

				if (ImGui::Button("MAKE VISIBLE ANYWHERE"))
				{
					auto selected_faces = app->pickInfo.selectedFaces;
					std::sort(selected_faces.begin(), selected_faces.end());

					while (selected_faces.size())
					{
						map->leaf_add_face((int)selected_faces[selected_faces.size() - 1], -1);
						selected_faces.pop_back();
					}
					mapRenderer->preRenderFaces();

					map->update_lump_pointers();

					mapRenderer->pushModelUndoState("MAKE FACES VISIBLE IN ALL LEAFS", FL_LEAVES | FL_MARKSURFACES);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces will be added to all leaves and make those faces visible from anything!");
					ImGui::EndTooltip();
				}

				if (ImGui::Button(fmt::format("DEL FROM {} LEAF", last_leaf).c_str()))
				{
					auto selected_faces = app->pickInfo.selectedFaces;
					std::sort(selected_faces.begin(), selected_faces.end());
					while (selected_faces.size())
					{
						map->leaf_del_face((int)selected_faces[selected_faces.size() - 1], last_leaf);
						selected_faces.pop_back();
					}

					mapRenderer->preRenderFaces();

					map->update_ent_lump();
					map->update_lump_pointers();

					mapRenderer->pushModelUndoState("MAKE FACES INVISIBLE FOR CURRENT LEAF", FL_LEAVES | FL_MARKSURFACES);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces will be removed from current leaf for invisibility!");
					ImGui::EndTooltip();
				}

				if (ImGui::Button(fmt::format("ADD TO {} LEAF", last_leaf).c_str()))
				{
					auto selected_faces = app->pickInfo.selectedFaces;
					std::sort(selected_faces.begin(), selected_faces.end());
					while (selected_faces.size())
					{
						map->leaf_add_face((int)selected_faces[selected_faces.size() - 1], last_leaf);
						selected_faces.pop_back();
					}

					mapRenderer->preRenderFaces();

					map->update_ent_lump();
					map->update_lump_pointers();

					mapRenderer->pushModelUndoState("MAKE FACES VISIBLE FOR CURRENT LEAF", FL_LEAVES | FL_MARKSURFACES);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Selected faces will be added to current leaf for visibility!");
					ImGui::EndTooltip();
				}

				ImGui::PopStyleColor();

				ImGui::TextUnformatted("Used in leaves:");
				style.FrameBorderSize = 1.0f;

				ImGui::BeginChild("##faceleaflist", ImVec2(0, 120), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);

				ImGuiListClipper leaf_clipper;
				leaf_clipper.Begin((int)face_leaf_list.size());

				while (leaf_clipper.Step())
				{
					for (int line_no = leaf_clipper.DisplayStart; line_no < leaf_clipper.DisplayEnd; line_no++)
					{
						if (ImGui::Selectable(std::to_string(face_leaf_list[line_no]).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
						{
							if (ImGui::IsMouseDoubleClicked(0))
							{

							}
						}
					}
				}

				ImGui::EndChild();
			}

			bool updatedLeafVec = false;

			vec3 mins = vec3();
			vec3 maxs = vec3();

			int vertIdx = 0;

			if (last_faces.size() == 1)
			{
				BSPFACE32 face = map->faces[last_faces[0]];

				BSPPLANE& tmpPlane = map->planes[face.iPlane];
				ImGui::PushItemWidth(105);
				ImGui::TextUnformatted(fmt::format("Plane [{}] side [{}] type [{}]", face.iPlane, face.nPlaneSide, tmpPlane.nType).c_str());

				maxs = tmpPlane.vNormal;
				float dist = tmpPlane.fDist;

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &maxs.x, 0.0f, 0, 0, "X:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &maxs.y, 0.0f, 0, 0, "Y:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &maxs.z, 0.0f, 0, 0, "Z:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &dist, 0.0f, 0, 0, "DIST:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (updatedLeafVec)
				{
					tmpPlane.vNormal = maxs;
					tmpPlane.fDist = dist;

					/*mapRenderer->nodePlaneCube->mins = { -32,-32,-32 };
					mapRenderer->nodePlaneCube->maxs = { 32, 32, 32 };

					mapRenderer->nodePlaneCube->mins += tmpPlane.vNormal;
					mapRenderer->nodePlaneCube->maxs += tmpPlane.vNormal;

					g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodePlaneCube);*/
					updatedLeafVec = false;
					mapRenderer->pushModelUndoState("UPDATE MODEL PLANE MINS/MAXS", FL_NODES);
				}
				ImGui::PopItemWidth();
			}


			ImGui::Separator();

			ImVec4 errorColor = { 1.0, 0.0, 0.0, 1.0 };
			ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
			ImGui::TextUnformatted("Leaves");
			ImGui::PopStyleColor(1);

			if (ImGui::Checkbox("Auto update", &auto_update_leaf) && auto_update_leaf)
			{
				if (last_leaf >= 0)
				{
					leaf_decompress = true;
				}
			}

			if (!auto_update_leaf)
			{
				ImGui::TextUnformatted("Enter leaf number:");
				int tmp_new_leaf = last_leaf;
				if (ImGui::InputInt("##inputleaf", &tmp_new_leaf, 1, 1))
				{
					if (tmp_new_leaf != last_leaf && last_leaf >= 0 && last_leaf < map->leafCount)
					{
						last_leaf = tmp_new_leaf;
						new_last_leaf = true;
					}
				}
				if (ImGui::Button("GO TO##LEAF"))
				{
					BSPLEAF32& leaf = map->leaves[last_leaf];
					app->goToCoords(getCenter(leaf.nMins, leaf.nMaxs));
				}
			}

			//if (!auto_update_leaf)
			//{
			//	if (ImGui::Button("Update leaf"))
			//	{
			//		if (last_leaf >= 0)
			//		{
			//			leaf_decompress = true;
			//		}
			//	}
			//}

			ImGui::TextUnformatted(fmt::format("Leaf list. Leaf:{}", last_leaf).c_str());
			ImGui::TextUnformatted(fmt::format("Leaf model id:{}", last_leaf_mdl).c_str());

			float flContents = 0.0f;

			if (last_leaf >= 0 && last_leaf < map->leafCount)
			{
				flContents = map->leaves[last_leaf].nContents * 1.0f;

				ImGui::TextUnformatted(fmt::format("Vis offset:{}", map->leaves[last_leaf].nVisOffset).c_str());
				ImGui::TextUnformatted("Contents:");
				ImGui::SameLine();
				ImGui::PushItemWidth(30);
				if (ImGui::DragFloat("##leaf1", &flContents, 0.0f, 0, 0, "%.0f"))
				{
					if (flContents != map->leaves[last_leaf].nContents * 1.0f)
						updatedLeafVec = true;
				}
				ImGui::PopItemWidth();
			}


			if (ImGui::Button(get_localized_string(LANG_0645).c_str()))
			{
				if (!g_app->reloading)
				{
					vis_debugger_press = true;

					std::vector<int> visLeafs;
					map->modelLeafs(0, visLeafs);

					for (int l = 0; l < map->leafCount - 1; l++)
					{
						if (std::find(visLeafs.begin(), visLeafs.end(), l) != visLeafs.end())
						{
							if (l == 0)
								continue;
							if (l == last_leaf || CHECKVISBIT(visData, l - 1))
							{
							}
							else
							{
								auto faceList = map->getLeafFaces(l);
								for (const auto& idx : faceList)
								{
									mapRenderer->highlightFace(idx, 1);
								}
							}
						}
						else
						{
							auto faceList = map->getLeafFaces(l + 1);
							for (const auto& idx : faceList)
							{
								mapRenderer->highlightFace(idx, 3);
							}
						}
					}

					for (auto l : visLeafs)
					{
						if (l == 0)
							continue;
						if (l == last_leaf || CHECKVISBIT(visData, l - 1))
						{
							auto faceList = map->getLeafFaces(l);
							for (const auto& idx : faceList)
							{
								mapRenderer->highlightFace(idx, 2);
							}
						}
					}
				}
			}

			if (ImGui::Button(get_localized_string("LANG_SELECT_LEAF_FACES").c_str()))
			{
				app->pickInfo.selectedFaces.clear();
				mapRenderer->preRenderFaces();
				auto faceList = map->getLeafFaces(last_leaf);
				for (const auto& idx : faceList)
				{
					app->pickInfo.selectedFaces.push_back(idx);
					mapRenderer->highlightFace(idx, 2);
				}
				pickCount++;
			}

			ImGui::BeginChild("##leaffacelist", ImVec2(0, 120), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);

			ImGuiListClipper face_clipper;
			face_clipper.Begin((int)leaf_faces.size());

			while (face_clipper.Step())
			{
				for (int line_no = face_clipper.DisplayStart; line_no < face_clipper.DisplayEnd; line_no++)
				{
					if (ImGui::Selectable(std::to_string(leaf_faces[line_no]).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
					{
						if (ImGui::IsMouseDoubleClicked(0))
						{

						}
					}
				}
			}

			ImGui::EndChild();


			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.0, 0.0, 1.0, 1.0 });
			ImGui::TextUnformatted("Blue is visible.");
			ImGui::PopStyleColor();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 1.0, 0.0, 0.0, 1.0 });
			ImGui::TextUnformatted("Red is invisible.");
			ImGui::PopStyleColor();

			if (last_leaf == 0)
				ImGui::BeginDisabled();
			ImGui::TextUnformatted("Double click for edit");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Double click for change leaf visibility for current leaf.");
				ImGui::EndTooltip();
			}
			if (last_leaf == 0)
				ImGui::EndDisabled();

			style.FrameBorderSize = 1.0f;

			ImGui::BeginChild("##leaflist", ImVec2(0, 240), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);

			ImGuiListClipper clipper;
			clipper.Begin((int)(vis_leafs.size() + invis_leafs.size()));
			bool vis_print = true;

			bool need_compress = false;

			while (clipper.Step())
			{
				for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
				{
					if (vis_print && line_no >= (int)vis_leafs.size())
					{
						vis_print = false;
					}

					if (last_leaf == 0)
						ImGui::BeginDisabled();
					ImGui::PushStyleColor(ImGuiCol_Text, vis_print ? ImVec4{ 0.0, 0.0, 1.0, 1.0 } : ImVec4{ 1.0, 0.0, 0.0, 1.0 });
					if (vis_print)
					{
						if (ImGui::Selectable(std::to_string(vis_leafs[line_no]).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
						{
							if (ImGui::IsMouseDoubleClicked(0))
							{
								int tmpleaf = vis_leafs[line_no];
								vis_leafs.erase(vis_leafs.begin() + line_no);
								invis_leafs.push_back(tmpleaf);
								vis_print = true;
								need_compress = true;
							}
						}
					}
					else
					{
						if (ImGui::Selectable(std::to_string(invis_leafs[line_no - vis_leafs.size()]).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick))
						{
							if (ImGui::IsMouseDoubleClicked(0))
							{
								int tmpleaf = invis_leafs[line_no - vis_leafs.size()];
								invis_leafs.erase(invis_leafs.begin() + (line_no - vis_leafs.size()));
								vis_leafs.push_back(tmpleaf);
								vis_print = true;
								need_compress = true;
							}
						}
					}
					if (last_leaf == 0)
						ImGui::EndDisabled();
					ImGui::PopStyleColor();
				}
			}
			clipper.End();


			ImGui::EndChild();
			if (last_leaf == 0)
				ImGui::BeginDisabled();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.0, 0.0, 1.0, 1.0 });

			if (ImGui::Button("Mark all visible"))
			{
				invis_leafs.clear();
				vis_leafs.clear();

				std::vector<int> visLeafs;
				map->modelLeafs(0, visLeafs);

				for (auto l : visLeafs)
				{
					print_log("{}\n", l);
					vis_leafs.push_back(l);
				}
				need_compress = true;
			}

			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 1.0, 0.0, 0.0, 1.0 });

			if (ImGui::Button("Mark all invisible"))
			{
				invis_leafs.clear();
				vis_leafs.clear();

				std::vector<int> visLeafs;
				map->modelLeafs(0, visLeafs);

				for (auto l : visLeafs)
				{
					print_log("{}\n", l);
					invis_leafs.push_back(l);
				}
				need_compress = true;
			}
			ImGui::PopStyleColor();

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.0, 0.0, 1.0, 1.0 });

			if (ImGui::Button("Mark visible for all"))
			{
				unsigned char* tmpVisData = new unsigned char[rowSize];
				unsigned char* tmpCompressed = new unsigned char[MAX_MAP_LEAVES / 8];

				// ADD ONE LEAF TO ALL VISIBILITY BYTES
				for (int i = 1; i < map->leafCount; i++)
				{
					if (map->leaves[i].nVisOffset >= 0)
					{
						memset(tmpVisData, 0, rowSize);
						DecompressVis(map->visdata + map->leaves[i].nVisOffset, tmpVisData, rowSize, map->leafCount - 1, map->visDataLength - map->leaves[i].nVisOffset);

						if (last_leaf > 0)
							SETVISBIT(tmpVisData, last_leaf - 1);

						memset(tmpCompressed, 0, MAX_MAP_LEAVES / 8);
						int size = CompressVis(tmpVisData, rowSize, tmpCompressed, MAX_MAP_LEAVES / 8);

						map->leaves[i].nVisOffset = map->visDataLength;

						unsigned char* newVisLump = new unsigned char[map->visDataLength + size];
						memcpy(newVisLump, map->visdata, map->visDataLength);
						memcpy(newVisLump + map->visDataLength, tmpCompressed, size);
						map->replace_lump(LUMP_VISIBILITY, newVisLump, map->visDataLength + size);
					}
				}

				delete[] tmpCompressed;
				delete[] tmpVisData;

				// repack visdata
				auto removed = map->remove_unused_model_structures(CLEAN_VISDATA);

				if (!removed.allZero())
					removed.print_delete_stats(1);

				mapRenderer->pushModelUndoState("UPDATE LEAF VISIBILITY", FL_VISIBILITY);
			}

			ImGui::PopStyleColor();
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 1.0, 0.0, 0.0, 1.0 });

			if (ImGui::Button("Mark invisible for all"))
			{
				unsigned char* tmpVisData = new unsigned char[rowSize];
				unsigned char* tmpCompressed = new unsigned char[MAX_MAP_LEAVES / 8];

				// ADD ONE LEAF TO ALL VISIBILITY BYTES
				for (int i = 1; i < map->leafCount; i++)
				{
					if (map->leaves[i].nVisOffset >= 0)
					{
						memset(tmpVisData, 0, rowSize);
						DecompressVis(map->visdata + map->leaves[i].nVisOffset, tmpVisData, rowSize, map->leafCount - 1, map->visDataLength - map->leaves[i].nVisOffset);

						if (last_leaf > 0)
							CLEARVISBIT(tmpVisData, last_leaf - 1);

						memset(tmpCompressed, 0, MAX_MAP_LEAVES / 8);
						int size = CompressVis(tmpVisData, rowSize, tmpCompressed, MAX_MAP_LEAVES / 8);

						map->leaves[i].nVisOffset = map->visDataLength;

						unsigned char* newVisLump = new unsigned char[map->visDataLength + size];
						memcpy(newVisLump, map->visdata, map->visDataLength);
						memcpy(newVisLump + map->visDataLength, tmpCompressed, size);
						map->replace_lump(LUMP_VISIBILITY, newVisLump, map->visDataLength + size);
					}
				}

				delete[] tmpCompressed;
				delete[] tmpVisData;

				// repack visdata
				auto removed = map->remove_unused_model_structures(CLEAN_VISDATA);

				if (!removed.allZero())
					removed.print_delete_stats(1);


				mapRenderer->pushModelUndoState("UPDATE LEAF VISIBILITY", FL_VISIBILITY);
			}
			ImGui::PopStyleColor();

			if (last_leaf == 0)
				ImGui::EndDisabled();

			BSPLEAF32& tmpLeaf = map->leaves[last_leaf];
			mins = tmpLeaf.nMins;
			maxs = tmpLeaf.nMaxs;

			ImGui::TextUnformatted("Leaf mins/maxs");
			ImGui::PushItemWidth(105);
			vertIdx++;
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &mins.x, 0.0f, 0, 0, "X1:%.2f"))
			{
				if (mins != tmpLeaf.nMins)
					updatedLeafVec = true;
			}

			vertIdx++;
			ImGui::SameLine();
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &mins.y, 0.0f, 0, 0, "Y1:%.2f"))
			{
				if (mins != tmpLeaf.nMins)
					updatedLeafVec = true;
			}

			vertIdx++;
			ImGui::SameLine();
			if (ImGui::DragFloat((fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx)).c_str(), &mins.z, 0.0f, 0, 0, "Z1:%.2f"))
			{
				if (mins != tmpLeaf.nMins)
					updatedLeafVec = true;
			}

			vertIdx++;
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &maxs.x, 0.0f, 0, 0, "X2:%.2f"))
			{
				if (maxs != tmpLeaf.nMaxs)
					updatedLeafVec = true;
			}

			vertIdx++;
			ImGui::SameLine();
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &maxs.y, 0.0f, 0, 0, "Y2:%.2f"))
			{
				if (maxs != tmpLeaf.nMaxs)
					updatedLeafVec = true;
			}

			vertIdx++;
			ImGui::SameLine();
			if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &maxs.z, 0.0f, 0, 0, "Z2:%.2f"))
			{
				if (maxs != tmpLeaf.nMaxs)
					updatedLeafVec = true;
			}
			vertIdx++;
			ImGui::PopItemWidth();
			if (updatedLeafVec)
			{
				tmpLeaf.nMins = mins;
				tmpLeaf.nMaxs = maxs;


				mapRenderer->leafCube->mins = tmpLeaf.nMins;
				mapRenderer->leafCube->maxs = tmpLeaf.nMaxs;

				map->leaves[last_leaf].nContents = (int)std::round(flContents);

				g_app->pointEntRenderer->genCubeBuffers(mapRenderer->leafCube);
				updatedLeafVec = false;

				mapRenderer->pushModelUndoState("EDIT LEAF", FL_LEAVES);
			}

			std::vector<int> leafNodes{};
			map->get_leaf_nodes(last_leaf, leafNodes);

			if (leafNodes.size())
			{
				int nodeIdx = leafNodes[0];
				BSPNODE32& tmpNode = map->nodes[nodeIdx];

				mins = tmpNode.nMins;
				maxs = tmpNode.nMaxs;


				if (ImGui::Button("Same as leaf"))
				{
					mins = tmpLeaf.nMins;
					maxs = tmpLeaf.nMaxs;
					updatedLeafVec = true;
				}

				ImGui::TextUnformatted(fmt::format("Leaf node [{}] mins/maxs", nodeIdx).c_str());


				ImGui::PushItemWidth(105);
				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &mins.x, 0.0f, 0, 0, "X1:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &mins.y, 0.0f, 0, 0, "Y1:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &mins.z, 0.0f, 0, 0, "Z1:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &maxs.x, 0.0f, 0, 0, "X2:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &maxs.y, 0.0f, 0, 0, "Y2:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &maxs.z, 0.0f, 0, 0, "Z2:%.2f"))
				{
					updatedLeafVec = true;
				}
				vertIdx++;
				ImGui::PopItemWidth();
				if (updatedLeafVec)
				{
					tmpNode.nMins = mins;
					tmpNode.nMaxs = maxs;

					mapRenderer->nodeCube->mins = tmpNode.nMins;
					mapRenderer->nodeCube->maxs = tmpNode.nMaxs;

					g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodeCube);
					updatedLeafVec = false;
					mapRenderer->pushModelUndoState("UPDATE LEAF NODE MINS/MAXS", FL_NODES);
				}
			}

			if (leafNodes.size())
			{
				int nodeIdx = leafNodes[0];
				BSPNODE32& tmpNode = map->nodes[nodeIdx];

				ImGui::PushItemWidth(105);
				ImGui::TextUnformatted(fmt::format("Node [{}] plane [{}]", nodeIdx, tmpNode.iPlane).c_str());


				BSPPLANE& tmpPlane = map->planes[tmpNode.iPlane];
				maxs = tmpPlane.vNormal;
				float dist = tmpPlane.fDist;

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0423)), vertIdx).c_str(), &maxs.x, 0.0f, 0, 0, "X:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0424)), vertIdx).c_str(), &maxs.y, 0.0f, 0, 0, "Y:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				ImGui::SameLine();
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &maxs.z, 0.0f, 0, 0, "Z:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (ImGui::DragFloat(fmt::format(fmt::runtime(get_localized_string(LANG_0425)), vertIdx).c_str(), &dist, 0.0f, 0, 0, "DIST:%.2f"))
				{
					updatedLeafVec = true;
				}

				vertIdx++;
				if (updatedLeafVec)
				{
					tmpPlane.vNormal = maxs;
					tmpPlane.fDist = dist;

					/*mapRenderer->nodePlaneCube->mins = { -32,-32,-32 };
					mapRenderer->nodePlaneCube->maxs = { 32, 32, 32 };

					mapRenderer->nodePlaneCube->mins += tmpPlane.vNormal;
					mapRenderer->nodePlaneCube->maxs += tmpPlane.vNormal;

					g_app->pointEntRenderer->genCubeBuffers(mapRenderer->nodePlaneCube);*/
					updatedLeafVec = false;
					mapRenderer->pushModelUndoState("UPDATE LEAF NODE MINS/MAXS", FL_NODES);
				}
				ImGui::PopItemWidth();
			}

			if (ImGui::Button("Create duplicate"))
			{
				last_leaf = map->clone_world_leaf(last_leaf);
				BSPLEAF32& leaf = map->leaves[last_leaf];
				app->goToCoords(getCenter(leaf.nMins, leaf.nMaxs));
				mapRenderer->pushModelUndoState("DUPLICATE LEAF", FL_LEAVES | FL_NODES | FL_PLANES | FL_MARKSURFACES | FL_VISIBILITY);
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Create new leaf with same settings.");
				ImGui::TextUnformatted("(BUT NOT WORKING!)");
				ImGui::EndTooltip();
			}

			if (need_compress)
			{
				leaf_decompress = true;
				memset(visData, 0, rowSize);

				for (auto sel : vis_leafs)
				{
					SETVISBIT(visData, sel - 1);
				}

				//for (auto unsel : invis_leafs)
				//{
				//	CLEARVISBIT(visData, unsel - 1);
				//}

				unsigned char* compressed = new unsigned char[MAX_MAP_LEAVES * 8];
				memset(compressed, 0, MAX_MAP_LEAVES / 8);
				int size = CompressVis(visData, rowSize, compressed, MAX_MAP_LEAVES / 8);

				map->leaves[last_leaf].nVisOffset = map->visDataLength;
				unsigned char* newVisLump = new unsigned char[map->visDataLength + size];
				memcpy(newVisLump, map->visdata, map->visDataLength);
				memcpy(newVisLump + map->visDataLength, compressed, size);
				map->replace_lump(LUMP_VISIBILITY, newVisLump, map->visDataLength + size);

				delete[] compressed;

				auto removed = map->remove_unused_model_structures(CLEAN_VISDATA);

				if (!removed.allZero())
					removed.print_delete_stats(1);

				mapRenderer->pushModelUndoState("UPDATE VIS LUMP", FL_LEAVES | FL_MARKSURFACES);
			}
		}
	}

	if (g_app->curLeftMouse != GLFW_RELEASE
		|| g_app->oldLeftMouse != GLFW_RELEASE)
	{
		scroll_x = ImGui::GetScrollX();
		scroll_y = ImGui::GetScrollY();
	}
	ImGui::End();
}

StatInfo Gui::calcStat(std::string name, unsigned int val, unsigned int max, bool isMem)
{
	StatInfo stat;
	const float meg = 1024 * 1024;
	float percent = max != 0 ? (val / (float)max) * 100 : 0;

	ImVec4 color;

	if (percent >= 100)
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
		tmp = fmt::format("{:>8.2f}", val / meg);
		stat.val = std::string(tmp);

		tmp = fmt::format("{:>8.2f}", max / meg);
		stat.max = std::string(tmp);
	}
	else
	{
		tmp = fmt::format("{:>8}", val);
		stat.val = std::string(tmp);

		tmp = fmt::format("{:>8}", max);
		stat.max = std::string(tmp);
	}
	tmp = fmt::format("{:3.1f}%", percent);
	stat.fullness = std::string(tmp);
	stat.color = color;

	stat.progress = max != 0 ? (float)val / (float)max : 0;

	return stat;
}

ModelInfo Gui::calcModelStat(Bsp* map, STRUCTUSAGE* modelInfo, unsigned int val, unsigned int max, bool isMem)
{
	ModelInfo stat;

	std::string classname = modelInfo->modelIdx == 0 ? "worldspawn" : "???";
	std::string targetname = modelInfo->modelIdx == 0 ? "" : "???";
	for (size_t k = 0; k < map->ents.size(); k++)
	{
		if (map->ents[k]->getBspModelIdx() == modelInfo->modelIdx)
		{
			targetname = map->ents[k]->keyvalues["targetname"];
			classname = map->ents[k]->keyvalues["classname"];
			stat.entIdx = (int)k;
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
		for (size_t k = 0; k < mapRenderers.size() && !anyHullValid[i]; k++)
		{
			Bsp* map = mapRenderers[k]->map;

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


	for (size_t i = 0; i < app->pickInfo.selectedFaces.size(); i++)
	{
		int size[2];
		map->GetFaceLightmapSize((int)app->pickInfo.selectedFaces[i], size);
		if ((size[0] > MAX_SURFACE_EXTENT) || (size[1] > MAX_SURFACE_EXTENT) || size[0] < 0 || size[1] < 0)
		{
			print_log(get_localized_string(LANG_0426), size[0], size[1]);
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
