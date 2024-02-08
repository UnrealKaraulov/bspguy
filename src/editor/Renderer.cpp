#include "lang.h"
#include "Settings.h"
#include "Renderer.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Gui.h"
#include <algorithm>
#include <map>
#include <sstream>
#include <chrono>
#include <execution>
#include "filedialog/ImFileDialog.h"
#include "lodepng.h"
#include <cstdlib>

Renderer* g_app = NULL;
std::vector<BspRenderer*> mapRenderers{};

int current_fps = 0;

vec2 mousePos;
vec3 cameraOrigin;
vec3 cameraAngles;

int pickCount = 0; // used to give unique IDs to text inputs so switching ents doesn't update keys accidentally
int vertPickCount = 0;


size_t g_drawFrameId = 0;

Texture* whiteTex = NULL;
Texture* redTex = NULL;
Texture* yellowTex = NULL;
Texture* greyTex = NULL;
Texture* blackTex = NULL;
Texture* blueTex = NULL;
Texture* missingTex = NULL;
Texture* missingTex_rgba = NULL;
Texture* aaatriggerTex_rgba = NULL;
Texture* aaatriggerTex = NULL;
Texture* skyTex_rgba = NULL;
Texture* clipTex_rgba = NULL;

std::future<void> Renderer::fgdFuture;

void error_callback(int error, const char* description)
{
	print_log(get_localized_string(LANG_0895), error, description);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
	{
		g_app->hideGui = !g_app->hideGui;
	}

	if (action == GLFW_REPEAT)
		return;
	g_app->oldPressed[key] = g_app->pressed[key];
	g_app->pressed[key] = action != GLFW_RELEASE;
}

void drop_callback(GLFWwindow* window, int count, const char** paths)
{
	if (!g_app->isLoading && count > 0 && paths[0] && paths[0][0] != '\0')
	{
		fs::path tmpPath = paths[0];

		std::string lowerPath = toLowerCase(tmpPath.string());

		if (fileExists(tmpPath.string()))
		{
			if (lowerPath.ends_with(".bsp"))
			{
				print_log(get_localized_string(LANG_0896), tmpPath.string());
				g_app->addMap(new Bsp(tmpPath.string()));
			}
			else if (lowerPath.ends_with(".mdl"))
			{
				print_log(get_localized_string(LANG_0897), tmpPath.string());
				g_app->addMap(new Bsp(tmpPath.string()));
			}
			else
			{
				print_log(get_localized_string(LANG_0898), tmpPath.string());
			}
		}
		else
		{
			print_log(get_localized_string(LANG_0899), tmpPath.string());
		}
	}
	else if (g_app->isLoading)
	{
		print_log(get_localized_string(LANG_0900));
	}
}

void window_size_callback(GLFWwindow* window, int width, int height)
{
	if (g_settings.maximized || width == 0 || height == 0)
	{
		return; // ignore size change when maximized, or else iconifying doesn't change size at all
	}
	g_settings.windowWidth = width;
	g_settings.windowHeight = height;
}

void window_pos_callback(GLFWwindow* window, int x, int y)
{
	g_settings.windowX = x;
	g_settings.windowY = y;
}

void window_maximize_callback(GLFWwindow* window, int maximized)
{
	g_settings.maximized = maximized == GLFW_TRUE;
}

void window_minimize_callback(GLFWwindow* window, int iconified)
{
	g_app->is_minimized = iconified == GLFW_TRUE;
}

void window_focus_callback(GLFWwindow* window, int focused)
{
	g_app->is_focused = focused == GLFW_TRUE;
}

void window_close_callback(GLFWwindow* window)
{
	g_settings.save();
	print_log(get_localized_string(LANG_0901));

#ifdef MINGW 
	std::set_terminate(NULL);
	std::terminate();
#else 
	std::quick_exit(0);
#endif
}

int g_scroll = 0;

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	g_scroll += (int)round(yoffset);
}

Renderer::Renderer()
{
	g_app = this;
	gl_errors = 0;
	g_drawFrameId = 0;

	glfwSetErrorCallback(error_callback);

	if (!glfwInit())
	{
		print_log(get_localized_string(LANG_0902));
		return;
	}

	gui = new Gui(this);

	loadSettings();

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 0);

	window = glfwCreateWindow(g_settings.windowWidth, g_settings.windowHeight, "bspguy", NULL, NULL);

	glfwSetWindowPos(window, g_settings.windowX, g_settings.windowY);

	// setting size again to fix issue where window is too small because it was
	// moved to a monitor with a different DPI than the one it was created for
	glfwSetWindowSize(window, g_settings.windowWidth, g_settings.windowHeight);
	if (g_settings.maximized)
	{
		glfwMaximizeWindow(window);
	}

	if (!window)
	{
		print_log(get_localized_string(LANG_0903));
		return;
	}

	glfwMakeContextCurrent(window);
	glfwSetDropCallback(window, drop_callback);
	glfwSetKeyCallback(window, key_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetWindowSizeCallback(window, window_size_callback);
	glfwSetWindowPosCallback(window, window_pos_callback);
	glfwSetWindowCloseCallback(window, window_close_callback);
	glfwSetWindowIconifyCallback(window, window_minimize_callback);
	glfwSetWindowMaximizeCallback(window, window_maximize_callback);
	glfwSetWindowFocusCallback(window, window_focus_callback);

	glewInit();

	glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, GL_FASTEST);
	glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_FASTEST);
	glHint(GL_TEXTURE_COMPRESSION_HINT, GL_FASTEST);

	unsigned char* img_dat = NULL;
	unsigned int w, h;

	lodepng_decode32_file(&img_dat, &w, &h, "./pictures/missing.png");
	missingTex_rgba = new Texture(w, h, img_dat, "missing", true);
	img_dat = NULL;

	lodepng_decode32_file(&img_dat, &w, &h, "./pictures/aaatrigger.png");
	aaatriggerTex_rgba = new Texture(w, h, img_dat, "aaatrigger", true);
	img_dat = NULL;

	lodepng_decode24_file(&img_dat, &w, &h, "./pictures/aaatrigger.png");
	aaatriggerTex = new Texture(w, h, img_dat, "aaatrigger");
	img_dat = NULL;

	lodepng_decode32_file(&img_dat, &w, &h, "./pictures/sky.png");
	skyTex_rgba = new Texture(w, h, img_dat, "sky", true);
	img_dat = NULL;

	lodepng_decode32_file(&img_dat, &w, &h, "./pictures/clip.png");
	clipTex_rgba = new Texture(w, h, img_dat, "clip", true);
	img_dat = NULL;

	lodepng_decode24_file(&img_dat, &w, &h, "./pictures/missing.png");
	missingTex = new Texture(w, h, img_dat, "missing_rgb");
	img_dat = NULL;

	lodepng_decode24_file(&img_dat, &w, &h, "./pictures/white.png");
	whiteTex = new Texture(w, h, img_dat, "white");
	img_dat = NULL;

	lodepng_decode24_file(&img_dat, &w, &h, "./pictures/grey.png");
	greyTex = new Texture(w, h, img_dat, "grey");
	img_dat = NULL;

	lodepng_decode24_file(&img_dat, &w, &h, "./pictures/red.png");
	redTex = new Texture(w, h, img_dat, "red");
	img_dat = NULL;

	lodepng_decode24_file(&img_dat, &w, &h, "./pictures/yellow.png");
	yellowTex = new Texture(w, h, img_dat, "yellow");
	img_dat = NULL;

	lodepng_decode24_file(&img_dat, &w, &h, "./pictures/black.png");
	blackTex = new Texture(w, h, img_dat, "black");
	img_dat = NULL;

	lodepng_decode24_file(&img_dat, &w, &h, "./pictures/blue.png");
	blueTex = new Texture(w, h, img_dat, "blue");
	img_dat = NULL;


	missingTex_rgba->upload();
	aaatriggerTex_rgba->upload();
	aaatriggerTex->upload();
	skyTex_rgba->upload();
	clipTex_rgba->upload();
	missingTex->upload();
	whiteTex->upload();
	redTex->upload();
	yellowTex->upload();
	greyTex->upload();
	blackTex->upload();
	blueTex->upload();

	//GLuint in;
	//glGenVertexArrays(1, &in);
	//glBindVertexArray(in);
	glGetFloatv(GL_LINE_WIDTH_RANGE, lineWidthRange);
	glLineWidth(1.3f);

	// init to black screen instead of white
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glfwSwapBuffers(window);

	bspShader = new ShaderProgram(Shaders::g_shader_multitexture_vertex, Shaders::g_shader_multitexture_fragment);
	bspShader->setMatrixes(&modelView, &modelViewProjection);
	bspShader->setMatrixNames(NULL, "modelViewProjection");

	bspShader->addAttribute(TEX_2F, "vTex");
	bspShader->addAttribute(3, GL_FLOAT, 0, "vLightmapTex0");
	bspShader->addAttribute(3, GL_FLOAT, 0, "vLightmapTex1");
	bspShader->addAttribute(3, GL_FLOAT, 0, "vLightmapTex2");
	bspShader->addAttribute(3, GL_FLOAT, 0, "vLightmapTex3");
	bspShader->addAttribute(4, GL_FLOAT, 0, "vColor");
	bspShader->addAttribute(POS_3F, "vPosition");

	modelShader = new ShaderProgram(Shaders::g_shader_model_vertex, Shaders::g_shader_model_fragment);
	modelShader->setMatrixes(&modelView, &modelViewProjection);
	modelShader->setMatrixNames(NULL, "modelViewProjection");
	modelShader->addAttribute(POS_3F, "vPosition");
	modelShader->addAttribute(TEX_2F, "vTex");

	colorShader = new ShaderProgram(Shaders::g_shader_cVert_vertex, Shaders::g_shader_cVert_fragment);
	colorShader->setMatrixes(&modelView, &modelViewProjection);
	colorShader->setMatrixNames(NULL, "modelViewProjection");
	colorShader->setVertexAttributeNames("vPosition", "vColor", NULL, COLOR_4B | POS_3F);

	bspShader->bind();
	glUniform1i(glGetUniformLocation(g_app->bspShader->ID, "sTex"), 0);
	for (int s = 0; s < MAX_LIGHTMAPS; s++)
	{
		unsigned int sLightmapTexIds = glGetUniformLocation(g_app->bspShader->ID, ("sLightmapTex" + std::to_string(s)).c_str());
		// assign lightmap texture units (skips the normal texture unit)
		glUniform1i(sLightmapTexIds, s + 1);
	}
	bspShader->bindAttributes();

	modelShader->bind();
	glUniform1i(glGetUniformLocation(g_app->modelShader->ID, "sTex"), 0);
	modelShader->bindAttributes();

	colorShader->bind();
	colorShaderMultId = glGetUniformLocation(g_app->colorShader->ID, "colorMult");
	glUniform4f(colorShaderMultId, 0.0, 0.0, 0.0, 0.0);
	colorShader->bindAttributes();

	clearSelection();

	oldLeftMouse = curLeftMouse = oldRightMouse = curRightMouse = 0;

	blockMoving = false;
	showDragAxes = true;

	gui->init();

	g_progress.simpleMode = true;

	pointEntRenderer = new PointEntRenderer(NULL);

	reloading = true;
	fgdFuture = std::async(std::launch::async, &Renderer::loadFgds, this);

	hoverAxis = -1;
	saveTranformResult = false;
}

Renderer::~Renderer()
{
	g_settings.save();
	print_log(get_localized_string(LANG_0901));
	glfwTerminate();
#ifdef MINGW 
	std::set_terminate(NULL);
	std::terminate();
#else 
	std::quick_exit(0);
#endif
}

void Renderer::updateWindowTitle(double _curTime)
{
	static double lastTitleTime = 0.0f;
	if (_curTime - lastTitleTime > 0.25)
	{
		lastTitleTime = _curTime;
		if (SelectedMap)
		{
			std::string smallPath = SelectedMap->bsp_path;
			if (smallPath.length() > 51) {
				smallPath = smallPath.substr(0, 18) + "..." + smallPath.substr(smallPath.length() - 32);
			}
			if (g_progress.progress_total > 0)
			{
				float percent = (g_progress.progress / (float)g_progress.progress_total) * 100.0f;
				glfwSetWindowTitle(window, fmt::format("bspguy [fps {:>4}] - [{} = {:.0f}%]", current_fps, g_progress.progress_title, percent).c_str());
			}
			else
			{
				glfwSetWindowTitle(window, fmt::format("bspguy [fps {:>4}] - {}", current_fps, std::string("bspguy - ") + smallPath).c_str());
			}
		}
	}
}

void Renderer::renderLoop()
{
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_max_texture_size);

	if (LIGHTMAP_ATLAS_SIZE > gl_max_texture_size)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0904), gl_max_texture_size);
		LIGHTMAP_ATLAS_SIZE = gl_max_texture_size;
	}

	{
		line_verts = new cVert[2];
		lineBuf = new VertexBuffer(colorShader, line_verts, 2, GL_LINES);
	}

	{
		plane_verts = new cQuad(cVert(), cVert(), cVert(), cVert());
		planeBuf = new VertexBuffer(colorShader, plane_verts, 6, GL_TRIANGLES);
	}

	{
		moveAxes.dimColor[0] = { 110, 0, 160, 255 };
		moveAxes.dimColor[1] = { 0, 0, 220, 255 };
		moveAxes.dimColor[2] = { 0, 160, 0, 255 };
		moveAxes.dimColor[3] = { 160, 160, 160, 255 };

		moveAxes.hoverColor[0] = { 128, 64, 255, 255 };
		moveAxes.hoverColor[1] = { 64, 64, 255, 255 };
		moveAxes.hoverColor[2] = { 64, 255, 64, 255 };
		moveAxes.hoverColor[3] = { 255, 255, 255, 255 };
		// flipped for HL coords
		moveAxes.buffer = new VertexBuffer(colorShader, &moveAxes.model, 6 * 6 * 4, GL_TRIANGLES);
		moveAxes.numAxes = 4;
	}

	{
		scaleAxes.dimColor[0] = { 110, 0, 160, 255 };
		scaleAxes.dimColor[1] = { 0, 0, 220, 255 };
		scaleAxes.dimColor[2] = { 0, 160, 0, 255 };

		scaleAxes.dimColor[3] = { 110, 0, 160, 255 };
		scaleAxes.dimColor[4] = { 0, 0, 220, 255 };
		scaleAxes.dimColor[5] = { 0, 160, 0, 255 };

		scaleAxes.hoverColor[0] = { 128, 64, 255, 255 };
		scaleAxes.hoverColor[1] = { 64, 64, 255, 255 };
		scaleAxes.hoverColor[2] = { 64, 255, 64, 255 };

		scaleAxes.hoverColor[3] = { 128, 64, 255, 255 };
		scaleAxes.hoverColor[4] = { 64, 64, 255, 255 };
		scaleAxes.hoverColor[5] = { 64, 255, 64, 255 };
		// flipped for HL coords
		scaleAxes.buffer = new VertexBuffer(colorShader, &scaleAxes.model, 6 * 6 * 6, GL_TRIANGLES);
		scaleAxes.numAxes = 6;
	}

	updateDragAxes();

	oldTime = glfwGetTime();
	curTime = oldTime;

	glfwSwapInterval(0);
	static int vsync = -1;


	static int tmpPickIdx = -1, tmpVertPickIdx = -1, tmpTransformTarget = -1, tmpModelIdx = -1;

	static bool isScalingObject = false;
	static bool isMovingOrigin = false;
	static bool isTransformingValid = false;
	static bool isTransformingWorld = false;

	memset(pressed, 0, sizeof(pressed));
	memset(oldPressed, 0, sizeof(oldPressed));

	//glEnable(GL_DEPTH_CLAMP);
	//glEnable(GL_STENCIL_TEST);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);

	int frame_fps = 0;
	current_fps = 0;

	double fpsTime = 0.0;
	double mouseTime = 0.0;

	double framerateTime = 0.0;

	double xpos = 0.0, ypos = 0.0;

	if (SelectedMap && SelectedMap->is_mdl_model)
		glClearColor(0.25, 0.25, 0.25, 1.0);
	else
		glClearColor(0.0, 0.0, 0.0, 1.0);


	while (!glfwWindowShouldClose(window))
	{
		curTime = glfwGetTime();
		if (vsync != 0 || abs(curTime - framerateTime) > 1.0f / g_settings.fpslimit)
		{

			if (abs(curTime - fpsTime) >= 0.50)
			{
				fpsTime = curTime;
				current_fps = frame_fps * 2;
				frame_fps = 0;
			}

			mousePos = vec2((float)xpos, (float)ypos);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			//Update keyboard / mouse state 
			oldLeftMouse = curLeftMouse;
			curLeftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
			oldRightMouse = curRightMouse;
			curRightMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

			DebugKeyPressed = pressed[GLFW_KEY_F1];

			anyCtrlPressed = pressed[GLFW_KEY_LEFT_CONTROL] || pressed[GLFW_KEY_RIGHT_CONTROL];
			anyAltPressed = pressed[GLFW_KEY_LEFT_ALT] || pressed[GLFW_KEY_RIGHT_ALT];
			anyShiftPressed = pressed[GLFW_KEY_LEFT_SHIFT] || pressed[GLFW_KEY_RIGHT_SHIFT];

			oldControl = canControl;

			canControl = /*!gui->imgui_io->WantCaptureKeyboard && */ !gui->imgui_io->WantTextInput && !gui->imgui_io->WantCaptureMouseUnlessPopupClose;

			updateWindowTitle(curTime);

			int modelIdx = -1;
			auto entIdx = pickInfo.selectedEnts;
			Entity* ent = NULL;
			if (SelectedMap && entIdx.size() && entIdx[0] < (int)SelectedMap->ents.size())
			{
				ent = SelectedMap->ents[entIdx[0]];
				modelIdx = ent->getBspModelIdx();
			}

			bool updatePickCount = false;

			framerateTime = curTime;
			g_drawFrameId++;
			frame_fps++;

			if (SelectedMap && (tmpPickIdx != pickCount || tmpVertPickIdx != vertPickCount || transformTarget != tmpTransformTarget || tmpModelIdx != modelIdx))
			{
				if (transformTarget != tmpTransformTarget && transformTarget == TRANSFORM_VERTEX)
				{
					updateModelVerts();
				}
				else if (!modelVerts.size() || tmpModelIdx != modelIdx)
				{
					updateModelVerts();
				}

				if (pickMode != PICK_OBJECT)
				{
					pickInfo.selectedEnts.clear();
					for (auto& f : pickInfo.selectedFaces)
					{
						int mdl = SelectedMap->get_model_from_face((int)f);
						if (mdl > 0 && mdl < SelectedMap->modelCount)
						{
							int mdl_ent = SelectedMap->get_ent_from_model(mdl);
							if (mdl_ent >= 0 && mdl_ent < SelectedMap->ents.size())
							{
								pickInfo.AddSelectedEnt(mdl_ent);
							}
						}
					}
				}

				updatePickCount = true;
				isTransformableSolid = modelIdx > 0 || (entIdx.size() && SelectedMap->ents[entIdx[0]]->getBspModelIdx() < 0);

				if (!isTransformableSolid && pickInfo.selectedEnts.size())
				{
					if (SelectedMap && ent && ent->hasKey("classname") &&
						ent->keyvalues["classname"] == "worldspawn")
					{
						isTransformableSolid = true;
					}
				}

				modelUsesSharedStructures = modelIdx == 0 || modelIdx > 0 && SelectedMap->does_model_use_shared_structures(modelIdx);

				isScalingObject = transformMode == TRANSFORM_MODE_SCALE && transformTarget == TRANSFORM_OBJECT;
				isMovingOrigin = transformMode == TRANSFORM_MODE_MOVE && transformTarget == TRANSFORM_ORIGIN && modelIdx >= 0;
				isTransformingValid = (!modelUsesSharedStructures || (transformMode == TRANSFORM_MODE_MOVE && transformTarget != TRANSFORM_VERTEX))
					|| (isTransformableSolid && isScalingObject);
				isTransformingWorld = ent && ent->isWorldSpawn() && transformTarget != TRANSFORM_OBJECT;

				if (ent && modelIdx < 0)
					invalidSolid = false;
				else
				{
					invalidSolid = !modelVerts.size() || !SelectedMap->vertex_manipulation_sync(modelIdx, modelVerts, false);
					if (!invalidSolid)
					{
						std::vector<TransformVert> tmpVerts;
						SelectedMap->getModelPlaneIntersectVerts(modelIdx, tmpVerts); // for vertex manipulation + scaling

						Solid modelSolid;
						if (!getModelSolid(tmpVerts, SelectedMap, modelSolid))
						{
							invalidSolid = true;
						}
					}
				}
			}

			setupView();

			drawEntConnections();

			isLoading = reloading;

			for (size_t i = 0; i < mapRenderers.size(); i++)
			{
				if (!mapRenderers[i])
				{
					continue;
				}

				mapRenderers[i]->clearDrawCache();

				Bsp* curMap = mapRenderers[i]->map;
				if (!curMap || !curMap->bsp_name.size())
					continue;

				if (SelectedMap && getSelectedMap() != curMap && (!curMap->is_bsp_model || curMap->parentMap != SelectedMap))
				{
					continue;
				}

				if (SelectedMap->is_mdl_model && SelectedMap->mdl)
				{
					matmodel.loadIdentity();
					modelShader->bind();
					modelShader->updateMatrixes();
					SelectedMap->mdl->DrawMDL();
					continue;
				}

				std::set<int> modelidskip;

				if (curMap->ents.size() && !isLoading)
				{
					if (curMap->is_bsp_model)
					{
						for (size_t n = 0; n < mapRenderers.size(); n++)
						{
							if (n == i)
								continue;

							Bsp* anotherMap = mapRenderers[n]->map;
							if (anotherMap && anotherMap->ents.size())
							{
								vec3 anotherMapOrigin = anotherMap->ents[0]->origin;
								for (int s = 0; s < (int)anotherMap->ents.size(); s++)
								{
									Entity* tmpEnt = anotherMap->ents[s];
									if (tmpEnt && tmpEnt->hasKey("model"))
									{
										if (!modelidskip.count(s))
										{
											if (basename(tmpEnt->keyvalues["model"]) == basename(curMap->bsp_path))
											{
												modelidskip.insert(s);
												curMap->ents[0]->setOrAddKeyvalue("origin", (tmpEnt->origin + anotherMapOrigin).toKeyvalueString());
												break;
											}
										}
									}
								}
							}
						}
					}
				}

				mapRenderers[i]->render(transformTarget == TRANSFORM_VERTEX, clipnodeRenderHull);


				if (!mapRenderers[i]->isFinishedLoading())
				{
					isLoading = true;
				}
			}

			if (SelectedMap)
			{
				if (debugClipnodes && modelIdx > 0)
				{
					matmodel.loadIdentity();
					vec3 offset = (SelectedMap->getBspRender()->mapOffset + (entIdx.size() ? SelectedMap->ents[entIdx[0]]->origin : vec3())).flip();
					matmodel.translate(offset.x, offset.y, offset.z);
					colorShader->updateMatrixes();
					BSPMODEL& pickModel = SelectedMap->models[modelIdx];
					int currentPlane = 0;
					glDisable(GL_CULL_FACE);
					drawClipnodes(SelectedMap, pickModel.iHeadnodes[1], currentPlane, debugInt, pickModel.vOrigin);
					glEnable(GL_CULL_FACE);
					debugIntMax = currentPlane - 1;
				}

				if (debugNodes && modelIdx > 0)
				{
					matmodel.loadIdentity();
					vec3 offset = (SelectedMap->getBspRender()->mapOffset + (entIdx.size() > 0 ? SelectedMap->ents[entIdx[0]]->origin : vec3())).flip();
					matmodel.translate(offset.x, offset.y, offset.z);
					colorShader->updateMatrixes();
					BSPMODEL& pickModel = SelectedMap->models[modelIdx];
					int currentPlane = 0;
					glDisable(GL_CULL_FACE);
					drawNodes(SelectedMap, pickModel.iHeadnodes[0], currentPlane, debugNode, pickModel.vOrigin);
					glEnable(GL_CULL_FACE);
					debugNodeMax = currentPlane - 1;
				}

				if (g_render_flags & RENDER_ORIGIN)
				{
					glDisable(GL_CULL_FACE);
					matmodel.loadIdentity();
					vec3 offset = SelectedMap->getBspRender()->mapOffset;
					colorShader->updateMatrixes();
					vec3 p1 = offset + vec3(-10240.0f, 0.0f, 0.0f);
					vec3 p2 = offset + vec3(10240.0f, 0.0f, 0.0f);
					drawLine(p1, p2, { 128, 128, 255, 255 });
					vec3 p3 = offset + vec3(0.0f, -10240.0f, 0.0f);
					vec3 p4 = offset + vec3(0.0f, 10240.0f, 0.0f);
					drawLine(p3, p4, { 0, 255, 0, 255 });
					vec3 p5 = offset + vec3(0.0f, 0.0f, -10240.0f);
					vec3 p6 = offset + vec3(0.0f, 0.0f, 10240.0f);
					drawLine(p5, p6, { 0, 0, 255, 255 });
					glEnable(GL_CULL_FACE);
				}
			}


			glDepthMask(GL_FALSE);
			glDepthFunc(GL_ALWAYS);

			if (entConnectionPoints && (g_render_flags & RENDER_ENT_CONNECTIONS))
			{
				matmodel.loadIdentity();
				colorShader->updateMatrixes();
				entConnectionPoints->drawFull();
			}

			if (!entIdx.size())
			{
				if (SelectedMap && SelectedMap->is_bsp_model)
				{
					SelectedMap->selectModelEnt();
				}
			}

			if (showDragAxes && pickMode == pick_modes::PICK_OBJECT)
			{
				if (!movingEnt && !isTransformingWorld && entIdx.size() && (isTransformingValid || isMovingOrigin))
				{
					drawTransformAxes();
				}
			}

			if (modelIdx > 0 && pickMode == PICK_OBJECT)
			{
				if (transformTarget == TRANSFORM_VERTEX && isTransformableSolid)
				{
					glDisable(GL_CULL_FACE);
					drawModelVerts();
					glEnable(GL_CULL_FACE);
				}
				if (transformTarget == TRANSFORM_ORIGIN)
				{
					drawModelOrigin(modelIdx);
				}
			}

			glDepthMask(GL_TRUE);
			glDepthFunc(GL_LESS);
			vec3 forward, right, up;
			makeVectors(cameraAngles, forward, right, up);
			if (!hideGui)
				gui->draw();

			controls();

			if (reloading && fgdFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
			{
				postLoadFgds();
				for (size_t i = 0; i < mapRenderers.size(); i++)
				{
					mapRenderers[i]->preRenderEnts();
					if (reloadingGameDir)
					{
						mapRenderers[i]->reloadTextures();
					}
				}
				reloading = reloadingGameDir = false;
			}

			int glerror = glGetError();
			while (glerror != GL_NO_ERROR)
			{
				gl_errors++;
#ifndef NDEBUG
				std::cout << fmt::format(fmt::runtime(get_localized_string(LANG_0905)), glerror) << std::endl;
#endif 
				glerror = glGetError();
			}

			if (updatePickCount)
			{
				tmpModelIdx = modelIdx;
				tmpTransformTarget = transformTarget;
				tmpPickIdx = pickCount;
				tmpVertPickIdx = vertPickCount;
			}

			memcpy(oldPressed, pressed, sizeof(pressed));

			glfwSwapBuffers(window);
			glfwPollEvents();

			if (abs(curTime - mouseTime) >= 0.016)
			{
				if (vsync != (g_settings.vsync ? 1 : 0))
				{
					glfwSwapInterval(g_settings.vsync);
					vsync = (g_settings.vsync ? 1 : 0);
				}

				mouseTime = curTime;
				glfwGetCursorPos(window, &xpos, &ypos);
			}


			if (is_minimized || !is_focused)
			{
				using namespace std::chrono_literals;
				std::this_thread::sleep_for(25ms);
			}
			oldTime = curTime;
		}
	}

	glfwTerminate();
}

void Renderer::postLoadFgds()
{
	delete pointEntRenderer;
	delete fgd;

	pointEntRenderer = swapPointEntRenderer;
	if (pointEntRenderer)
		fgd = pointEntRenderer->fgd;
	swapPointEntRenderer = NULL;
}

void Renderer::postLoadFgdsAndTextures()
{
	if (reloading)
	{
		print_log(get_localized_string(LANG_0906));
		return;
	}
	reloading = reloadingGameDir = true;
	fgdFuture = std::async(std::launch::async, &Renderer::loadFgds, this);
}

void Renderer::clearMaps()
{
	for (size_t i = 0; i < mapRenderers.size(); i++)
	{
		delete mapRenderers[i];
	}
	mapRenderers.clear();
	clearSelection();
}

void Renderer::reloadMaps()
{
	std::vector<std::string> reloadPaths;

	for (size_t i = 0; i < mapRenderers.size(); i++)
	{
		reloadPaths.push_back(mapRenderers[i]->map->bsp_path);
	}

	clearMaps();

	for (size_t i = 0; i < reloadPaths.size(); i++)
	{
		addMap(new Bsp(reloadPaths[i]));
	}

	reloadBspModels();
	print_log(get_localized_string(LANG_0908));
}

void Renderer::saveSettings()
{
	g_settings.debug_open = gui->showDebugWidget;
	g_settings.keyvalue_open = gui->showKeyvalueWidget;
	g_settings.texbrowser_open = gui->showTextureBrowser;
	g_settings.goto_open = gui->showGOTOWidget;
	g_settings.transform_open = gui->showTransformWidget;
	g_settings.log_open = gui->showLogWidget;
	g_settings.limits_open = gui->showLimitsWidget;
	g_settings.entreport_open = gui->showEntityReport;
	g_settings.settings_tab = gui->settingsTab;
	g_settings.verboseLogs = g_verbose;
	g_settings.zfar = zFar;
	g_settings.fov = fov;
	g_settings.render_flags = g_render_flags;
	g_settings.fontSize = gui->fontSize;
	g_settings.moveSpeed = moveSpeed;
	g_settings.rotSpeed = rotationSpeed;
}

void Renderer::loadSettings()
{
	gui->showDebugWidget = g_settings.debug_open;
	gui->showTextureBrowser = g_settings.texbrowser_open;
	gui->showGOTOWidget = g_settings.goto_open;
	gui->showTransformWidget = g_settings.transform_open;
	gui->showLogWidget = g_settings.log_open;
	gui->showLimitsWidget = g_settings.limits_open;
	gui->showEntityReport = g_settings.entreport_open;
	gui->showKeyvalueWidget = g_settings.keyvalue_open;

	gui->settingsTab = g_settings.settings_tab;
	gui->openSavedTabs = true;

	g_verbose = g_settings.verboseLogs;
	zFar = g_settings.zfar;
	fov = g_settings.fov;

	if (zFar < 1000.0f)
		zFar = 1000.0f;
	if (fov < 40.0f)
		fov = 40.0f;

	g_render_flags = g_settings.render_flags;
	gui->fontSize = g_settings.fontSize;
	rotationSpeed = g_settings.rotSpeed;
	moveSpeed = g_settings.moveSpeed;

	gui->shouldReloadFonts = true;
	gui->settingLoaded = true;
}

void Renderer::loadFgds()
{
	Fgd* mergedFgd = NULL;
	for (size_t i = 0; i < g_settings.fgdPaths.size(); i++)
	{
		if (!g_settings.fgdPaths[i].enabled)
			continue;
		std::string newFgdPath;
		if (FindPathInAssets(NULL, g_settings.fgdPaths[i].path, newFgdPath))
		{
			Fgd* tmp = new Fgd(newFgdPath);
			if (!tmp->parse())
			{
				print_log(get_localized_string(LANG_0909), g_settings.fgdPaths[i].path);
				continue;
			}
			if (mergedFgd == NULL)
			{
				mergedFgd = tmp;
			}
			else
			{
				mergedFgd->merge(tmp);
				delete tmp;
			}
		}
		else
		{
			FindPathInAssets(NULL, g_settings.fgdPaths[i].path, newFgdPath, true);
			print_log(get_localized_string(LANG_0910), g_settings.fgdPaths[i].path);
			g_settings.fgdPaths[i].enabled = false;
			continue;
		}
	}

	swapPointEntRenderer = new PointEntRenderer(mergedFgd);
}

void Renderer::drawModelVerts()
{
	Bsp* map = SelectedMap;
	auto entIdx = pickInfo.selectedEnts;

	if (!map || entIdx.empty())
		return;
	BspRenderer* rend = map->getBspRender();
	if (!rend)
		return;

	Entity* ent = map->ents[entIdx[0]];
	if (ent->getBspModelIdx() < 0)
		return;

	if (!modelVertBuff || modelVertBuff->numVerts == 0 || !modelVerts.size())
	{
		return;
	}

	glClear(GL_DEPTH_BUFFER_BIT);

	COLOR4 vertDimColor = { 200, 200, 200, 255 };
	COLOR4 vertHoverColor = { 255, 255, 255, 255 };
	COLOR4 edgeDimColor = { 255, 128, 0, 255 };
	COLOR4 edgeHoverColor = { 255, 255, 0, 255 };
	COLOR4 selectColor = { 0, 128, 255, 255 };
	COLOR4 hoverSelectColor = { 96, 200, 255, 255 };
	vec3 entOrigin = ent->origin;

	if (modelUsesSharedStructures)
	{
		vertDimColor = { 32, 32, 32, 255 };
		edgeDimColor = { 64, 64, 32, 255 };
	}

	int cubeIdx = 0;
	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		vec3 ori = modelVerts[i].pos + entOrigin;
		float s = (ori - rend->localCameraOrigin).length() * vertExtentFactor;
		ori = ori.flip();

		if (anyEdgeSelected)
		{
			s = 0.0f; // can't select certs when edges are selected
		}

		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		COLOR4 color;
		if (modelVerts[i].selected)
		{
			color = (int)i == hoverVert ? hoverSelectColor : selectColor;
		}
		else
		{
			color = (int)i == hoverVert ? vertHoverColor : vertDimColor;
		}
		modelVertCubes[cubeIdx++] = cCube(min, max, color);
	}

	for (size_t i = 0; i < modelEdges.size(); i++)
	{
		vec3 ori = getEdgeControlPoint(modelVerts, modelEdges[i]) + entOrigin;
		float s = (ori - rend->localCameraOrigin).length() * vertExtentFactor;
		ori = ori.flip();

		if (anyVertSelected && !anyEdgeSelected)
		{
			s = 0.0f; // can't select edges when verts are selected
		}

		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;
		COLOR4 color;
		if (modelEdges[i].selected)
		{
			color = (int)i == hoverEdge ? hoverSelectColor : selectColor;
		}
		else
		{
			color = (int)i == hoverEdge ? edgeHoverColor : edgeDimColor;
		}
		modelVertCubes[cubeIdx++] = cCube(min, max, color);
	}

	matmodel.loadIdentity();
	matmodel.translate(rend->renderOffset.x, rend->renderOffset.y, rend->renderOffset.z);
	colorShader->updateMatrixes();

	//modelVertBuff->uploaded = false;
	modelVertBuff->drawFull();
}

void Renderer::drawModelOrigin(int modelIdx)
{
	if (!modelOriginBuff || modelIdx < 0)
		return;

	glClear(GL_DEPTH_BUFFER_BIT);

	Bsp* map = SelectedMap;
	if (!map)
		return;

	BspRenderer* rend = map->getBspRender();

	if (!rend)
		return;

	vec3 localCameraOrigin = rend->localCameraOrigin;

	//BSPMODEL& modl = map->models[modelIdx];

	COLOR4 vertDimColor = { 0, 200, 0, 255 };
	COLOR4 vertHoverColor = { 128, 255, 128, 255 };
	COLOR4 selectColor = { 0, 128, 255, 255 };
	COLOR4 hoverSelectColor = { 96, 200, 255, 255 };

	if (modelUsesSharedStructures)
	{
		vertDimColor = { 32, 32, 32, 255 };
	}


	float s = (moveAxes.origin - localCameraOrigin).length() * vertExtentFactor;
	s *= 1.2f;
	vec3 ori = moveAxes.origin.flip();
	vec3 min = vec3(-s, -s, -s) + ori;
	vec3 max = vec3(s, s, s) + ori;

	COLOR4 color;
	if (originSelected)
	{
		color = originHovered ? hoverSelectColor : selectColor;
	}
	else
	{
		color = originHovered ? vertHoverColor : vertDimColor;
	}
	modelOriginCube = cCube(min, max, color);

	matmodel.loadIdentity();
	colorShader->updateMatrixes();
	modelOriginBuff->uploaded = false;
	modelOriginBuff->drawFull();
}

void Renderer::drawTransformAxes()
{
	if (SelectedMap && pickInfo.selectedEnts.size() == 1 && pickInfo.selectedEnts[0] >= 0 && transformMode == TRANSFORM_MODE_SCALE && transformTarget == TRANSFORM_OBJECT && !modelUsesSharedStructures && !invalidSolid)
	{
		if (SelectedMap->ents[pickInfo.selectedEnts[0]]->getBspModelIdx() > 0)
		{
			matmodel.loadIdentity();
			glDepthMask(GL_FALSE);
			glDepthFunc(GL_ALWAYS);
			updateDragAxes();
			vec3 ori = scaleAxes.origin;
			matmodel.translate(ori.x, ori.z, -ori.y);
			colorShader->updateMatrixes();
			glDisable(GL_CULL_FACE);
			scaleAxes.buffer->drawFull();
			glEnable(GL_CULL_FACE);
			glDepthMask(GL_TRUE);
			glDepthFunc(GL_LESS);
		}
	}
	if (SelectedMap && pickInfo.selectedEnts.size() > 0 && transformMode == TRANSFORM_MODE_MOVE)
	{
		if (transformTarget != TRANSFORM_VERTEX || (anyVertSelected || anyEdgeSelected))
		{
			matmodel.loadIdentity();
			glDepthMask(GL_FALSE);
			glDepthFunc(GL_ALWAYS);
			updateDragAxes();
			vec3 ori = moveAxes.origin;
			matmodel.translate(ori.x, ori.z, -ori.y);
			colorShader->updateMatrixes();
			glDisable(GL_CULL_FACE);
			moveAxes.buffer->drawFull();
			glEnable(GL_CULL_FACE);
			glDepthMask(GL_TRUE);
			glDepthFunc(GL_LESS);
		}
	}
}

void Renderer::drawEntConnections()
{
	if (entConnections && (g_render_flags & RENDER_ENT_CONNECTIONS))
	{
		matmodel.loadIdentity();
		colorShader->updateMatrixes();
		entConnections->drawFull();
	}
}

void Renderer::controls()
{
	if (blockMoving)
	{
		if (!anyCtrlPressed || !pressed[GLFW_KEY_A])
			blockMoving = false;
	}

	if (canControl && !blockMoving)
	{
		if (anyCtrlPressed && !oldPressed[GLFW_KEY_A] && pressed[GLFW_KEY_A]
			&& pickMode != PICK_OBJECT && pickInfo.selectedFaces.size() == 1)
		{
			Bsp* map = SelectedMap;
			if (map)
			{
				blockMoving = true;
				BSPFACE32& selface = map->faces[pickInfo.selectedFaces[0]];
				BSPTEXTUREINFO& seltexinfo = map->texinfos[selface.iTextureInfo];
				deselectFaces();
				for (int i = 0; i < map->faceCount; i++)
				{
					BSPFACE32& face = map->faces[i];
					BSPTEXTUREINFO& texinfo = map->texinfos[face.iTextureInfo];
					if (texinfo.iMiptex == seltexinfo.iMiptex)
					{
						map->getBspRender()->highlightFace(i, 1);
						pickInfo.selectedFaces.push_back(i);
					}
				}
			}
		}

		cameraOrigin += getMoveDir() * (float)(curTime - oldTime) * moveSpeed;

		moveGrabbedEnt();

		vertexEditControls();

		cameraContextMenus();

		cameraRotationControls();

		makeVectors(cameraAngles, cameraForward, cameraRight, cameraUp);

		cameraObjectHovering();

		cameraPickingControls();

		if (!gui->imgui_io->WantCaptureKeyboard)
		{
			shortcutControls();
			globalShortcutControls();
		}
	}
	else
	{
		if (oldControl && !blockMoving && curLeftMouse == GLFW_PRESS)
		{
			curLeftMouse = GLFW_RELEASE;
			oldLeftMouse = GLFW_PRESS;
			cameraPickingControls();
		}
	}

	oldScroll = g_scroll;
}

void Renderer::vertexEditControls()
{
	if (transformTarget == TRANSFORM_VERTEX)
	{
		anyEdgeSelected = false;
		anyVertSelected = false;

		for (size_t i = 0; i < modelVerts.size(); i++)
		{
			if (modelVerts[i].selected)
			{
				anyVertSelected = true;
				break;
			}
		}

		for (size_t i = 0; i < modelEdges.size(); i++)
		{
			if (modelEdges[i].selected)
			{
				anyEdgeSelected = true;
			}
		}

	}

	if (pressed[GLFW_KEY_F] && !oldPressed[GLFW_KEY_F])
	{
		if (!anyCtrlPressed)
		{
			splitModelFace();
		}
		else
		{
			gui->showEntityReport = true;
		}
	}
}

void Renderer::cameraPickingControls()
{
	static bool oldTransforming = false;
	Bsp* map = SelectedMap;
	auto entIdx = pickInfo.selectedEnts;

	if (curLeftMouse == GLFW_RELEASE && oldLeftMouse == GLFW_RELEASE)
	{
		last_face_idx = -1;
	}

	if (curLeftMouse == GLFW_PRESS || oldLeftMouse == GLFW_PRESS || (curLeftMouse == GLFW_PRESS && facePickTime > 0.0 && curTime - facePickTime > 0.05))
	{
		bool transforming = false;
		bool isMovingOrigin = transformMode == TRANSFORM_MODE_MOVE && transformTarget == TRANSFORM_ORIGIN;
		bool isTransformingValid = (!modelUsesSharedStructures || (transformMode == TRANSFORM_MODE_MOVE && transformTarget != TRANSFORM_VERTEX))
			|| (isTransformableSolid);
		bool isTransformingWorld = pickInfo.IsSelectedEnt(0) && transformTarget != TRANSFORM_OBJECT;

		if (pickMode == pick_modes::PICK_OBJECT && !movingEnt && !isTransformingWorld && entIdx.size() && (isTransformingValid || isMovingOrigin))
		{
			transforming = transformAxisControls();
		}
		else
		{
			saveTranformResult = false;
		}

		bool anyHover = hoverVert != -1 || hoverEdge != -1;
		if (transformTarget == TRANSFORM_VERTEX && isTransformableSolid && anyHover)
		{
			if (oldLeftMouse != GLFW_PRESS)
			{
				if (!anyCtrlPressed)
				{
					for (size_t i = 0; i < modelEdges.size(); i++)
					{
						modelEdges[i].selected = false;
					}
					for (size_t i = 0; i < modelVerts.size(); i++)
					{
						modelVerts[i].selected = false;
					}
					anyVertSelected = false;
					anyEdgeSelected = false;
				}

				if (hoverVert != -1 && !anyEdgeSelected)
				{
					modelVerts[hoverVert].selected = !modelVerts[hoverVert].selected;
					anyVertSelected = modelVerts[hoverVert].selected;
				}
				else if (hoverEdge != -1 && !(anyVertSelected && !anyEdgeSelected))
				{
					modelEdges[hoverEdge].selected = !modelEdges[hoverEdge].selected;
					for (int i = 0; i < 2; i++)
					{
						TransformVert& vert = modelVerts[modelEdges[hoverEdge].verts[i]];
						vert.selected = modelEdges[hoverEdge].selected;
					}
					anyEdgeSelected = modelEdges[hoverEdge].selected;
				}

				vertPickCount++;
			}

			transforming = true;
		}

		if (transformTarget == TRANSFORM_ORIGIN && originHovered)
		{
			if (oldLeftMouse != GLFW_PRESS)
			{
				originSelected = !originSelected;
			}

			transforming = true;
		}

		// object picking
		if (!transforming && (oldLeftMouse == GLFW_RELEASE || (facePickTime > 0.0 && curTime - facePickTime > 0.05)))
		{
			facePickTime = -1.0;
		/*	if (map && entIdx.size() && oldLeftMouse == GLFW_RELEASE)
			{
				applyTransform(map);
			}*/
			if (hoverAxis == -1)
				pickObject();
		}
		oldTransforming = transforming;
	}
	else
	{ // left mouse not pressed
		pickClickHeld = false;
	}
	if (hoverAxis != -1 && curLeftMouse == GLFW_RELEASE && oldLeftMouse == GLFW_PRESS)
	{
		applyTransform(map, true);
	}
}

void Renderer::revertInvalidSolid(Bsp* map, int modelIdx)
{
	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		modelVerts[i].pos = modelVerts[i].startPos = modelVerts[i].undoPos;
		if (modelVerts[i].ptr)
			*modelVerts[i].ptr = modelVerts[i].pos;
	}
	for (size_t i = 0; i < modelFaceVerts.size(); i++)
	{
		modelFaceVerts[i].pos = modelFaceVerts[i].startPos = modelFaceVerts[i].undoPos;
		if (modelFaceVerts[i].ptr)
			*modelFaceVerts[i].ptr = modelFaceVerts[i].pos;
	}
	if (map && modelIdx >= 0)
	{
		map->vertex_manipulation_sync(modelIdx, modelVerts, false);
		BSPMODEL& model = map->models[modelIdx];
		map->get_model_vertex_bounds(modelIdx, model.nMins, model.nMaxs);
		map->getBspRender()->refreshModel(modelIdx);
	}
	gui->reloadLimits();
}

void Renderer::applyTransform(Bsp* map, bool forceUpdate)
{
	bool transformingVerts = transformTarget == TRANSFORM_VERTEX && transformMode == TRANSFORM_MODE_MOVE;
	bool scalingObject = transformTarget == TRANSFORM_OBJECT && transformMode == TRANSFORM_MODE_SCALE;
	//bool movingOrigin = transformTarget == TRANSFORM_ORIGIN && transformMode == TRANSFORM_MODE_MOVE;

	bool anyVertsChanged = false;
	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		if (modelVerts[i].pos != modelVerts[i].startPos || modelVerts[i].pos != modelVerts[i].undoPos)
		{
			anyVertsChanged = true;
		}
	}

	for (size_t i = 0; i < modelFaceVerts.size(); i++)
	{
		if (modelFaceVerts[i].pos != modelFaceVerts[i].startPos || modelFaceVerts[i].pos != modelFaceVerts[i].undoPos)
		{
			anyVertsChanged = true;
		}
	}

	if ((anyVertsChanged && (transformingVerts || scalingObject)) || forceUpdate)
	{
		for (size_t i = 0; i < modelVerts.size(); i++)
		{
			/*if (modelVerts[i].ptr)
				modelVerts[i].pos = *modelVerts[i].ptr;*/
			modelVerts[i].startPos = modelVerts[i].pos;
			if (!invalidSolid)
			{
				modelVerts[i].undoPos = modelVerts[i].pos;
			}
		}
		for (size_t i = 0; i < modelFaceVerts.size(); i++)
		{
			/*if (modelFaceVerts[i].ptr)
				modelFaceVerts[i].pos = *modelFaceVerts[i].ptr;*/
			modelFaceVerts[i].startPos = modelFaceVerts[i].pos;
			if (!invalidSolid)
			{
				modelFaceVerts[i].undoPos = modelFaceVerts[i].pos;
			}
		}

		if (scalingObject && map)
		{
			for (size_t i = 0; i < scaleTexinfos.size(); i++)
			{
				BSPTEXTUREINFO& info = map->texinfos[scaleTexinfos[i].texinfoIdx];
				scaleTexinfos[i].oldShiftS = info.shiftS;
				scaleTexinfos[i].oldShiftT = info.shiftT;
				scaleTexinfos[i].oldS = info.vS;
				scaleTexinfos[i].oldT = info.vT;
			}
		}

		if (modelTransform >= 0)
		{
			BSPMODEL& model = map->models[modelTransform];
			map->get_model_vertex_bounds(modelTransform, model.nMins, model.nMaxs);
		}

		gui->reloadLimits();
	}
}

void Renderer::cameraRotationControls()
{
	// camera rotation
	if (curRightMouse == GLFW_PRESS)
	{
		if (!cameraIsRotating)
		{
			lastMousePos = mousePos;
			cameraIsRotating = true;
			totalMouseDrag = vec2();
		}
		else
		{
			vec2 drag = mousePos - lastMousePos;
			cameraAngles.z += drag.x * rotationSpeed * 0.1f;
			cameraAngles.x += drag.y * rotationSpeed * 0.1f;

			totalMouseDrag += vec2(abs(drag.x), abs(drag.y));

			cameraAngles.x = clamp(cameraAngles.x, -90.0f, 90.0f);

			if (cameraAngles.z > 180.0f)
			{
				cameraAngles.z -= 360.0f;
			}
			else if (cameraAngles.z < -180.0f)
			{
				cameraAngles.z += 360.0f;
			}

			cameraAngles.y = 0.0f;
			lastMousePos = mousePos;
		}

		ImGui::SetWindowFocus(NULL);
		ImGui::ClearActiveID();
	}
	else
	{
		cameraIsRotating = false;
		totalMouseDrag = vec2();
	}
}

void Renderer::cameraObjectHovering()
{
	Bsp* map = SelectedMap;
	if (!map || (modelUsesSharedStructures && transformTarget != TRANSFORM_OBJECT && transformTarget != TRANSFORM_ORIGIN)
		|| anyPopupOpened)
		return;

	if (pickMode != PICK_OBJECT)
	{
		hoverAxis = -1;
		originHovered = false;
		return;
	}

	int modelIdx = -1;
	auto entIdx = pickInfo.selectedEnts;
	if (entIdx.size())
	{
		modelIdx = map->ents[entIdx[0]]->getBspModelIdx();
	}

	BspRenderer* rend = map->getBspRender();
	if (!rend)
		return;

	// axis handle hovering
	TransformAxes& activeAxes = *(transformMode == TRANSFORM_MODE_SCALE ? &scaleAxes : &moveAxes);


	vec3 mapOffset = rend->mapOffset;
	vec3 localCameraOrigin = rend->localCameraOrigin;

	if (transformTarget == TRANSFORM_VERTEX && entIdx.size())
	{
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		PickInfo vertPick = PickInfo();
		vertPick.bestDist = FLT_MAX_COORD;

		Entity* ent = map->ents[entIdx[0]];
		vec3 entOrigin = ent->origin;

		hoverEdge = -1;
		if (!(anyVertSelected && !anyEdgeSelected))
		{
			for (size_t i = 0; i < modelEdges.size(); i++)
			{
				vec3 ori = getEdgeControlPoint(modelVerts, modelEdges[i]) + entOrigin + mapOffset;
				float s = (ori - localCameraOrigin).length() * vertExtentFactor * 2.0f;
				vec3 min = vec3(-s, -s, -s) + ori;
				vec3 max = vec3(s, s, s) + ori;
				if (pickAABB(pickStart, pickDir, min, max, vertPick.bestDist))
				{
					hoverEdge = (int)i;
				}
			}
		}

		hoverVert = -1;
		if (!anyEdgeSelected)
		{
			for (size_t i = 0; i < modelVerts.size(); i++)
			{
				vec3 ori = entOrigin + modelVerts[i].pos + mapOffset;
				float s = (ori - localCameraOrigin).length() * vertExtentFactor * 2.0f;
				vec3 min = vec3(-s, -s, -s) + ori;
				vec3 max = vec3(s, s, s) + ori;
				if (pickAABB(pickStart, pickDir, min, max, vertPick.bestDist))
				{
					hoverVert = (int)i;
				}
			}
		}
	}

	if (hoverEdge != -1 || hoverVert != -1)
		return;

	PickInfo vertPick = PickInfo();
	vertPick.bestDist = FLT_MAX_COORD;

	originHovered = false;

	if (transformTarget == TRANSFORM_ORIGIN)
	{
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);

		float s = (activeAxes.origin - localCameraOrigin).length() * vertExtentFactor;
		s *= 1.2f;
		vec3 ori = activeAxes.origin.flip();
		vec3 min = vec3(-s, -s, -s) + ori;
		vec3 max = vec3(s, s, s) + ori;

		originHovered = pickAABB(pickStart, pickDir, min, max, vertPick.bestDist);
	}

	if ((transformTarget == TRANSFORM_VERTEX && transformMode == TRANSFORM_MODE_SCALE))
		return; // 3D scaling disabled in vertex edit mode

	if (curLeftMouse == GLFW_RELEASE)
	{
		hoverAxis = -1;
		if (showDragAxes && !movingEnt && hoverVert == -1 && hoverEdge == -1)
		{
			vec3 pickStart, pickDir;
			getPickRay(pickStart, pickDir);
			PickInfo axisPick = PickInfo();
			axisPick.bestDist = FLT_MAX_COORD;

			if (map->getBspRender())
			{
				vec3 origin = activeAxes.origin;

				int axisChecks = transformMode == TRANSFORM_MODE_SCALE ? activeAxes.numAxes : 3;
				for (int i = 0; i < axisChecks; i++)
				{
					if (pickAABB(pickStart, pickDir, origin + activeAxes.mins[i], origin + activeAxes.maxs[i], axisPick.bestDist))
					{
						hoverAxis = i;
					}
				}

				// center cube gets priority for selection (hard to select from some angles otherwise)
				// but origin has more priority!
				if (transformMode == TRANSFORM_MODE_MOVE)
				{
					if (pickAABB(pickStart, pickDir, origin + activeAxes.mins[3], origin + activeAxes.maxs[3], vertPick.bestDist))
					{
						hoverAxis = 3;
						originHovered = true;
					}
				}
			}
		}
	}
}

void Renderer::cameraContextMenus()
{
	// context menus
	bool wasTurning = cameraIsRotating && totalMouseDrag.length() >= 1.0f;
	if (curRightMouse == GLFW_RELEASE && oldRightMouse == GLFW_PRESS && !wasTurning)
	{
		if (pickInfo.selectedEnts.size())
		{
			gui->openContextMenu(false);
		}
		else
		{
			gui->openContextMenu(true);
		}
	}
}

void Renderer::moveGrabbedEnt()
{
	auto entIdx = pickInfo.selectedEnts;
	if (movingEnt && entIdx.size())
	{
		if (g_scroll != oldScroll)
		{
			float moveScale = pressed[GLFW_KEY_LEFT_SHIFT] ? 4.0f : 2.0f;
			if (pressed[GLFW_KEY_LEFT_CONTROL])
				moveScale = 1.0f;
			if (g_scroll < oldScroll)
				moveScale *= -1.0f;

			grabDist += 16.0f * moveScale;
		}

		Bsp* map = SelectedMap;
		vec3 mapOffset = map->getBspRender()->mapOffset;
		vec3 delta = ((cameraOrigin - mapOffset) + cameraForward * grabDist) - grabStartOrigin;

		for (auto& i : entIdx)
		{
			Entity* ent = map->ents[i];

			vec3 tmpOrigin = grabStartEntOrigin;
			vec3 offset = getEntOffset(map, ent);
			vec3 newOrigin = (tmpOrigin + delta) - offset;
			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOrigin) : newOrigin;

			ent->setOrAddKeyvalue("origin", rounded.toKeyvalueString());
			map->getBspRender()->refreshEnt((int)i);
			updateEntConnectionPositions();
		}
	}
	else
	{
		ungrabEnt();
	}
}

void Renderer::shortcutControls()
{
	bool anyEnterPressed = (pressed[GLFW_KEY_ENTER] && !oldPressed[GLFW_KEY_ENTER]) ||
		(pressed[GLFW_KEY_KP_ENTER] && !oldPressed[GLFW_KEY_KP_ENTER]);

	if (pickMode == PICK_OBJECT)
	{
		if (pressed[GLFW_KEY_G] == GLFW_PRESS && oldPressed[GLFW_KEY_G] != GLFW_PRESS && anyAltPressed)
		{
			if (!movingEnt)
				grabEnt();
			else
			{
				ungrabEnt();
			}
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_C] && !oldPressed[GLFW_KEY_C])
		{
			copyEnt();
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_X] && !oldPressed[GLFW_KEY_X])
		{
			cutEnt();
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_V] && !oldPressed[GLFW_KEY_V])
		{
			pasteEnt(false);
		}
		if (pressed[GLFW_KEY_DELETE] && !oldPressed[GLFW_KEY_DELETE])
		{
			deleteEnts();
		}
	}
	else if (pickMode != PICK_OBJECT)
	{
		if (anyCtrlPressed && pressed[GLFW_KEY_C] && !oldPressed[GLFW_KEY_C])
		{
			gui->copyTexture();
		}
		if (anyCtrlPressed && pressed[GLFW_KEY_V] && !oldPressed[GLFW_KEY_V])
		{
			gui->pasteTexture();
		}
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_M] && !oldPressed[GLFW_KEY_M])
	{
		gui->showTransformWidget = !gui->showTransformWidget;
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_G] && !oldPressed[GLFW_KEY_G])
	{
		gui->showGOTOWidget = !gui->showGOTOWidget;
		gui->showGOTOWidget_update = true;
	}
	if (anyAltPressed && anyEnterPressed)
	{
		gui->showKeyvalueWidget = !gui->showKeyvalueWidget;
	}
}

void Renderer::globalShortcutControls()
{
	Bsp* map = SelectedMap;
	if (!map)
		return;
	if (anyCtrlPressed && pressed[GLFW_KEY_Z] && !oldPressed[GLFW_KEY_Z])
	{
		map->getBspRender()->undo();
	}
	if (anyCtrlPressed && pressed[GLFW_KEY_Y] && !oldPressed[GLFW_KEY_Y])
	{
		map->getBspRender()->redo();
	}
}

void Renderer::pickObject()
{
	Bsp* map = SelectedMap;
	auto entIdx = pickInfo.selectedEnts;
	if (!map)
		return;
	bool pointEntWasSelected = entIdx.size();

	Entity* ent = NULL;
	if (pointEntWasSelected)
	{
		ent = SelectedMap->ents[entIdx[0]];
		pointEntWasSelected = ent && !ent->isBspModel();
	}
	vec3 pickStart, pickDir;
	getPickRay(pickStart, pickDir);

	Bsp* oldmap = map;

	PickInfo tmpPickInfo = PickInfo();
	tmpPickInfo.bestDist = FLT_MAX_COORD;

	map->getBspRender()->pickPoly(pickStart, pickDir, clipnodeRenderHull, tmpPickInfo, &map);

	for (size_t i = 0; i < mapRenderers.size(); i++)
	{
		if (map == mapRenderers[i]->map->parentMap)
		{
			mapRenderers[i]->pickPoly(pickStart, pickDir, clipnodeRenderHull, tmpPickInfo, &map);
		}
	}

	pickInfo.bestDist = tmpPickInfo.bestDist;

	if (map != oldmap && pickMode != PICK_OBJECT)
	{
		for (auto& idx : pickInfo.selectedFaces)
		{
			map->getBspRender()->highlightFace(idx, 0);
		}

		if (tmpPickInfo.selectedFaces.size() == 1)
		{
			map->getBspRender()->highlightFace(tmpPickInfo.selectedFaces[0], 0);
		}
		map->selectModelEnt();
		pickCount++;
		return;
	}

	auto tmpPickEnt = tmpPickInfo.selectedEnts;

	if (movingEnt && entIdx != tmpPickEnt)
	{
		ungrabEnt();
	}

	if (pickMode != PICK_OBJECT)
	{
		gui->showLightmapEditorUpdate = true;

		if (!anyCtrlPressed)
		{
			for (auto idx : pickInfo.selectedFaces)
			{
				map->getBspRender()->highlightFace(idx, 0);
			}
			pickInfo.selectedFaces.clear();
		}
		else
		{
			facePickTime = curTime;
		}

		if (tmpPickInfo.selectedFaces.size() > 0)
		{
			if ((int)tmpPickInfo.selectedFaces[0] != last_face_idx)
			{
				last_face_idx = -1;
				if (std::find(pickInfo.selectedFaces.begin(), pickInfo.selectedFaces.end(), tmpPickInfo.selectedFaces[0]) == pickInfo.selectedFaces.end())
				{
					map->getBspRender()->highlightFace(tmpPickInfo.selectedFaces[0], 1);
					pickInfo.selectedFaces.push_back(tmpPickInfo.selectedFaces[0]);
				}
				else if (curLeftMouse == GLFW_PRESS && oldLeftMouse == GLFW_RELEASE)
				{
					last_face_idx = (int)tmpPickInfo.selectedFaces[0];
					map->getBspRender()->highlightFace(last_face_idx, 0);
					pickInfo.selectedFaces.erase(std::find(pickInfo.selectedFaces.begin(), pickInfo.selectedFaces.end(), tmpPickInfo.selectedFaces[0]));
				}
				pickCount++;
			}
		}
	}
	else
	{
		for (auto idx : pickInfo.selectedFaces)
		{
			map->getBspRender()->highlightFace(idx, 0);
		}

		if (tmpPickInfo.selectedFaces.size() == 1)
		{
			map->getBspRender()->highlightFace(tmpPickInfo.selectedFaces[0], 0);
		}

		pickInfo.selectedFaces.clear();
		tmpPickInfo.selectedFaces.clear();

		updateModelVerts();

		if (pointEntWasSelected)
		{
			for (size_t i = 0; i < mapRenderers.size(); i++)
			{
				mapRenderers[i]->refreshPointEnt((int)entIdx[0]);
			}
		}

		pickClickHeld = true;

		updateEntConnections();

		if (curLeftMouse == GLFW_PRESS && oldLeftMouse == GLFW_RELEASE)
		{
			pickCount++;
			if (tmpPickEnt.size())
				selectEnt(SelectedMap, tmpPickEnt[0], anyCtrlPressed && tmpPickEnt[0] != 0);
			else if (!anyCtrlPressed)
			{
				pickInfo.selectedEnts.clear();
				pickCount++;
			}
		}
	}
}

bool Renderer::transformAxisControls()
{
	TransformAxes& activeAxes = *(transformMode == TRANSFORM_MODE_SCALE ? &scaleAxes : &moveAxes);
	Bsp* map = SelectedMap;
	auto entIdx = pickInfo.selectedEnts;

	bool transformingVerts = transformTarget == TRANSFORM_VERTEX && transformMode == TRANSFORM_MODE_MOVE;
	bool scalingObject = transformTarget == TRANSFORM_OBJECT && transformMode == TRANSFORM_MODE_SCALE;
	bool movingOrigin = (transformTarget == TRANSFORM_ORIGIN && transformMode == TRANSFORM_MODE_MOVE)
		|| (transformTarget == TRANSFORM_OBJECT && transformMode == TRANSFORM_MODE_MOVE);

	bool canTransform = transformingVerts || scalingObject || movingOrigin;

	if (!isTransformableSolid || pickClickHeld || entIdx.empty() || !map || !canTransform)
	{
		return false;
	}


	Entity* ent = map->ents[entIdx[0]];
	int modelIdx = ent->getBspModelIdx();
	// axis handle dragging
	if (showDragAxes && !movingEnt && hoverAxis != -1 &&
		curLeftMouse == GLFW_PRESS && oldLeftMouse == GLFW_RELEASE)
	{
		axisDragEntOriginStart = getEntOrigin(map, ent);
		axisDragStart = getAxisDragPoint(axisDragEntOriginStart);
	}

	bool retval = false;

	if (showDragAxes && !movingEnt && hoverAxis >= 0)
	{
		activeAxes.model[hoverAxis].setColor(activeAxes.hoverColor[hoverAxis]);

		vec3 dragPoint = getAxisDragPoint(axisDragEntOriginStart);

		if (gridSnappingEnabled)
		{
			dragPoint = snapToGrid(dragPoint);
		}

		vec3 delta = dragPoint - axisDragStart;
		if (delta.IsZero())
			retval = false;
		else
		{
			if (!modelVerts.size())
				updateModelVerts();

			float moveScale = pressed[GLFW_KEY_LEFT_SHIFT] ? 2.0f : 1.0f;
			if (pressed[GLFW_KEY_LEFT_CONTROL] == GLFW_PRESS)
				moveScale = 0.5f;

			float maxDragDist = 8192; // don't throw ents out to infinity
			for (int i = 0; i < 3; i++)
			{
				if (i != hoverAxis % 3)
					((float*)&delta)[i] = 0.0f;
				else
					((float*)&delta)[i] = clamp(((float*)&delta)[i] * moveScale, -maxDragDist, maxDragDist);
			}
			if (delta.IsZero())
				retval = false;
			else
			{
				axisDragStart = dragPoint;
				saveTranformResult = true;

				if (transformMode == TRANSFORM_MODE_MOVE)
				{
					if (transformTarget == TRANSFORM_VERTEX && anyVertSelected)
					{
						moveSelectedVerts(delta);
						vertPickCount++;
					}
					else if (transformTarget == TRANSFORM_OBJECT)
					{
						if (moveOrigin || modelIdx < 0)
						{
							for (size_t tmpentIdx : pickInfo.selectedEnts)
							{
								Entity* tmpEnt = map->ents[tmpentIdx];
								if (!tmpEnt)
									continue;

								vec3 ent_offset = getEntOffset(map, tmpEnt);

								vec3 offset = tmpEnt->origin + delta + ent_offset;

								vec3 rounded = gridSnappingEnabled ? snapToGrid(offset) : offset;

								tmpEnt->setOrAddKeyvalue("origin", (rounded - ent_offset).toKeyvalueString());

								map->getBspRender()->refreshEnt((int)tmpentIdx);

								updateEntConnectionPositions();
							}
						}
						else
						{
							vertPickCount++;
							map->move(delta, modelIdx, true, false, false);
							map->getBspRender()->refreshEnt((int)entIdx[0]);
							map->getBspRender()->refreshModel(modelIdx);
							updateEntConnectionPositions();
						}
					}
					else if (transformTarget == TRANSFORM_ORIGIN)
					{
						for (size_t i = 0; i < pickInfo.selectedEnts.size(); i++)
						{
							Entity* tmpent = map->ents[pickInfo.selectedEnts[i]];
							int tmpmodelidx = tmpent->getBspModelIdx();

							if (tmpent->getBspModelIdx() >= 0)
							{
								vec3 neworigin = map->models[tmpmodelidx].vOrigin + delta;
								map->models[tmpmodelidx].vOrigin = neworigin;
								//map->getBspRender()->refreshModel(tmpent->getBspModelIdx());
								map->getBspRender()->refreshEnt((int)pickInfo.selectedEnts[i]);
							}

							pickCount++;
							vertPickCount++;
						}
					}
				}
				else
				{
					if (ent->isBspModel())
					{
						vec3 scaleDirs[6]{
							vec3(1.0f, 0.0f, 0.0f),
							vec3(0.0f, 1.0f, 0.0f),
							vec3(0.0f, 0.0f, 1.0f),
							vec3(-1.0f, 0.0f, 0.0f),
							vec3(0.0f, -1.0f, 0.0f),
							vec3(0.0f, 0.0f, -1.0f),
						};
						scaleSelectedObject(delta, scaleDirs[hoverAxis]);
						map->getBspRender()->refreshModel(modelIdx);
						vertPickCount++;
					}
				}

				retval = true;
			}
		}
	}

	if (curLeftMouse != GLFW_PRESS && oldLeftMouse != GLFW_RELEASE)
	{
		if (saveTranformResult)
		{
			if (transformMode == TRANSFORM_MODE_MOVE)
			{
				if (transformTarget == TRANSFORM_VERTEX)
				{
					saveTranformResult = false;
					map->regenerate_clipnodes(modelIdx, -1);
					if (invalidSolid)
						revertInvalidSolid(map, modelIdx);
					else
					{
						map->resize_all_lightmaps();
						map->getBspRender()->pushModelUndoState("Move verts", EDIT_MODEL_LUMPS);
					}
					map->getBspRender()->refreshModel(modelIdx);
					map->getBspRender()->refreshModelClipnodes(modelIdx);
					applyTransform(map, true);
				}
				else if (transformTarget == TRANSFORM_OBJECT)
				{
					if (moveOrigin || modelIdx < 0)
					{
						for (size_t tmpentIdx : pickInfo.selectedEnts)
						{
							saveTranformResult = false;
							map->getBspRender()->pushEntityUndoState("Move Entity", (int)tmpentIdx);
						}
					}
					else
					{
						saveTranformResult = false;
						if (invalidSolid)
							revertInvalidSolid(map, modelIdx);
						else
						{
							map->resize_all_lightmaps();
							map->getBspRender()->pushModelUndoState("Move Model", EDIT_MODEL_LUMPS | FL_ENTITIES);
						}
						map->getBspRender()->refreshEnt((int)entIdx[0]);
						map->getBspRender()->refreshModel(modelIdx);
						map->getBspRender()->refreshModelClipnodes(modelIdx);
						applyTransform(map, true);
					}
				}
				else if (transformTarget == TRANSFORM_ORIGIN)
				{
					saveTranformResult = false;
					bool updateModels = false;

					for (size_t i = 0; i < pickInfo.selectedEnts.size(); i++)
					{
						Entity* tmpent = map->ents[pickInfo.selectedEnts[i]];
						int tmpmodelidx = tmpent->getBspModelIdx();

						if (tmpent->getBspModelIdx() >= 0)
						{
							vec3 neworigin = gridSnappingEnabled ? snapToGrid(map->models[tmpmodelidx].vOrigin) : map->models[tmpmodelidx].vOrigin;
							map->models[tmpmodelidx].vOrigin = neworigin;
							map->getBspRender()->refreshModel(tmpent->getBspModelIdx());
							map->getBspRender()->refreshEnt((int)pickInfo.selectedEnts[i]);
							updateModels = true;
						}

						pickCount++;
						vertPickCount++;
					}

					if (updateModels)
					{
						map->resize_all_lightmaps();
						map->getBspRender()->pushModelUndoState("Move model [vOrigin]", EDIT_MODEL_LUMPS | FL_ENTITIES);
					}
				}
			}
			else
			{
				if (ent->isBspModel())
				{
					saveTranformResult = false;
					applyTransform(map, true);
					map->regenerate_clipnodes(modelIdx, -1);
					if (invalidSolid)
					{
						revertInvalidSolid(map, modelIdx);
					}
					else
					{
						map->getBspRender()->pushModelUndoState("Scale Model", EDIT_MODEL_LUMPS);
						map->resize_all_lightmaps();
					}
					map->getBspRender()->refreshModel(modelIdx);
					map->getBspRender()->refreshModelClipnodes(modelIdx);
				}
			}
		}
	}

	return retval;
}

vec3 Renderer::getMoveDir()
{
	mat4x4 rotMat;
	rotMat.loadIdentity();
	rotMat.rotateX(HL_PI * cameraAngles.x / 180.0f);
	rotMat.rotateZ(HL_PI * cameraAngles.z / 180.0f);

	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);


	vec3 wishdir{};
	if (pressed[GLFW_KEY_A])
	{
		wishdir -= right;
	}
	if (pressed[GLFW_KEY_D])
	{
		wishdir += right;
	}
	if (pressed[GLFW_KEY_W])
	{
		wishdir += forward;
	}
	if (pressed[GLFW_KEY_S])
	{
		wishdir -= forward;
	}

	if (anyShiftPressed)
		wishdir *= 4.0f;
	if (anyCtrlPressed)
		wishdir *= 0.1f;
	return wishdir;
}

void Renderer::getPickRay(vec3& start, vec3& pickDir)
{
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);

	// invert ypos
	ypos = windowHeight - ypos;

	// translate mouse coordinates so that the origin lies in the center and is a scaler from +/-1.0
	float mouseX = (((float)xpos / (float)windowWidth) * 2.0f) - 1.0f;
	float mouseY = (((float)ypos / (float)windowHeight) * 2.0f) - 1.0f;

	// http://schabby.de/picking-opengl-ray-tracing/
	vec3 forward, right, up;
	makeVectors(cameraAngles, forward, right, up);

	vec3 tview = forward.normalize(1.0f);
	vec3 h = crossProduct(tview, up).normalize(1.0f); // 3D float std::vector
	vec3 v = crossProduct(h, tview).normalize(1.0f); // 3D float std::vector

	// convert fovy to radians 
	float rad = fov * (HL_PI / 180.0f);
	float vLength = tan(rad / 2.0f) * zNear;
	float hLength = vLength * (windowWidth / (float)windowHeight);

	v *= vLength;
	h *= hLength;

	// linear combination to compute intersection of picking ray with view port plane
	start = cameraOrigin + tview * zNear + h * mouseX + v * mouseY;

	// compute direction of picking ray by subtracting intersection point with camera position
	pickDir = (start - cameraOrigin).normalize(1.0f);
}

Bsp* Renderer::getSelectedMap()
{
	// auto select if one map
	if (!SelectedMap && mapRenderers.size() == 1)
	{
		SelectedMap = mapRenderers[0]->map;
	}

	// TEMP DEBUG FOR CRASH DETECT
	if (SelectedMap != NULL && !mapRenderers.size())
	{
		if (g_verbose)
		{
			print_log(PRINT_RED, "CRITICAL ERROR! BAD MAP POINTER!!\n");
		}

		SelectedMap = NULL;
	}

	return SelectedMap;
}

int Renderer::getSelectedMapId()
{
	for (size_t i = 0; i < mapRenderers.size(); i++)
	{
		BspRenderer* s = mapRenderers[i];
		if (s->map && s->map == getSelectedMap())
		{
			return (int)i;
		}
	}
	return -1;
}

void Renderer::selectMapId(int id)
{
	for (size_t i = 0; i < mapRenderers.size(); i++)
	{
		BspRenderer* s = mapRenderers[i];
		if (s->map && (int)i == id)
		{
			SelectedMap = s->map;
			return;
		}
	}
	SelectedMap = NULL;
	SelectedMapChanged = true;
}

void Renderer::selectMap(Bsp* map)
{
	SelectedMap = map;
	SelectedMapChanged = true;
}

void Renderer::deselectMap()
{
	SelectedMap = NULL;
	SelectedMapChanged = true;
}

void Renderer::clearSelection()
{
	pickInfo = PickInfo();
}

BspRenderer* Renderer::getMapContainingCamera()
{
	for (size_t i = 0; i < mapRenderers.size(); i++)
	{
		Bsp* map = mapRenderers[i]->map;

		vec3 mins, maxs;
		map->get_bounding_box(mins, maxs);

		if (cameraOrigin.x > mins.x && cameraOrigin.y > mins.y && cameraOrigin.z > mins.z &&
			cameraOrigin.x < maxs.x && cameraOrigin.y < maxs.y && cameraOrigin.z < maxs.z)
		{
			return map->getBspRender();
		}
	}

	return NULL;
}

void Renderer::setupView()
{
	glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

	glViewport(0, 0, windowWidth, windowHeight);

	projection.perspective(fov, (float)windowWidth / (float)windowHeight, zNear, zFar);

	matview.loadIdentity();
	matview.rotateX(cameraAngles.x * HL_PI / 180.0f);
	matview.rotateY(cameraAngles.z * HL_PI / 180.0f);
	matview.translate(-cameraOrigin.x, -cameraOrigin.z, cameraOrigin.y);

	if (g_settings.save_cam)
	{
		if (g_app->SelectedMap)
		{
			g_app->SelectedMap->save_cam_pos = cameraOrigin;
			g_app->SelectedMap->save_cam_angles = cameraAngles;
		}
	}
}

void Renderer::reloadBspModels()
{
	isModelsReloading = true;

	if (!mapRenderers.size())
	{
		isModelsReloading = false;
		return;
	}

	size_t modelcount = 0;

	for (size_t i = 0; i < mapRenderers.size(); i++)
	{
		if (mapRenderers[i]->map->is_bsp_model)
		{
			modelcount++;
		}
	}

	if (modelcount == mapRenderers.size())
	{
		isModelsReloading = false;
		return;
	}

	std::vector<BspRenderer*> sorted_renders;

	for (size_t i = 0; i < mapRenderers.size(); i++)
	{
		if (!mapRenderers[i]->map->is_bsp_model)
		{
			sorted_renders.push_back(mapRenderers[i]);
		}
		else
		{
			delete mapRenderers[i];
			mapRenderers[i] = NULL;
		}
	}

	mapRenderers = sorted_renders;

	for (auto bsprend : sorted_renders)
	{
		if (bsprend)
		{
			for (auto const& entity : bsprend->map->ents)
			{
				if (entity->hasKey("model"))
				{
					std::string modelPath = entity->keyvalues["model"];
					if (toLowerCase(modelPath).ends_with(".bsp"))
					{
						std::string newBspPath;
						if (FindPathInAssets(bsprend->map, modelPath, newBspPath))
						{
							Bsp* tmpBsp = new Bsp(newBspPath);
							tmpBsp->is_bsp_model = true;
							tmpBsp->parentMap = bsprend->map;
							if (tmpBsp->bsp_valid)
							{
								BspRenderer* mapRenderer = new BspRenderer(tmpBsp);
								mapRenderers.push_back(mapRenderer);
							}
						}
						else
						{
							print_log(get_localized_string(LANG_0911), modelPath);
							FindPathInAssets(bsprend->map, modelPath, newBspPath, true);
						}
					}
				}
			}
		}
	}

	isModelsReloading = false;
}

void Renderer::addMap(Bsp* map)
{
	if (!map->bsp_valid)
	{
		print_log(get_localized_string(LANG_0912));
		return;
	}

	if (!map->is_bsp_model)
	{
		deselectObject();
		clearSelection();
		/*
		* TODO: save camera pos
		*/
	}

	BspRenderer* mapRenderer = new BspRenderer(map);

	mapRenderers.push_back(mapRenderer);

	gui->checkValidHulls();

	// Pick default map
	if (!getSelectedMap())
	{
		clearSelection();
		selectMap(map);
		if (map->ents.size())
			pickInfo.SetSelectedEnt(0);
	}
}

void Renderer::drawLine(vec3& start, vec3& end, COLOR4 color)
{
	line_verts[0].pos = start.flip();
	line_verts[0].c = color;

	line_verts[1].pos = end.flip();
	line_verts[1].c = color;

	lineBuf->uploaded = false;
	lineBuf->drawFull();
}

void Renderer::drawPlane(BSPPLANE& plane, COLOR4 color, vec3 offset)
{
	vec3 ori = plane.vNormal * plane.fDist;
	vec3 crossDir = abs(plane.vNormal.z) > 0.9f ? vec3(1.0f, 0.0f, 0.0f) : vec3(0.0f, 0.0f, 1.0f);
	vec3 right = crossProduct(plane.vNormal, crossDir);
	vec3 up = crossProduct(right, plane.vNormal);

	float s = 100.0f;

	vec3 topLeft = vec3(ori + right * -s + up * s).flip();
	vec3 topRight = vec3(ori + right * s + up * s).flip();
	vec3 bottomLeft = vec3(ori + right * -s + up * -s).flip();
	vec3 bottomRight = vec3(ori + right * s + up * -s).flip();

	cVert topLeftVert(topLeft, color);
	cVert topRightVert(topRight, color);
	cVert bottomLeftVert(bottomLeft, color);
	cVert bottomRightVert(bottomRight, color);

	plane_verts->v1 = bottomRightVert;
	plane_verts->v2 = bottomLeftVert;
	plane_verts->v3 = topLeftVert;
	plane_verts->v4 = topRightVert;

	planeBuf->uploaded = false;
	planeBuf->drawFull();
}

void Renderer::drawClipnodes(Bsp* map, int iNode, int& currentPlane, int activePlane, vec3 offset)
{
	if (iNode < 0)
		return;
	BSPCLIPNODE32& node = map->clipnodes[iNode];

	if (currentPlane == activePlane)
		drawPlane(map->planes[node.iPlane], { 255, 255, 255, 255 }, offset);
	currentPlane++;

	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			drawClipnodes(map, node.iChildren[i], currentPlane, activePlane);
		}
	}
}

void Renderer::drawNodes(Bsp* map, int iNode, int& currentPlane, int activePlane, vec3 offset)
{
	if (iNode < 0)
		return;
	BSPNODE32& node = map->nodes[iNode];

	if (currentPlane == activePlane)
		drawPlane(map->planes[node.iPlane], { 255, 128, 128, 255 }, offset);
	currentPlane++;

	for (int i = 0; i < 2; i++)
	{
		if (node.iChildren[i] >= 0)
		{
			drawNodes(map, node.iChildren[i], currentPlane, activePlane);
		}
	}
}

vec3 Renderer::getEntOrigin(Bsp* map, Entity* ent)
{
	vec3 origin = ent->origin;
	return origin + getEntOffset(map, ent);
}

vec3 Renderer::getEntOffset(Bsp* map, Entity* ent)
{
	if (ent->isBspModel())
	{
		BSPMODEL& tmodel = map->models[ent->getBspModelIdx()];
		return tmodel.nMins + (tmodel.nMaxs - tmodel.nMins) * 0.5f;
	}
	return vec3();
}

void Renderer::updateDragAxes(vec3 delta)
{
	Bsp* map = SelectedMap;
	Entity* ent = NULL;
	int modelIdx = -1;
	vec3 mapOffset;
	vec3 localCameraOrigin;
	auto& entIdx = pickInfo.selectedEnts;

	if (map && entIdx.size())
	{
		BspRenderer* rend = map->getBspRender();
		if (rend)
		{
			ent = map->ents[entIdx[0]];
			modelIdx = ent->getBspModelIdx();
			mapOffset = rend->mapOffset;
			localCameraOrigin = rend->localCameraOrigin;
		}
		else
		{
			return;
		}
	}
	else
	{
		return;
	}

	vec3 entMin, entMax;
	// set origin of the axes
	if (transformMode == TRANSFORM_MODE_SCALE)
	{
		if (ent && modelIdx >= 0)
		{
			entMin = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
			entMax = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);

			if (modelVerts.size())
			{
				for (auto& vert : modelVerts)
				{
					expandBoundingBox(vert.pos, entMin, entMax);
				}
			}
			else
			{
				map->get_model_vertex_bounds(modelIdx, entMin, entMax);
			}
			vec3 modelOrigin = entMin + (entMax - entMin) * 0.5f;

			entMax -= modelOrigin;
			entMin -= modelOrigin;

			scaleAxes.origin = modelOrigin;
			scaleAxes.origin += ent->origin;
			scaleAxes.origin += delta;
		}
	}
	else
	{
		if (ent)
		{
			if (transformTarget == TRANSFORM_ORIGIN)
			{
				if (modelIdx >= 0)
				{
					/*entMin = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
					entMax = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);

					if (modelVerts.size())
					{
						for (auto& vert : modelVerts)
						{
							expandBoundingBox(vert.pos, entMin, entMax);
						}
					}
					else
					{
						map->get_model_vertex_bounds(modelIdx, entMin, entMax);
					}
					vec3 modelOrigin = entMin + (entMax - entMin) * 0.5f;*/

					moveAxes.origin = map->models[modelIdx].vOrigin/* + modelOrigin*/;
					moveAxes.origin += ent->origin;
				}
				else
					moveAxes.origin = ent->origin;

				moveAxes.origin += delta;
			}
			else
			{
				moveAxes.origin = getEntOrigin(map, ent);
				moveAxes.origin += delta;
			}
		}

		if (entIdx.empty())
		{
			moveAxes.origin -= mapOffset;
		}

		if (transformTarget == TRANSFORM_VERTEX)
		{
			vec3 entOrigin = ent ? ent->origin : vec3();
			vec3 min(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
			vec3 max(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);
			int selectTotal = 0;
			for (size_t i = 0; i < modelVerts.size(); i++)
			{
				if (modelVerts[i].selected)
				{
					vec3 v = modelVerts[i].pos + entOrigin;
					if (v.x < min.x) min.x = v.x;
					if (v.y < min.y) min.y = v.y;
					if (v.z < min.z) min.z = v.z;
					if (v.x > max.x) max.x = v.x;
					if (v.y > max.y) max.y = v.y;
					if (v.z > max.z) max.z = v.z;
					selectTotal++;
				}
			}
			if (selectTotal != 0)
			{
				moveAxes.origin = min + (max - min) * 0.5f;
				moveAxes.origin += delta;
			}
		}
	}

	// create the meshes
	if (transformMode == TRANSFORM_MODE_SCALE)
	{
		float baseScale = (scaleAxes.origin - localCameraOrigin).length() * 0.005f;
		float s = baseScale;
		float s2 = baseScale * 2;
		float d = baseScale * 32;


		vec3 axisMins[6] = {
			vec3(0, -s, -s) + vec3(entMax.x,0,0), // x+
			vec3(-s, 0, -s) + vec3(0,entMax.y,0), // y+
			vec3(-s, -s, 0) + vec3(0,0,entMax.z), // z+

			vec3(-d, -s, -s) + vec3(entMin.x,0,0), // x-
			vec3(-s, -d, -s) + vec3(0,entMin.y,0), // y-
			vec3(-s, -s, -d) + vec3(0,0,entMin.z)  // z-
		};
		vec3 axisMaxs[6] = {
			vec3(d, s, s) + vec3(entMax.x,0,0), // x+
			vec3(s, d, s) + vec3(0,entMax.y,0), // y+
			vec3(s, s, d) + vec3(0,0,entMax.z), // z+

			vec3(0, s, s) + vec3(entMin.x,0,0), // x-
			vec3(s, 0, s) + vec3(0,entMin.y,0), // y-
			vec3(s, s, 0) + vec3(0,0,entMin.z)  // z-
		};

		scaleAxes.model[0] = cCube(axisMins[0], axisMaxs[0], scaleAxes.dimColor[0]);
		scaleAxes.model[1] = cCube(axisMins[1], axisMaxs[1], scaleAxes.dimColor[1]);
		scaleAxes.model[2] = cCube(axisMins[2], axisMaxs[2], scaleAxes.dimColor[2]);

		scaleAxes.model[3] = cCube(axisMins[3], axisMaxs[3], scaleAxes.dimColor[3]);
		scaleAxes.model[4] = cCube(axisMins[4], axisMaxs[4], scaleAxes.dimColor[4]);
		scaleAxes.model[5] = cCube(axisMins[5], axisMaxs[5], scaleAxes.dimColor[5]);

		// flip to HL coords
		cVert* verts = (cVert*)scaleAxes.model;
		for (int i = 0; i < 6 * 6 * 6; i++)
		{
			verts[i].pos = verts[i].pos.flip();
		}

		// larger mins/maxs so you can be less precise when selecting them
		s *= 4;
		vec3 grabAxisMins[6] = {
			vec3(0, -s, -s) + vec3(entMax.x,0,0), // x+
			vec3(-s, 0, -s) + vec3(0,entMax.y,0), // y+
			vec3(-s, -s, 0) + vec3(0,0,entMax.z), // z+

			vec3(-d, -s, -s) + vec3(entMin.x,0,0), // x-
			vec3(-s, -d, -s) + vec3(0,entMin.y,0), // y-
			vec3(-s, -s, -d) + vec3(0,0,entMin.z)  // z-
		};
		vec3 grabAxisMaxs[6] = {
			vec3(d, s, s) + vec3(entMax.x,0,0), // x+
			vec3(s, d, s) + vec3(0,entMax.y,0), // y+
			vec3(s, s, d) + vec3(0,0,entMax.z), // z+

			vec3(0, s, s) + vec3(entMin.x,0,0), // x-
			vec3(s, 0, s) + vec3(0,entMin.y,0), // y-
			vec3(s, s, 0) + vec3(0,0,entMin.z)  // z-
		};

		for (int i = 0; i < 6; i++)
		{
			scaleAxes.mins[i] = grabAxisMins[i];
			scaleAxes.maxs[i] = grabAxisMaxs[i];
		}


		if (hoverAxis >= 0 && hoverAxis < scaleAxes.numAxes)
		{
			scaleAxes.model[hoverAxis].setColor(scaleAxes.hoverColor[hoverAxis]);
		}
		else if (gui->guiHoverAxis >= 0 && gui->guiHoverAxis < scaleAxes.numAxes)
		{
			scaleAxes.model[gui->guiHoverAxis].setColor(scaleAxes.hoverColor[gui->guiHoverAxis]);
		}

		scaleAxes.origin += mapOffset;
		scaleAxes.buffer->uploaded = false;
	}
	else
	{
		float baseScale = (moveAxes.origin - localCameraOrigin).length() * 0.005f;
		float s = baseScale;
		float s2 = baseScale * 1.2f;
		float d = baseScale * 32.0f;

		// flipped for HL coords
		moveAxes.model[0] = cCube(vec3(0, -s, -s), vec3(d, s, s), moveAxes.dimColor[0]);
		moveAxes.model[2] = cCube(vec3(-s, 0, -s), vec3(s, d, s), moveAxes.dimColor[2]);
		moveAxes.model[1] = cCube(vec3(-s, -s, 0), vec3(s, s, -d), moveAxes.dimColor[1]);
		moveAxes.model[3] = cCube(vec3(-s2, -s2, -s2), vec3(s2, s2, s2), moveAxes.dimColor[3]);

		// larger mins/maxs so you can be less precise when selecting them
		s *= 4;
		s2 *= 1.7f;

		moveAxes.mins[0] = vec3(0, -s, -s);
		moveAxes.mins[1] = vec3(-s, 0, -s);
		moveAxes.mins[2] = vec3(-s, -s, 0);
		moveAxes.mins[3] = vec3(-s2, -s2, -s2);

		moveAxes.maxs[0] = vec3(d, s, s);
		moveAxes.maxs[1] = vec3(s, d, s);
		moveAxes.maxs[2] = vec3(s, s, d);
		moveAxes.maxs[3] = vec3(s2, s2, s2);


		if (hoverAxis >= 0 && hoverAxis < moveAxes.numAxes)
		{
			moveAxes.model[hoverAxis].setColor(moveAxes.hoverColor[hoverAxis]);
		}
		else if (gui->guiHoverAxis >= 0 && gui->guiHoverAxis < moveAxes.numAxes)
		{
			moveAxes.model[gui->guiHoverAxis].setColor(moveAxes.hoverColor[gui->guiHoverAxis]);
		}

		moveAxes.origin += mapOffset;
		moveAxes.buffer->uploaded = false;
	}
}

vec3 Renderer::getAxisDragPoint(vec3 origin)
{
	vec3 pickStart, pickDir;
	getPickRay(pickStart, pickDir);

	vec3 axisNormals[3] = {
		vec3(1,0,0),
		vec3(0,1,0),
		vec3(0,0,1)
	};

	// get intersection points between the pick ray and each each movement direction plane
	float dots[3]{};
	for (int i = 0; i < 3; i++)
	{
		dots[i] = abs(dotProduct(cameraForward, axisNormals[i]));
	}

	// best movement planee is most perpindicular to the camera direction
	// and ignores the plane being moved
	int bestMovementPlane = 0;
	switch (hoverAxis % 3)
	{
	case 0: bestMovementPlane = dots[1] > dots[2] ? 1 : 2; break;
	case 1: bestMovementPlane = dots[0] > dots[2] ? 0 : 2; break;
	case 2: bestMovementPlane = dots[1] > dots[0] ? 1 : 0; break;
	}

	float fDist = ((float*)&origin)[bestMovementPlane];
	float intersectDist;
	rayPlaneIntersect(pickStart, pickDir, axisNormals[bestMovementPlane], fDist, intersectDist);

	// don't let ents zoom out to infinity
	if (intersectDist < 0)
	{
		intersectDist = 0;
	}

	return pickStart + pickDir * intersectDist;
}

void Renderer::updateModelVerts()
{
	Bsp* map = SelectedMap;
	int modelIdx = -1;
	Entity* ent = NULL;
	auto entIdx = pickInfo.selectedEnts;


	if (modelVertBuff)
	{
		delete modelVertBuff;
		modelVertBuff = NULL;
	}

	if (modelVertCubes)
	{
		delete[] modelVertCubes;
		modelVertCubes = NULL;
	}

	scaleTexinfos.clear();
	modelEdges.clear();
	modelVerts.clear();
	modelFaceVerts.clear();

	if (entIdx.size())
	{
		ent = map->ents[entIdx[0]];
		modelIdx = ent->getBspModelIdx();
	}
	else
	{
		modelTransform = -1;
		return;
	}

	modelTransform = modelIdx;

	if (!modelOriginBuff)
	{
		modelOriginBuff = new VertexBuffer(colorShader, &modelOriginCube, 6 * 6, GL_TRIANGLES);
	}
	else
	{
		modelOriginBuff->uploaded = false;
	}

	if (modelIdx < 0)
	{
		originSelected = false;
		updateSelectionSize();
		return;
	}

	//map->getBspRender()->refreshModel(modelIdx);

	updateSelectionSize();

	if (!map->is_convex(modelIdx))
	{
		return;
	}

	scaleTexinfos = map->getScalableTexinfos(modelIdx);

	map->getModelPlaneIntersectVerts(modelIdx, modelVerts); // for vertex manipulation + scaling

	modelFaceVerts = map->getModelTransformVerts(modelIdx); // for scaling only

	Solid modelSolid;
	if (!getModelSolid(modelVerts, map, modelSolid))
	{
		scaleTexinfos.clear();
		modelEdges.clear();
		modelVerts.clear();
		modelFaceVerts.clear();
		return;
	}

	modelEdges = modelSolid.hullEdges;

	size_t numCubes = modelVerts.size() + modelEdges.size();

	if (numCubes == 0)
	{
		scaleTexinfos.clear();
		modelEdges.clear();
		modelVerts.clear();
		modelFaceVerts.clear();
		return;
	}

	modelVertCubes = new cCube[numCubes];
	modelVertBuff = new VertexBuffer(colorShader, modelVertCubes, (6 * 6 * numCubes), GL_TRIANGLES);
	//print_log(get_localized_string(LANG_0913),modelVerts.size());
}

void Renderer::updateSelectionSize()
{
	selectionSize = vec3();
	Bsp* map = SelectedMap;

	if (!map)
	{
		return;
	}

	int modelIdx = -1;
	auto entIdx = pickInfo.selectedEnts;

	if (entIdx.size())
	{
		modelIdx = map->ents[entIdx[0]]->getBspModelIdx();
	}

	if (!entIdx.size() || modelIdx <= 0)
	{
		vec3 mins, maxs;
		map->get_bounding_box(mins, maxs);
		selectionSize = maxs - mins;
	}
	else if (modelIdx > 0)
	{
		vec3 mins, maxs;
		if (map->models[modelIdx].nFaces == 0)
		{
			mins = map->models[modelIdx].nMins;
			maxs = map->models[modelIdx].nMaxs;
		}
		else
		{
			map->get_model_vertex_bounds(modelIdx, mins, maxs);
		}
		selectionSize = maxs - mins;
	}
	else if (entIdx.size())
	{
		Entity* ent = map->ents[entIdx[0]];
		EntCube* cube = pointEntRenderer->getEntCube(ent);
		if (cube)
			selectionSize = cube->maxs - cube->mins;
	}
}

void Renderer::updateEntConnections()
{
	if (entConnections)
	{
		delete entConnections;
		delete entConnectionPoints;
		entConnections = NULL;
		entConnectionPoints = NULL;
	}

	Bsp* map = SelectedMap;
	auto entIdx = pickInfo.selectedEnts;

	if (!(g_render_flags & RENDER_ENT_CONNECTIONS) || entIdx.empty() || !map)
	{
		return;
	}

	Entity* ent = map->ents[entIdx[0]];

	std::vector<std::string> targetNames = ent->getTargets();
	std::vector<Entity*> targets;
	std::vector<Entity*> callers;
	std::vector<Entity*> callerAndTarget; // both a target and a caller
	std::string thisName;

	if (ent->hasKey("targetname"))
	{
		thisName = ent->keyvalues["targetname"];
	}

	for (size_t k = 0; k < map->ents.size(); k++)
	{
		Entity* tEnt = map->ents[k];

		if (tEnt == ent)
			continue;

		bool isTarget = false;
		if (tEnt->hasKey("targetname"))
		{
			std::string tname = tEnt->keyvalues["targetname"];
			for (size_t i = 0; i < targetNames.size(); i++)
			{
				if (tname == targetNames[i])
				{
					isTarget = true;
					break;
				}
			}
		}

		bool isCaller = thisName.length() && tEnt->hasTarget(thisName);

		if (isTarget && isCaller)
		{
			callerAndTarget.push_back(tEnt);
		}
		else if (isTarget)
		{
			targets.push_back(tEnt);
		}
		else if (isCaller)
		{
			callers.push_back(tEnt);
		}
	}

	if (targets.empty() && callers.empty() && callerAndTarget.empty())
	{
		return;
	}

	size_t numVerts = targets.size() * 2 + callers.size() * 2 + callerAndTarget.size() * 2;
	size_t numPoints = callers.size() + targets.size() + callerAndTarget.size();
	cVert* lines = new cVert[numVerts + 9];
	cCube* points = new cCube[numPoints + 3];

	const COLOR4 targetColor = { 255, 255, 0, 255 };
	const COLOR4 callerColor = { 0, 255, 255, 255 };
	const COLOR4 bothColor = { 0, 255, 0, 255 };

	vec3 srcPos = getEntOrigin(map, ent).flip();
	size_t idx = 0;
	size_t cidx = 0;
	float s = 1.5f;
	vec3 extent = vec3(s, s, s);

	for (size_t i = 0; i < targets.size(); i++)
	{
		vec3 ori = getEntOrigin(map, targets[i]).flip();
		points[cidx++] = cCube(ori - extent, ori + extent, targetColor);
		lines[idx++] = cVert(srcPos, targetColor);
		lines[idx++] = cVert(ori, targetColor);
	}
	for (size_t i = 0; i < callers.size(); i++)
	{
		vec3 ori = getEntOrigin(map, callers[i]).flip();
		points[cidx++] = cCube(ori - extent, ori + extent, callerColor);
		lines[idx++] = cVert(srcPos, callerColor);
		lines[idx++] = cVert(ori, callerColor);
	}
	for (size_t i = 0; i < callerAndTarget.size() && cidx < numPoints && idx < numVerts; i++)
	{
		vec3 ori = getEntOrigin(map, callerAndTarget[i]).flip();
		points[cidx++] = cCube(ori - extent, ori + extent, bothColor);
		lines[idx++] = cVert(srcPos, bothColor);
		lines[idx++] = cVert(ori, bothColor);
	}

	entConnections = new VertexBuffer(colorShader, lines, numVerts, GL_LINES);
	entConnectionPoints = new VertexBuffer(colorShader, points, (numPoints * 6 * 6), GL_TRIANGLES);
	entConnections->ownData = true;
	entConnectionPoints->ownData = true;
}

void Renderer::updateEntConnectionPositions()
{
	auto entIdx = pickInfo.selectedEnts;
	if (entConnections && entIdx.size())
	{
		Entity* ent = SelectedMap->ents[entIdx[0]];
		vec3 pos = getEntOrigin(getSelectedMap(), ent).flip();

		cVert* verts = (cVert*)entConnections->get_data();
		for (int i = 0; i < entConnections->numVerts; i += 2)
		{
			verts[i].pos = pos;
		}
		entConnections->uploaded = false;
	}
}

bool Renderer::getModelSolid(std::vector<TransformVert>& hullVerts, Bsp* map, Solid& outSolid)
{
	outSolid.faces.clear();
	outSolid.hullEdges.clear();
	outSolid.hullVerts.clear();
	outSolid.hullVerts = hullVerts;

	// get verts for each plane
	std::map<int, std::vector<size_t>> planeVerts;
	for (size_t i = 0; i < hullVerts.size(); i++)
	{
		for (size_t k = 0; k < hullVerts[i].iPlanes.size(); k++)
		{
			int iPlane = hullVerts[i].iPlanes[k];
			planeVerts[iPlane].push_back(i);
		}
	}

	vec3 centroid = getCentroid(hullVerts);

	// sort verts CCW on each plane to get edges
	for (auto it = planeVerts.begin(); it != planeVerts.end(); ++it)
	{
		int iPlane = it->first;
		std::vector<size_t> verts = it->second;
		BSPPLANE& plane = map->planes[iPlane];
		if (verts.size() < 2)
		{
			if (g_settings.verboseLogs)
				print_log(get_localized_string(LANG_0914)); // hl_c00 pipe in green water place
			return false;
		}

		std::vector<vec3> tempVerts(verts.size());
		for (size_t i = 0; i < verts.size(); i++)
		{
			tempVerts[i] = hullVerts[verts[i]].pos;
		}

		std::vector<size_t> orderedVerts = getSortedPlanarVertOrder(tempVerts);
		for (size_t i = 0; i < orderedVerts.size(); i++)
		{
			orderedVerts[i] = verts[orderedVerts[i]];
			tempVerts[i] = hullVerts[orderedVerts[i]].pos;
		}

		Face face;
		face.plane = plane;

		vec3 orderedVertsNormal = getNormalFromVerts(tempVerts);

		// get plane normal, flipping if it points inside the solid
		vec3 faceNormal = plane.vNormal;
		vec3 planeDir = ((plane.vNormal * plane.fDist) - centroid).normalize();
		face.planeSide = 1;

		if (dotProduct(planeDir, plane.vNormal) > EPSILON)
		{
			faceNormal = faceNormal.invert();
			face.planeSide = 0;
		}

		// reverse vert order if not CCW when viewed from outside the solid
		if (dotProduct(orderedVertsNormal, faceNormal) < EPSILON)
		{
			reverse(orderedVerts.begin(), orderedVerts.end());
		}

		for (size_t i = 0; i < orderedVerts.size(); i++)
		{
			face.verts.push_back(orderedVerts[i]);
		}

		face.iTextureInfo = 1; // TODO
		outSolid.faces.push_back(face);

		for (size_t i = 0; i < orderedVerts.size(); i++)
		{
			HullEdge edge = HullEdge();
			edge.verts[0] = orderedVerts[i];
			edge.verts[1] = orderedVerts[(i + 1) % orderedVerts.size()];
			edge.selected = false;

			// find the planes that this edge joins
			vec3 midPoint = getEdgeControlPoint(hullVerts, edge);
			int planeCount = 0;
			for (auto it2 = planeVerts.begin(); it2 != planeVerts.end(); ++it2)
			{
				int iPlane2 = it2->first;
				BSPPLANE& p = map->planes[iPlane2];
				float dist = dotProduct(midPoint, p.vNormal) - p.fDist;
				if (abs(dist) < ON_EPSILON)
				{
					edge.planes[planeCount % 2] = iPlane2;
					planeCount++;
				}
			}
			if (planeCount != 2)
			{
				if (g_settings.verboseLogs)
					print_log(get_localized_string(LANG_0915), planeCount);
				return false;
			}

			outSolid.hullEdges.push_back(edge);
		}
	}

	return true;
}

void Renderer::scaleSelectedObject(float x, float y, float z)
{
	vec3 minDist;
	vec3 maxDist;

	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		vec3 v = modelVerts[i].startPos;
		if (v.x > maxDist.x) maxDist.x = v.x;
		if (v.x < minDist.x) minDist.x = v.x;

		if (v.y > maxDist.y) maxDist.y = v.y;
		if (v.y < minDist.y) minDist.y = v.y;

		if (v.z > maxDist.z) maxDist.z = v.z;
		if (v.z < minDist.z) minDist.z = v.z;
	}
	vec3 distRange = maxDist - minDist;

	vec3 dir;
	dir.x = (distRange.x * x) - distRange.x;
	dir.y = (distRange.y * y) - distRange.y;
	dir.z = (distRange.z * z) - distRange.z;

	scaleSelectedObject(dir, vec3());
}

void Renderer::scaleSelectedObject(vec3 dir, const vec3& fromDir, bool logging)
{
	auto entIdx = pickInfo.selectedEnts;
	if (entIdx.empty() || !SelectedMap)
		return;

	Bsp* map = SelectedMap;

	bool scaleFromOrigin = abs(fromDir.x) < EPSILON && abs(fromDir.y) < EPSILON && abs(fromDir.z) < EPSILON;

	vec3 minDist = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
	vec3 maxDist = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);

	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		expandBoundingBox(modelVerts[i].startPos, minDist, maxDist);
	}
	for (size_t i = 0; i < modelFaceVerts.size(); i++)
	{
		expandBoundingBox(modelFaceVerts[i].startPos, minDist, maxDist);
	}

	vec3 distRange = maxDist - minDist;

	vec3 scaleFromDist = minDist;
	if (scaleFromOrigin)
	{
		scaleFromDist = minDist + (maxDist - minDist) * 0.5f;
	}
	else
	{
		if (fromDir.x < 0)
		{
			scaleFromDist.x = maxDist.x;
			dir.x = -dir.x;
		}
		if (fromDir.y < 0)
		{
			scaleFromDist.y = maxDist.y;
			dir.y = -dir.y;
		}
		if (fromDir.z < 0)
		{
			scaleFromDist.z = maxDist.z;
			dir.z = -dir.z;
		}
	}

	// scale planes
	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		vec3 stretchFactor = (modelVerts[i].startPos - scaleFromDist) / distRange;
		modelVerts[i].pos += dir * stretchFactor;
		if (gridSnappingEnabled)
		{
			modelVerts[i].pos = snapToGrid(modelVerts[i].pos);
		}
		if (modelVerts[i].ptr)
			*modelVerts[i].ptr = modelVerts[i].pos;
	}

	// scale visible faces
	for (size_t i = 0; i < modelFaceVerts.size(); i++)
	{
		vec3 stretchFactor = (modelFaceVerts[i].startPos - scaleFromDist) / distRange;
		modelFaceVerts[i].pos += dir * stretchFactor;
		if (gridSnappingEnabled)
		{
			modelFaceVerts[i].pos = snapToGrid(modelFaceVerts[i].pos);
		}
		if (modelFaceVerts[i].ptr)
			*modelFaceVerts[i].ptr = modelFaceVerts[i].pos;
	}

	updateSelectionSize();
	//
	// TODO: I have no idea what I'm doing but this code scales axis-aligned texture coord axes correctly.
	//       Rewrite all of this after understanding texture axes.
	//

	if (textureLock)
	{
		minDist = vec3(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
		maxDist = vec3(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);

		for (size_t i = 0; i < modelFaceVerts.size(); i++)
		{
			expandBoundingBox(modelFaceVerts[i].pos, minDist, maxDist);
		}
		vec3 newDistRange = maxDist - minDist;
		vec3 scaleFactor = distRange / newDistRange;

		mat4x4 scaleMat;
		scaleMat.loadIdentity();
		scaleMat.scale(scaleFactor.x, scaleFactor.y, scaleFactor.z);

		for (size_t i = 0; i < scaleTexinfos.size(); i++)
		{
			ScalableTexinfo& oldinfo = scaleTexinfos[i];
			BSPTEXTUREINFO& info = map->texinfos[scaleTexinfos[i].texinfoIdx];

			info.vS = (scaleMat * vec4(oldinfo.oldS, 1)).xyz();
			info.vT = (scaleMat * vec4(oldinfo.oldT, 1)).xyz();

			float shiftS = oldinfo.oldShiftS;
			float shiftT = oldinfo.oldShiftT;

			// magic guess-and-check code that somehow works some of the time
			// also its shit
			for (int k = 0; k < 3; k++)
			{
				vec3 stretchDir;
				if (k == 0) stretchDir = vec3(dir.x, 0, 0).normalize();
				if (k == 1) stretchDir = vec3(0, dir.y, 0).normalize();
				if (k == 2) stretchDir = vec3(0, 0, dir.z).normalize();

				float refDist = 0;
				if (k == 0) refDist = scaleFromDist.x;
				if (k == 1) refDist = scaleFromDist.y;
				if (k == 2) refDist = scaleFromDist.z;

				vec3 texFromDir;
				if (k == 0) texFromDir = dir * vec3(1, 0, 0);
				if (k == 1) texFromDir = dir * vec3(0, 1, 0);
				if (k == 2) texFromDir = dir * vec3(0, 0, 1);

				float dotS = dotProduct(oldinfo.oldS.normalize(), stretchDir);
				float dotT = dotProduct(oldinfo.oldT.normalize(), stretchDir);

				float dotSm = dotProduct(texFromDir, info.vS) < 0 ? 1.0f : -1.0f;
				float dotTm = dotProduct(texFromDir, info.vT) < 0 ? 1.0f : -1.0f;

				// hurr dur oh god im fucking retarded huurr
				if (k == 0 && dotProduct(texFromDir, fromDir) < 0 != fromDir.x < 0)
				{
					dotSm *= -1.0f;
					dotTm *= -1.0f;
				}
				if (k == 1 && dotProduct(texFromDir, fromDir) < 0 != fromDir.y < 0)
				{
					dotSm *= -1.0f;
					dotTm *= -1.0f;
				}
				if (k == 2 && dotProduct(texFromDir, fromDir) < 0 != fromDir.z < 0)
				{
					dotSm *= -1.0f;
					dotTm *= -1.0f;
				}

				float vsdiff = info.vS.length() - oldinfo.oldS.length();
				float vtdiff = info.vT.length() - oldinfo.oldT.length();

				shiftS += (refDist * vsdiff * abs(dotS)) * dotSm;
				shiftT += (refDist * vtdiff * abs(dotT)) * dotTm;
			}

			info.shiftS = shiftS;
			info.shiftT = shiftT;
		}
	}
}

void Renderer::moveSelectedVerts(const vec3& delta)
{
	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		if (modelVerts[i].selected)
		{
			modelVerts[i].pos += delta;
			if (gridSnappingEnabled)
				modelVerts[i].pos = snapToGrid(modelVerts[i].pos);
			if (modelVerts[i].ptr)
				*modelVerts[i].ptr = modelVerts[i].pos;
		}
	}

	Bsp* map = SelectedMap;
	auto entIdx = pickInfo.selectedEnts;
	if (map && entIdx.size())
	{
		Entity* ent = map->ents[entIdx[0]];
		map->getBspRender()->refreshModel(ent->getBspModelIdx());
	}
}

bool Renderer::splitModelFace()
{
	Bsp* map = SelectedMap;
	auto entIdx = pickInfo.selectedEnts;
	if (!map)
	{
		print_log(get_localized_string(LANG_0916));
		return false;
	}
	BspRenderer* mapRenderer = map->getBspRender();
	// find the pseudo-edge to split with
	std::vector<size_t> selectedEdges;
	for (size_t i = 0; i < modelEdges.size(); i++)
	{
		if (modelEdges[i].selected)
		{
			selectedEdges.push_back(i);
		}
	}

	if (selectedEdges.size() != 2)
	{
		print_log(get_localized_string(LANG_0917));
		return false;
	}
	if (entIdx.empty())
	{
		print_log(get_localized_string(LANG_0918));
		return false;
	}
	Entity* ent = map->ents[entIdx[0]];

	HullEdge& edge1 = modelEdges[selectedEdges[0]];
	HullEdge& edge2 = modelEdges[selectedEdges[1]];
	int commonPlane = -1;
	for (size_t i = 0; i < 2 && commonPlane == -1; i++)
	{
		size_t thisPlane = edge1.planes[i];
		for (size_t k = 0; k < 2; k++)
		{
			size_t otherPlane = edge2.planes[k];
			if (thisPlane == otherPlane)
			{
				commonPlane = (int)thisPlane;
				break;
			}
		}
	}

	if (commonPlane == -1)
	{
		print_log(get_localized_string(LANG_0919));
		return false;
	}

	vec3 splitPoints[2] = {
		getEdgeControlPoint(modelVerts, edge1),
		getEdgeControlPoint(modelVerts, edge2)
	};

	std::vector<int> modelPlanes;


	BSPMODEL& tmodel = map->models[ent->getBspModelIdx()];
	map->getNodePlanes(tmodel.iHeadnodes[0], modelPlanes);

	// find the plane being split
	int commonPlaneIdx = -1;
	for (size_t i = 0; i < modelPlanes.size(); i++)
	{
		if (modelPlanes[i] == commonPlane)
		{
			commonPlaneIdx = (int)i;
			break;
		}
	}
	if (commonPlaneIdx == -1)
	{
		print_log(get_localized_string(LANG_0920));
		return false;
	}

	// extrude split points so that the new planes aren't coplanar
	{
		size_t i0 = edge1.verts[0];
		size_t i1 = edge1.verts[1];
		size_t i2 = edge2.verts[0];
		if (i2 == i1 || i2 == i0)
			i2 = edge2.verts[1];

		vec3 v0 = modelVerts[i0].pos;
		vec3 v1 = modelVerts[i1].pos;
		vec3 v2 = modelVerts[i2].pos;

		vec3 e1 = (v1 - v0).normalize();
		vec3 e2 = (v2 - v0).normalize();
		vec3 normal = crossProduct(e1, e2).normalize();

		vec3 centroid = getCentroid(modelVerts);
		vec3 faceDir = (centroid - v0).normalize();
		if (dotProduct(faceDir, normal) > 0)
		{
			normal *= -1;
		}

		for (int i = 0; i < 2; i++)
			splitPoints[i] += normal * 4;
	}

	// replace split plane with 2 new slightly-angled planes
	{
		vec3 planeVerts[2][3] = {
			{
				splitPoints[0],
				modelVerts[edge1.verts[1]].pos,
				splitPoints[1]
			},
			{
				splitPoints[0],
				splitPoints[1],
				modelVerts[edge1.verts[0]].pos
			}
		};

		modelPlanes.erase(modelPlanes.begin() + commonPlaneIdx);
		for (int i = 0; i < 2; i++)
		{
			vec3 e1 = (planeVerts[i][1] - planeVerts[i][0]).normalize();
			vec3 e2 = (planeVerts[i][2] - planeVerts[i][0]).normalize();
			vec3 normal = crossProduct(e1, e2).normalize();

			int newPlaneIdx = map->create_plane();
			BSPPLANE& plane = map->planes[newPlaneIdx];
			plane.update_plane(normal, getDistAlongAxis(normal, planeVerts[i][0]));
			modelPlanes.push_back(newPlaneIdx);
		}
	}

	// create a new model from the new set of planes
	std::vector<TransformVert> newHullVerts;
	if (!map->getModelPlaneIntersectVerts(ent->getBspModelIdx(), modelPlanes, newHullVerts))
	{
		print_log(get_localized_string(LANG_0921));
		return false;
	}

	Solid newSolid;
	if (!getModelSolid(newHullVerts, map, newSolid))
	{
		print_log(get_localized_string(LANG_0922));
		return false;
	}

	// test that all planes have at least 3 verts
	{
		std::map<int, std::vector<vec3>> planeVerts;
		for (size_t i = 0; i < newHullVerts.size(); i++)
		{
			for (size_t k = 0; k < newHullVerts[i].iPlanes.size(); k++)
			{
				int iPlane = newHullVerts[i].iPlanes[k];
				planeVerts[iPlane].push_back(newHullVerts[i].pos);
			}
		}
		for (auto it = planeVerts.begin(); it != planeVerts.end(); ++it)
		{
			std::vector<vec3>& verts = it->second;

			if (verts.size() < 3)
			{
				print_log(get_localized_string(LANG_0923));
				return false;
			}
		}
	}

	// copy textures/UVs from the old model
	{
		BSPMODEL& oldModel = map->models[ent->getBspModelIdx()];
		for (size_t i = 0; i < newSolid.faces.size(); i++)
		{
			Face& solidFace = newSolid.faces[i];
			BSPFACE32* bestMatch = NULL;
			float bestdot = -FLT_MAX_COORD;
			for (int k = 0; k < oldModel.nFaces; k++)
			{
				BSPFACE32& BSPFACE32 = map->faces[oldModel.iFirstFace + k];
				BSPPLANE& plane = map->planes[BSPFACE32.iPlane];
				vec3 bspFaceNormal = BSPFACE32.nPlaneSide ? plane.vNormal.invert() : plane.vNormal;
				vec3 solidFaceNormal = solidFace.planeSide ? solidFace.plane.vNormal.invert() : solidFace.plane.vNormal;
				float dot = dotProduct(bspFaceNormal, solidFaceNormal);
				if (dot > bestdot)
				{
					bestdot = dot;
					bestMatch = &BSPFACE32;
				}
			}
			if (bestMatch)
			{
				solidFace.iTextureInfo = bestMatch->iTextureInfo;
			}
		}
	}

	int modelIdx = map->create_solid(newSolid, ent->getBspModelIdx());

	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		modelVerts[i].selected = false;
	}
	for (size_t i = 0; i < modelEdges.size(); i++)
	{
		modelEdges[i].selected = false;
	}

	gui->reloadLimits();


	map->getBspRender()->pushModelUndoState("Split Face", EDIT_MODEL_LUMPS);
	map->resize_all_lightmaps();

	mapRenderer->loadLightmaps();
	mapRenderer->calcFaceMaths();
	mapRenderer->refreshModel(modelIdx);
	updateModelVerts();

	return true;
}

void Renderer::scaleSelectedVerts(float x, float y, float z)
{
	auto entIdx = pickInfo.selectedEnts;
	TransformAxes& activeAxes = *(transformMode == TRANSFORM_MODE_SCALE ? &scaleAxes : &moveAxes);
	vec3 fromOrigin = activeAxes.origin;

	vec3 min(FLT_MAX_COORD, FLT_MAX_COORD, FLT_MAX_COORD);
	vec3 max(-FLT_MAX_COORD, -FLT_MAX_COORD, -FLT_MAX_COORD);
	int selectTotal = 0;
	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		if (modelVerts[i].selected)
		{
			vec3 v = modelVerts[i].pos;
			if (v.x < min.x) min.x = v.x;
			if (v.y < min.y) min.y = v.y;
			if (v.z < min.z) min.z = v.z;
			if (v.x > max.x) max.x = v.x;
			if (v.y > max.y) max.y = v.y;
			if (v.z > max.z) max.z = v.z;
			selectTotal++;
		}
	}
	if (selectTotal != 0)
		fromOrigin = min + (max - min) * 0.5f;

	debugVec1 = fromOrigin;

	for (size_t i = 0; i < modelVerts.size(); i++)
	{

		if (modelVerts[i].selected)
		{
			vec3 delta = modelVerts[i].startPos - fromOrigin;
			modelVerts[i].pos = fromOrigin + delta * vec3(x, y, z);
			if (gridSnappingEnabled)
				modelVerts[i].pos = snapToGrid(modelVerts[i].pos);
			if (modelVerts[i].ptr)
				*modelVerts[i].ptr = modelVerts[i].pos;
		}
	}
	Bsp* map = SelectedMap;
	if (map)
	{
		if (entIdx.size())
		{
			//modelIdx = map->ents[entIdx]->getBspModelIdx();
			Entity* ent = map->ents[entIdx[0]];
			map->getBspRender()->refreshModel(ent->getBspModelIdx());
		}
		updateSelectionSize();
	}
	else
	{
		print_log(get_localized_string(LANG_0924));
	}
}

vec3 Renderer::getEdgeControlPoint(const std::vector<TransformVert>& hullVerts, HullEdge& edge)
{
	vec3 v0 = hullVerts[edge.verts[0]].pos;
	vec3 v1 = hullVerts[edge.verts[1]].pos;
	return v0 + (v1 - v0) * 0.5f;
}

vec3 Renderer::getCentroid(std::vector<TransformVert>& hullVerts)
{
	vec3 centroid;
	for (size_t i = 0; i < hullVerts.size(); i++)
	{
		centroid += hullVerts[i].pos;
	}
	return centroid / (float)hullVerts.size();
}

vec3 Renderer::snapToGrid(vec3 pos)
{
	float snapSize = (float)pow(2.0f, gridSnapLevel);
	return pos.snap(snapSize);
}

void Renderer::grabEnt()
{
	auto entIdx = pickInfo.selectedEnts;
	if (entIdx.empty() || pickInfo.IsSelectedEnt(0))
	{
		movingEnt = false;
		return;
	}
	movingEnt = true;
	Bsp* map = SelectedMap;
	vec3 mapOffset = map->getBspRender()->mapOffset;
	vec3 localCamOrigin = cameraOrigin - mapOffset;
	grabDist = (getEntOrigin(map, map->ents[entIdx[0]]) - localCamOrigin).length();
	grabStartOrigin = localCamOrigin + cameraForward * grabDist;
	grabStartEntOrigin = localCamOrigin + cameraForward * grabDist;
}

void Renderer::cutEnt()
{
	auto ents = pickInfo.selectedEnts;
	if (ents.empty())
		return;

	std::sort(ents.begin(), ents.end());
	std::reverse(ents.begin(), ents.end());

	Bsp* map = SelectedMap;
	if (!map)
		return;

	if (!copiedEnts.empty())
	{
		for (auto& ent : copiedEnts)
		{
			delete ent;
		}
	}
	copiedEnts.clear();

	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i] <= 0)
			continue;
		copiedEnts.push_back(new Entity(*map->ents[ents[i]]));
		DeleteEntityCommand* deleteCommand = new DeleteEntityCommand("Cut Entity", ents[i]);
		map->getBspRender()->pushUndoCommand(deleteCommand);
	}
}

void Renderer::copyEnt()
{
	auto ents = pickInfo.selectedEnts;
	if (ents.empty())
		return;

	std::sort(ents.begin(), ents.end());
	std::reverse(ents.begin(), ents.end());

	Bsp* map = SelectedMap;
	if (!map)
		return;

	if (!copiedEnts.empty())
	{
		for (auto& ent : copiedEnts)
		{
			delete ent;
		}
	}
	copiedEnts.clear();

	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i] <= 0)
			continue;
		copiedEnts.push_back(new Entity(*map->ents[ents[i]]));
	}
}

void Renderer::pasteEnt(bool noModifyOrigin)
{
	if (copiedEnts.empty())
		return;

	Bsp* map = SelectedMap;
	if (!map)
	{
		print_log(get_localized_string(LANG_0925));
		return;
	}

	vec3 baseOrigin = getEntOrigin(map, copiedEnts[0]);

	for (size_t i = 0; i < copiedEnts.size(); i++)
	{
		if (!noModifyOrigin)
		{
			// can't just set camera origin directly because solid ents can have (0,0,0) origins
			vec3 tmpOrigin = getEntOrigin(map, copiedEnts[i]);

			vec3 offset = getEntOrigin(map, copiedEnts[i]) - baseOrigin;

			vec3 modelOffset = getEntOffset(map, copiedEnts[i]);
			vec3 mapOffset = map->getBspRender()->mapOffset;

			vec3 moveDist = (cameraOrigin + cameraForward * 100) - tmpOrigin;
			vec3 newOri = (tmpOrigin + moveDist) - (modelOffset + mapOffset);

			newOri += offset;

			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
			copiedEnts[i]->setOrAddKeyvalue("origin", rounded.toKeyvalueString());
		}

		CreateEntityCommand* createCommand = new CreateEntityCommand("Paste Entity", getSelectedMapId(), copiedEnts[i]);
		map->getBspRender()->pushUndoCommand(createCommand);
	}

	clearSelection();
	selectMap(map);
	selectEnt(map, map->ents.size() > 1 ? ((int)map->ents.size() - 1) : 0);
}

void Renderer::deleteEnt(size_t entIdx)
{
	Bsp* map = SelectedMap;

	if (!map || entIdx <= 0)
		return;
	PickInfo tmpPickInfo = pickInfo;
	DeleteEntityCommand* deleteCommand = new DeleteEntityCommand("Delete Entity", entIdx);
	map->getBspRender()->pushUndoCommand(deleteCommand);
}

void Renderer::deleteEnts()
{
	Bsp* map = SelectedMap;

	if (map && pickInfo.selectedEnts.size() > 0)
	{
		bool reloadbspmdls = false;
		auto tmpEnts = pickInfo.selectedEnts;
		std::sort(tmpEnts.begin(), tmpEnts.end());
		std::reverse(tmpEnts.begin(), tmpEnts.end());


		clearSelection();

		for (auto entIdx : tmpEnts)
		{
			if (map->ents[entIdx]->hasKey("model") &&
				toLowerCase(map->ents[entIdx]->keyvalues["model"]).ends_with(".bsp"))
			{
				reloadbspmdls = true;
			}
			deleteEnt((int)entIdx);
		}

		if (reloadbspmdls)
		{
			reloadBspModels();
		}
	}
}

void Renderer::deselectObject(bool onlyobject)
{
	filterNeeded = true;
	pickInfo.selectedEnts.clear();
	if (!onlyobject)
		pickInfo.selectedFaces.clear();
	isTransformableSolid = false;
	hoverVert = -1;
	hoverEdge = -1;
	updateEntConnections();
}

void Renderer::selectFace(Bsp* map, int face, bool add)
{
	if (!map)
		return;

	if (!add)
	{
		for (auto faceIdx : pickInfo.selectedFaces)
		{
			map->getBspRender()->highlightFace(faceIdx, 0);
		}
		pickInfo.selectedFaces.clear();
	}

	if (face < map->faceCount && face >= 0)
	{
		map->getBspRender()->highlightFace(face, 1);
		pickInfo.selectedFaces.push_back(face);
	}
}

void Renderer::deselectFaces()
{
	Bsp* map = SelectedMap;
	if (!map)
		return;

	for (auto faceIdx : pickInfo.selectedFaces)
	{
		map->getBspRender()->highlightFace(faceIdx, 0);
	}

	pickInfo.selectedFaces.clear();
}

void Renderer::selectEnt(Bsp* map, size_t entIdx, bool add)
{
	if (!map)
		return;

	pickMode = PICK_OBJECT;
	pickInfo.selectedFaces.clear();

	Entity* ent = NULL;
	if (entIdx < map->ents.size())
	{
		ent = map->ents[entIdx];
		if (!add)
		{
			add = true;
			pickInfo.SetSelectedEnt(entIdx);
		}
		else
		{
			if (ent && ent->isWorldSpawn())
			{
				add = false;
			}
			else
			{
				if (!pickInfo.IsSelectedEnt(entIdx))
				{
					pickInfo.AddSelectedEnt(entIdx);
				}
				else
				{
					pickInfo.DelSelectedEnt(entIdx);
				}
			}
		}
	}
	else
	{
		add = true;
		pickInfo.selectedEnts.clear();
	}



	if (add)
	{
		filterNeeded = true;

		updateSelectionSize();
		updateEntConnections();

		map->getBspRender()->saveEntityState(entIdx);
		pickCount++; // force transform window update
	}
}

float magnitude(vec3 vec) {
	return sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);
}

float Angle(vec3 from, vec3 to) {
	//Find the scalar/dot product of the provided 2 Vectors
	float dot = dotProduct(from, to);
	//Find the product of both magnitudes of the vectors then divide dot from it
	dot = dot / (magnitude(from) * magnitude(to));
	//Get the arc cosin of the angle, you now have your angle in radians 
	float arcAcos = acos(dot);
	//Convert to degrees by Multiplying the arc cosin by 180/M_PI
	float angle = arcAcos * 180.0f / HL_PI;
	return angle;
}


void Renderer::goToFace(Bsp* map, int faceIdx)
{
	if (faceIdx < 0 || faceIdx >= map->faceCount)
		return;
	BSPFACE32& face = map->faces[faceIdx];
	if (face.iFirstEdge >= 0 && face.nEdges)
	{
		BSPPLANE plane = map->planes[face.iPlane];
		vec3 planeNormal = face.nPlaneSide ? plane.vNormal * -1 : plane.vNormal;
		float dist = plane.fDist;
		int model = map->get_model_from_face(faceIdx);
		vec3 offset = {};
		if (model >= 0 && model < map->modelCount)
		{
			offset = map->models[model].vOrigin;

			int ent = map->get_ent_from_model(model);
			if (ent > 0 && ent < map->ents.size())
			{
				offset += map->ents[ent]->origin;
			}
		}

		std::vector<vec3> edgeVerts;
		for (int i = 0; i < face.nEdges; i++)
		{
			int edgeIdx = map->surfedges[face.iFirstEdge + i];
			BSPEDGE32& edge = map->edges[abs(edgeIdx)];
			int vertIdx = edgeIdx < 0 ? edge.iVertex[1] : edge.iVertex[0];
			edgeVerts.push_back(map->verts[vertIdx]);
		}

		vec3 center_object = getCenter(edgeVerts) + offset;
		vec3 center_camera = center_object + (planeNormal * 250.0f * (face.nPlaneSide ? -1.0f : 1.0f));

		goToCoords(center_camera.x, center_camera.y, center_camera.z);

		vec3 direction = (center_object - center_camera).flip().normalize();
		float pitch = asin(-direction.y) * 180.0f / HL_PI;
		float yaw = atan2(direction.x, direction.z) * 180.0f / HL_PI;

		cameraAngles = { pitch, 0.0f , yaw };
	}
}
void Renderer::goToCoords(float x, float y, float z)
{
	cameraOrigin.x = x;
	cameraOrigin.y = y;
	cameraOrigin.z = z;
}
void Renderer::goToCoords(const vec3& pos)
{
	cameraOrigin.x = pos.x;
	cameraOrigin.y = pos.y;
	cameraOrigin.z = pos.z;
}

void Renderer::goToEnt(Bsp* map, size_t entIdx)
{
	if (entIdx >= map->ents.size())
		return;

	Entity* ent = map->ents[entIdx];

	vec3 size;
	if (ent->isBspModel())
	{
		BSPMODEL& model = map->models[ent->getBspModelIdx()];
		size = (model.nMaxs - model.nMins) * 0.5f;
	}
	else
	{
		EntCube* cube = pointEntRenderer->getEntCube(ent);
		size = cube->maxs - cube->mins * 0.5f;
	}

	cameraOrigin = getEntOrigin(map, ent) - cameraForward * (size.length() + 64.0f);
}

void Renderer::ungrabEnt()
{
	Bsp* map = SelectedMap;
	auto pickEnts = pickInfo.selectedEnts;
	if (!movingEnt || !map || pickEnts.empty())
	{
		return;
	}
	for (auto& ent : pickEnts)
		map->getBspRender()->pushEntityUndoState("Move Entity", (int)ent);
	movingEnt = false;
}


void Renderer::updateEnts()
{
	Bsp* map = SelectedMap;
	if (map)
	{
		map->getBspRender()->preRenderEnts();
		g_app->updateEntConnections();
		g_app->updateEntConnectionPositions();
	}
}

bool Renderer::isEntTransparent(const char* classname)
{
	if (!classname)
		return false;
	for (auto const& s : g_settings.transparentEntities)
	{
		if (strcasecmp(s.c_str(), classname) == 0)
			return true;
	}
	return false;
}

// now it temporary used for something
Texture* Renderer::giveMeTexture(const std::string& texname)
{
	if (!texname.size())
	{
		return missingTex;
	}

	if (glExteralTextures.find(texname) != glExteralTextures.end())
	{
		return glExteralTextures[texname];
	}

	for (auto& render : mapRenderers)
	{
		for (auto& wad : render->wads)
		{
			if (wad->hasTexture(texname))
			{
				WADTEX* wadTex = wad->readTexture(texname);
				if (wadTex)
				{
					COLOR3* imageData = ConvertWadTexToRGB(wadTex);
					if (imageData)
					{
						Texture* tmpTex = new Texture(wadTex->nWidth, wadTex->nHeight, (unsigned char*)imageData, texname);
						glExteralTextures[texname] = tmpTex;
						delete wadTex;
						return tmpTex;
					}
					delete wadTex;
				}
			}
		}
	}
	return missingTex;
}

