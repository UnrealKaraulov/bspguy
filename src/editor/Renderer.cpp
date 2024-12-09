#include "lang.h"
#include "Renderer.h"
#include "ShaderProgram.h"
#include "primitives.h"
#include "VertexBuffer.h"
#include "shaders.h"
#include "Gui.h"
#include "filedialog/ImFileDialog.h"
#include "lodepng.h"
#include "util.h"
#include "log.h"

#include <chrono>
#include <execution>
#include "NavMesh.h"
#include "LeafNavMesh.h"
#include "Settings.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "quantizer.h"

#include "as.h"

Renderer* g_app = NULL;
std::vector<BspRenderer*> mapRenderers{};

int current_fps = 0;
int ortho_overview = 0;

vec3 ortho_mins(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord), ortho_maxs(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
vec3 ortho_offset = {};
float ortho_near = 1.0f;
float ortho_far = 262144.0f;
float ortho_fov = 10.0f;
float ortho_custom_aspect = 0.0f;
float ortho_custom_w = 0.0f;
float ortho_custom_h = 0.0f;
int ortho_tga_w = 1024;
int ortho_tga_h = 768;
bool ortho_save_tga = false;
bool ortho_save_bmp = false;

// for screenmaker cmd
int make_screenshot = 0;
int make_screenshot_target = 0;
std::string make_screenshot_dir{};

vec2 mousePos;
vec3 cameraOrigin;
vec3 cameraAngles;

int pickCount = 0; // used to give unique IDs to text inputs so switching ents doesn't update keys accidentally
// also used to refresh pick models
int vertPickCount = 0; // used to refresh solid state

int g_drawFrameId = 0;

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
			g_settings.AddRecentFile(tmpPath.string());
			if (ends_with(lowerPath, ".bsp"))
			{
				print_log(get_localized_string(LANG_0896), tmpPath.string());
				g_app->addMap(new Bsp(tmpPath.string()));
			}
			else if (ends_with(lowerPath, ".mdl"))
			{
				print_log(get_localized_string(LANG_0897), tmpPath.string());
				g_app->addMap(new Bsp(tmpPath.string()));
			}
			else if (ends_with(lowerPath, ".spr"))
			{
				print_log(get_localized_string(LANG_0897), tmpPath.string());
				g_app->addMap(new Bsp(tmpPath.string()));
			}
			else if (ends_with(lowerPath, ".csm"))
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
	g_app->is_closing = true;
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

	loadGuiSettings();

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

	//blockMoving = false;

	gui->init();

	g_progress.simpleMode = true;

	pointEntRenderer = new PointEntRenderer(NULL);

	reloading = true;
	fgdFuture = std::async(std::launch::async, &Renderer::loadFgds, this);

	hoverAxis = -1;
	saveTranformResult = false;
	deltaMoveOffset = vec3();
}

Renderer::~Renderer()
{
	print_log(get_localized_string(LANG_0901));
	glfwTerminate();
}

void Renderer::updateWindowTitle(double _curTime)
{
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
				glfwSetWindowTitle(window, fmt::format("bspguy [fps {:>4}] - {}", current_fps, g_limits.engineName + "-" + smallPath).c_str());
			}
		}
	}
}

void Renderer::renderLoop()
{
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gl_max_texture_size);

	if (MAX_LIGHTMAP_ATLAS_SIZE > gl_max_texture_size)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0904), gl_max_texture_size);
		MAX_LIGHTMAP_ATLAS_SIZE = gl_max_texture_size;
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

	int clearcolor = 0;

	while (!glfwWindowShouldClose(window))
	{
		curTime = glfwGetTime();
		if (g_rend_vsync != 0 || std::abs(curTime - framerateTime) > 1.0f / g_settings.fpslimit)
		{
			AS_OnFrameTick(curTime - framerateTime);

			if (SelectedMap && SelectedMap->is_mdl_model)
			{
				if (clearcolor != 1)
				{
					glClearColor(0.25, 0.25, 0.25, 1.0);
					clearcolor = 1;
				}
			}
			else if (!ortho_overview && !make_screenshot)
			{
				if (clearcolor != 2)
				{
					glClearColor(0.0, 0.0, 0.0, 1.0);
					clearcolor = 2;
				}
			}
			else if (ortho_overview)
			{
				if (clearcolor != 3)
				{
					glClearColor(0.0, 1.0, 0.0, 1.0);
					clearcolor = 3;
				}
			}
			else
			{
				if (clearcolor != 3)
				{
					glClearColor(0.0, 0.0, 1.0, 1.0);
					clearcolor = 3;
				}
			}

			if (std::abs(curTime - fpsTime) >= 0.5)
			{
				fpsTime = curTime;
				current_fps = frame_fps * 2;
				frame_fps = 0;
			}

			if (std::abs(curTime - flushConsoleTime) > 0.25)
			{
				flushConsoleTime = curTime;
				FlushConsoleLog();
			}

			mousePos = vec2((float)xpos, (float)ypos);

			GLuint fbo = NULL, texture, rbo;


			if (ortho_save_tga || ortho_save_bmp || (make_screenshot && !isLoading))
			{
				glEnable(GL_MULTISAMPLE);
				glEnable(GL_LINE_SMOOTH);
				glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
				glEnable(GL_POLYGON_SMOOTH);
				glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
				glEnable(GL_POINT_SMOOTH);
				glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

				glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, GL_NICEST);
				

				//for (auto& tex : g_all_Textures)
				//{
				//	tex->upload(tex->type);
				//}


				glGenFramebuffers(1, &fbo);
				glBindFramebuffer(GL_FRAMEBUFFER, fbo);

				glGenTextures(1, &texture);
				glBindTexture(GL_TEXTURE_2D, texture);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, ortho_tga_w, ortho_tga_h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glBindTexture(GL_TEXTURE_2D, 0);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

				glGenRenderbuffers(1, &rbo);
				glBindRenderbuffer(GL_RENDERBUFFER, rbo);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, ortho_tga_w, ortho_tga_h);
				glBindRenderbuffer(GL_RENDERBUFFER, 0);
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
					print_log(PRINT_RED, "Framebuffer not complete!\n");

				glBindFramebuffer(GL_FRAMEBUFFER, fbo);

				if (make_screenshot)
				{
					Bsp* map = SelectedMap;
					if (map)
					{
						int tries = make_screenshot_target + 1;
						BspRenderer* rend = SelectedMap->getBspRender();
						Entity* foundEnt = NULL;

						bool foundCam = false;

						for (auto ent : map->ents)
						{
							if (ent->hasKey("classname") && ent->keyvalues["classname"] == "trigger_camera")
							{
								tries--;
								if (tries <= 0)
								{
									foundCam = true;
									foundEnt = ent;
									break;
								}
							}
						}

						if (!foundEnt)
						{
							for (auto ent : map->ents)
							{
								if (ent->hasKey("classname") && ent->keyvalues["classname"] == "info_player_start")
								{
									tries--;
									if (tries <= 0)
									{
										foundEnt = ent;
										break;
									}
								}
							}
						}

						if (!foundEnt)
						{
							for (auto ent : map->ents)
							{
								if (ent->hasKey("classname") && ent->keyvalues["classname"] == "info_player_deathmatch")
								{
									tries--;
									if (tries <= 0)
									{
										foundEnt = ent;
										break;
									}
								}
							}
						}

						if (!foundEnt)
						{
							for (auto ent : map->ents)
							{
								tries--;
								if (tries <= 0)
								{
									foundEnt = ent;
									break;
								}
							}
						}

						if (foundEnt)
						{
							rend->renderCameraOrigin = foundEnt->origin;
							if (!foundCam)
							{
								rend->renderCameraOrigin.z += 32;
								for (unsigned int i = 0; i < foundEnt->keyOrder.size(); i++)
								{
									if (foundEnt->keyOrder[i] == "angles")
									{
										rend->renderCameraAngles = parseVector(foundEnt->keyvalues["angles"]);
									}
									if (foundEnt->keyOrder[i] == "angle")
									{
										float y = str_to_float(foundEnt->keyvalues["angle"]);

										if (y >= 0.0f)
										{
											rend->renderCameraAngles.y = y;
										}
										else if (y == -1.0f)
										{
											rend->renderCameraAngles.x = -90.0f;
											rend->renderCameraAngles.y = 0.0f;
											rend->renderCameraAngles.z = 0.0f;
										}
										else if (y <= -2.0f)
										{
											rend->renderCameraAngles.x = 90.0f;
											rend->renderCameraAngles.y = 0.0f;
											rend->renderCameraAngles.z = 0.0f;
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
									rend->renderCameraAngles = newAngle;
								}
							}

							rend->renderCameraAngles = rend->renderCameraAngles.flip();
							rend->renderCameraAngles.z = rend->renderCameraAngles.z + 90.0f;
							rend->renderCameraAngles = rend->renderCameraAngles.normalize_angles();
							rend->renderCameraAngles.y = 0.0f;

							cameraAngles = rend->renderCameraAngles;
							cameraOrigin = rend->renderCameraOrigin;
							makeVectors(cameraAngles, cameraForward, cameraRight, cameraUp);
						}
					}
				}
			}



			if (ortho_overview)
			{
				if (ortho_save_tga || ortho_save_bmp)
				{
					setupFakeOrthoView(ortho_tga_w, ortho_tga_h, ortho_mins, ortho_maxs);
				}
				else
				{
					setupFakeOrthoView(0, 0, ortho_mins, ortho_maxs);
				}
			}
			else
			{
				if (make_screenshot)
				{
					setupView(ortho_tga_w, ortho_tga_h);
				}
				else
				{
					setupView();
				}
			}


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
				if ((modelVerts.empty() && modelFaceVerts.empty()) || tmpModelIdx != modelIdx || tmpPickIdx != pickCount)
				{
					updateModelVerts();
				}

				if (pickMode != PICK_OBJECT)
				{
					pickInfo.selectedEnts.clear();
					for (auto& f : pickInfo.selectedFaces)
					{
						int mdl = SelectedMap->get_model_from_face(static_cast<int>(f));
						if (mdl > 0 && mdl < SelectedMap->modelCount)
						{
							int mdl_ent = SelectedMap->get_ent_from_model(mdl);
							if (mdl_ent >= 0 && mdl_ent < static_cast<int>(SelectedMap->ents.size()))
							{
								pickInfo.AddSelectedEnt(mdl_ent);
							}
						}
					}
				}

				updatePickCount = true;
				isTransformableSolid = modelIdx > 0 || (!entIdx.empty() && SelectedMap->ents[entIdx[0]]->getBspModelIdx() < 0);

				if (!isTransformableSolid && !pickInfo.selectedEnts.empty())
				{
					if (ent && ent->hasKey("classname") &&
						ent->keyvalues["classname"] == "worldspawn")
					{
						isTransformableSolid = true;
					}
				}

				isScalingObject = transformMode == TRANSFORM_MODE_SCALE && transformTarget == TRANSFORM_OBJECT;
				isMovingOrigin = transformMode == TRANSFORM_MODE_MOVE && transformTarget == TRANSFORM_ORIGIN && modelIdx >= 0;
				isTransformingValid = (!modelUsesSharedStructures || (transformMode == TRANSFORM_MODE_MOVE && transformTarget != TRANSFORM_VERTEX))
					|| (isTransformableSolid && isScalingObject);
				isTransformingWorld = modelIdx == 0 || (ent && ent->isWorldSpawn());

				invalidSolid = false;

				if (ent && modelIdx != 0)
				{
					invalidSolid = modelIdx > 0 && !SelectedMap->vertex_manipulation_sync(modelIdx, modelVerts, false);
					showDragAxes = transformMode != TRANSFORM_MODE_NONE;
				}
				else
				{
					showDragAxes = false;
				}

				if (modelUsesSharedStructures)
				{
					moveOrigin = true;
				}

				gui->updateTransformWidget = true;
			}

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

				if (SelectedMap->is_mdl_model && SelectedMap->map_mdl)
				{
					matmodel.loadIdentity();
					modelShader->bind();
					modelShader->updateMatrixes();
					if (anyCtrlPressed && !ortho_overview && !make_screenshot)
					{
						if (SelectedMap->map_mdl->mdl_cube && SelectedMap->map_mdl->mdl_cube->axesBuffer)
						{
							SelectedMap->map_mdl->mdl_cube->axesBuffer->drawFull();
						}
					}
					if (anyAltPressed && !ortho_overview && !make_screenshot)
					{
						if (SelectedMap->map_mdl->mdl_cube && SelectedMap->map_mdl->mdl_cube->cubeBuffer)
						{
							SelectedMap->map_mdl->mdl_cube->cubeBuffer->drawFull();
						}
					}
					SelectedMap->map_mdl->DrawMDL();
					continue;
				}

				if (SelectedMap->is_mdl_model && SelectedMap->map_spr)
				{
					matmodel.loadIdentity();
					modelShader->bind();
					modelShader->updateMatrixes();
					if (anyCtrlPressed)
					{
						SelectedMap->map_spr->DrawAxes();
					}
					if (anyAltPressed)
					{
						SelectedMap->map_spr->DrawBBox();
					}
					SelectedMap->map_spr->DrawSprite();
					continue;
				}

				if (SelectedMap->is_mdl_model && SelectedMap->map_csm)
				{
					matmodel.loadIdentity();
					modelShader->bind();
					modelShader->updateMatrixes();
					SelectedMap->map_csm->draw();
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

			if (SelectedMap && !ortho_overview && !make_screenshot)
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
				if (g_render_flags & RENDER_ORIGIN || g_render_flags & RENDER_MAP_BOUNDARY || hasCullbox)
				{
					glDisable(GL_CULL_FACE);
					if (g_render_flags & RENDER_ORIGIN)
					{
						matmodel.loadIdentity();
						vec3 offset = SelectedMap->getBspRender()->mapOffset;
						colorShader->updateMatrixes();
						vec3 p1 = offset + vec3(-10240.0f, 0.0f, 0.0f);
						vec3 p2 = offset + vec3(10240.0f, 0.0f, 0.0f);
						drawLine(p1, p2, { 128, 128, 255, 255 });
						vec3 p3 = offset + vec3(0.0f, -10240.0f, 0.0f);
						vec3 p4 = offset + vec3(0.0f, 10240.0f, 0.0f);
						drawLine(p3, p4, { 0, 0, 255, 255 });
						vec3 p5 = offset + vec3(0.0f, 0.0f, -10240.0f);
						vec3 p6 = offset + vec3(0.0f, 0.0f, 10240.0f);
						drawLine(p5, p6, { 0, 255, 0, 255 });
					}

					if (g_render_flags & RENDER_MAP_BOUNDARY) {
						drawBox(SelectedMap->ents[0]->origin * -1, g_limits.maxMapBoundary * 2, COLOR4(0, 255, 0, 64));
					}

					if (hasCullbox) {
						drawBox(cullMins, cullMaxs, COLOR4(255, 0, 0, 64));
					}
					glEnable(GL_CULL_FACE);
				}
			}


			glDepthMask(GL_FALSE);
			glDepthFunc(GL_ALWAYS);

			if (entConnectionPoints && (g_render_flags & RENDER_ENT_CONNECTIONS) && !ortho_overview && !make_screenshot)
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

			drawingMoveAxes = false;
			drawingScaleAxes = false;

			if (showDragAxes && !ortho_overview && !make_screenshot && pickMode == pick_modes::PICK_OBJECT)
			{
				if (!movingEnt && !isTransformingWorld && entIdx.size() && (isTransformingValid || isMovingOrigin))
				{
					drawTransformAxes();
				}
			}

			if (modelIdx > 0 && !ortho_overview && !make_screenshot && pickMode == PICK_OBJECT)
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

			if (!ortho_overview && !make_screenshot)
			{
				matmodel.loadIdentity();
				colorShader->updateMatrixes();
				GLfloat currentLineWidth;
				glGetFloatv(GL_LINE_WIDTH, &currentLineWidth);
				glDisable(GL_CULL_FACE);
				if (debugTraceStart != vec3())
				{
					glLineWidth(128.0f);
					drawLine(debugLine0, debugLine1, { 255, 0, 0, 255 });
					drawLine(debugTraceStart, debugTrace.vecEndPos, COLOR4(255, 0, 0, 255));
				}

				if (debugNavMesh && debugNavPoly != -1)
				{
					glLineWidth(1);
					NavNode& node = debugNavMesh->nodes[debugNavPoly];
					Polygon3D& poly = debugNavMesh->polys[debugNavPoly];

					for (int i = 0; i < MAX_NAV_LINKS; i++) {
						NavLink& link = node.links[i];
						if (link.node == -1) {
							break;
						}
						Polygon3D& linkPoly = debugNavMesh->polys[link.node];

						vec3 srcMid, dstMid;
						debugNavMesh->getLinkMidPoints(debugNavPoly, i, srcMid, dstMid);

						glDisable(GL_DEPTH_TEST);
						drawLine(poly.center, srcMid, COLOR4(0, 255, 255, 255));
						drawLine(srcMid, dstMid, COLOR4(0, 255, 255, 255));
						drawLine(dstMid, linkPoly.center, COLOR4(0, 255, 255, 255));

						if (fabs(link.zDist) > NAV_STEP_HEIGHT) {
							Bsp* map = mapRenderers[0]->map;
							int n = link.srcEdge;
							int k = link.dstEdge;
							int inext = (n + 1) % poly.verts.size();
							int knext = (k + 1) % linkPoly.verts.size();

							Line2D thisEdge(poly.topdownVerts[n], poly.topdownVerts[inext]);
							Line2D otherEdge(linkPoly.topdownVerts[k], linkPoly.topdownVerts[knext]);

							float t0, t1, t2, t3;
							thisEdge.getOverlapRanges(otherEdge, t0, t1, t2, t3);

							vec3 delta1 = poly.verts[inext] - poly.verts[n];
							vec3 delta2 = linkPoly.verts[knext] - linkPoly.verts[k];
							vec3 e1 = poly.verts[n] + delta1 * t0;
							vec3 e2 = poly.verts[n] + delta1 * t1;
							vec3 e3 = linkPoly.verts[k] + delta2 * t2;
							vec3 e4 = linkPoly.verts[k] + delta2 * t3;

							bool isBelow = link.zDist > 0;
							delta1 = e2 - e1;
							delta2 = e4 - e3;
							vec3 mid1 = e1 + delta1 * 0.5f;
							vec3 mid2 = e3 + delta2 * 0.5f;
							vec3 inwardDir = crossProduct(poly.plane_z, delta1.normalize());
							vec3 testOffset = (isBelow ? inwardDir : inwardDir * -1) + vec3(0, 0, 1.0f);

							float flatLen = (vec2(e2.x, e2.y) - vec2(e1.x, e1.y)).length();
							float stepUnits = 1.0f;
							float step = stepUnits / flatLen;
							TraceResult tr;
							for (float f = 0; f < 0.5f; f += step) {
								vec3 test1 = mid1 + (delta1 * f) + testOffset;
								vec3 test2 = mid2 + (delta2 * f) + testOffset;
								vec3 test3 = mid1 + (delta1 * -f) + testOffset;
								vec3 test4 = mid2 + (delta2 * -f) + testOffset;

								map->traceHull(test1, test2, 3, &tr);
								if (!tr.fAllSolid && !tr.fStartSolid && tr.flFraction > 0.99f) {
									drawLine(test1, test2, COLOR4(255, 255, 0, 255));
								}
								else {
									drawLine(test1, test2, COLOR4(255, 0, 0, 255));
								}

								map->traceHull(test3, test4, 3, &tr);
								if (!tr.fAllSolid && !tr.fStartSolid && tr.flFraction > 0.99f) {
									drawLine(test3, test4, COLOR4(255, 255, 0, 255));
								}
								else {
									drawLine(test3, test4, COLOR4(255, 0, 0, 255));
								}
							}
						}

						glEnable(GL_DEPTH_TEST);
						drawBox(linkPoly.center, 4, COLOR4(0, 255, 255, 255));
					}
				}

				if (debugTraceStart != vec3())
				{
					glLineWidth(currentLineWidth);
				}

				if (debugLeafNavMesh) {
					glDisable(GL_DEPTH_TEST);

					Bsp* map = mapRenderers[0]->map;
					int leafIdx = map->get_leaf(cameraOrigin, 3);
					int leafNavIdx = -1;

					if (leafIdx >= 0 && leafIdx < MAX_MAP_CLIPNODE_LEAVES) {
						leafNavIdx = debugLeafNavMesh->leafMap[leafIdx];
					}

					if (leafNavIdx >= 0 && leafNavIdx < (int)debugLeafNavMesh->nodes.size()) {

						if (pickInfo.selectedEnts.size()) {

							int endNode = debugLeafNavMesh->getNodeIdx(map, map->ents[pickInfo.selectedEnts[0]]);
							//vector<int> route = debugLeafNavMesh->AStarRoute(leafNavIdx, endNode);
							std::vector<int> route = debugLeafNavMesh->dijkstraRoute(leafNavIdx, endNode);

							if (route.size()) {
								LeafNode* lastNode = &debugLeafNavMesh->nodes[route[0]];

								vec3 lastPos = lastNode->origin;
								drawBox(lastNode->origin, 2, COLOR4(0, 255, 255, 255));

								for (size_t i = 1; i < route.size(); i++) {
									LeafNode& node = debugLeafNavMesh->nodes[route[i]];

									vec3 nodeCenter = node.origin;

									for (size_t k = 0; k < lastNode->links.size(); k++) {
										LeafLink& link = lastNode->links[k];

										if (link.node == route[i]) {
											vec3 linkPoint = link.pos;

											if (link.baseCost > 16000) {
												drawLine(lastPos, linkPoint, COLOR4(255, 0, 0, 255));
												drawLine(linkPoint, node.origin, COLOR4(255, 0, 0, 255));
											}
											else if (link.baseCost > 0) {
												drawLine(lastPos, linkPoint, COLOR4(255, 64, 0, 255));
												drawLine(linkPoint, node.origin, COLOR4(255, 64, 0, 255));
											}
											else if (link.costMultiplier > 99.0f) {
												drawLine(lastPos, linkPoint, COLOR4(255, 255, 0, 255));
												drawLine(linkPoint, node.origin, COLOR4(255, 255, 0, 255));
											}
											else if (link.costMultiplier > 9.0f) {
												drawLine(lastPos, linkPoint, COLOR4(255, 0, 255, 255));
												drawLine(linkPoint, node.origin, COLOR4(255, 0, 255, 255));
											}
											else if (link.costMultiplier > 1.9f) {
												drawLine(lastPos, linkPoint, COLOR4(64, 255, 0, 255));
												drawLine(linkPoint, node.origin, COLOR4(64, 255, 0, 255));
											}
											else {
												drawLine(lastPos, linkPoint, COLOR4(0, 255, 255, 255));
												drawLine(linkPoint, node.origin, COLOR4(0, 255, 255, 255));
											}
											drawBox(nodeCenter, 2, COLOR4(0, 255, 255, 255));
											lastPos = nodeCenter;
											break;
										}
									}

									lastNode = &node;
								}
								vec3 lastPosEnd = map->ents[pickInfo.selectedEnts[0]]->getHullOrigin(map);
								drawLine(lastPos, lastPosEnd, COLOR4(0, 255, 255, 255));
							}
						}
						else {
							LeafNode& node = debugLeafNavMesh->nodes[leafNavIdx];

							drawBox(node.origin, 2, COLOR4(0, 255, 0, 255));

							std::string linkStr;

							for (size_t i = 0; i < node.links.size(); i++) {
								LeafLink& link = node.links[i];
								if (link.node == -1) {
									break;
								}
								LeafNode& linkLeaf = debugLeafNavMesh->nodes[link.node];
								Polygon3D& linkArea = link.linkArea;

								if (link.baseCost > 16000) {
									drawLine(node.origin, link.pos, COLOR4(255, 0, 0, 255));
									drawLine(link.pos, linkLeaf.origin, COLOR4(255, 0, 0, 255));
								}
								else if (link.baseCost > 0) {
									drawLine(node.origin, link.pos, COLOR4(255, 128, 0, 255));
									drawLine(link.pos, linkLeaf.origin, COLOR4(255, 128, 0, 255));
								}
								else if (link.costMultiplier > 99.0f) {
									drawLine(node.origin, link.pos, COLOR4(255, 255, 0, 255));
									drawLine(link.pos, linkLeaf.origin, COLOR4(255, 255, 0, 255));
								}
								else if (link.costMultiplier > 9.0f) {
									drawLine(node.origin, link.pos, COLOR4(255, 0, 255, 255));
									drawLine(link.pos, linkLeaf.origin, COLOR4(255, 0, 255, 255));
								}
								else if (link.costMultiplier > 1.9f) {
									drawLine(node.origin, link.pos, COLOR4(64, 255, 0, 255));
									drawLine(link.pos, linkLeaf.origin, COLOR4(64, 255, 0, 255));
								}
								else {
									drawLine(node.origin, link.pos, COLOR4(0, 255, 255, 255));
									drawLine(link.pos, linkLeaf.origin, COLOR4(0, 255, 255, 255));
								}

								for (size_t k = 0; k < linkArea.verts.size(); k++) {
									drawBox(linkArea.verts[k], 1, COLOR4(255, 255, 0, 255));
								}
								drawBox(link.pos, 1, COLOR4(0, 255, 0, 255));
								drawBox(linkLeaf.origin, 2, COLOR4(0, 255, 255, 255));
								linkStr += std::to_string(link.node) + " (" + std::to_string(linkArea.verts.size()) + "v), ";

								/*
								for (int k = 0; k < node.links.size(); k++) {
									if (i == k)
										continue;
									drawLine(link.pos, node.links[k].pos, COLOR4(64, 0, 255, 255));
								}
								*/
							}

							//logf("Leaf node idx: %d, links: %s\n", leafNavIdx, linkStr.c_str());
						}

					}

					/*
					colorShader->pushMatrix(MAT_PROJECTION);
					colorShader->pushMatrix(MAT_VIEW);
					projection.ortho(0, windowWidth, windowHeight, 0, -1.0f, 1.0f);
					view.loadIdentity();
					colorShader->updateMatrixes();

					drawPolygon2D(debugPoly, vec2(800, 100), vec2(500, 500), COLOR4(255, 0, 0, 255));

					colorShader->popMatrix(MAT_PROJECTION);
					colorShader->popMatrix(MAT_VIEW);
					*/
					glEnable(GL_DEPTH_TEST);
				}
				glEnable(GL_CULL_FACE);
			}


			glDepthMask(GL_TRUE);
			glDepthFunc(GL_LESS);

			if (fbo && (ortho_save_tga || ortho_save_bmp || (make_screenshot && !isLoading)))
			{
				std::vector<uint8_t> pixels(3 * ortho_tga_w * ortho_tga_h);
				glBindFramebuffer(GL_FRAMEBUFFER, fbo);
				glReadPixels(0, 0, ortho_tga_w, ortho_tga_h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
				glBindFramebuffer(GL_FRAMEBUFFER, 0);

				for (int line = 0; line != ortho_tga_h / 2; ++line) {
					std::swap_ranges(pixels.begin() + 3 * ortho_tga_w * line,
						pixels.begin() + 3 * ortho_tga_w * (line + 1),
						pixels.begin() + 3 * ortho_tga_w * (ortho_tga_h - line - 1));
				}

				if (ortho_save_tga || (make_screenshot && !isLoading))
				{
					if (make_screenshot)
					{
						std::string screenPath = g_working_dir;


						if (make_screenshot_dir.size() && dirExists(make_screenshot_dir))
						{
							screenPath = make_screenshot_dir;
						}

						screenPath += SelectedMap ? SelectedMap->bsp_name : "screenshot";
						screenPath += "_" + std::to_string(make_screenshot_target + 1) + ".tga";

						stbi_write_tga(screenPath.c_str(), ortho_tga_w, ortho_tga_h, 3, pixels.data());
						print_log("Saved to {} file!\n", screenPath);
					}
					else
					{
						stbi_write_tga((g_working_dir + (SelectedMap ? (SelectedMap->bsp_name + ".tga") : "overview.tga")).c_str(), ortho_tga_w, ortho_tga_h, 3, pixels.data());
						print_log("Saved to {} file!\n", (g_working_dir + "overview.tga"));
					}

					make_screenshot--;
					make_screenshot_target++;


					if (make_screenshot <= 0)
					{
						is_closing = true;
					}
				}
				else
				{
					Quantizer* tmpCQuantizer = new Quantizer(256, 8);
					tmpCQuantizer->ApplyColorTable((COLOR3*)pixels.data(), ortho_tga_w * ortho_tga_h);
					delete tmpCQuantizer;

					int colors = 0;
					COLOR3 palette[256];
					std::vector<unsigned char> indexedPixels(ortho_tga_w * ortho_tga_h);

					for (int y = 0; y < ortho_tga_h; y++)
					{
						for (int x = 0; x < ortho_tga_w; x++)
						{
							int paletteIdx = -1;
							for (int k = 0; k < colors; k++)
							{
								if (pixels[(y * ortho_tga_w + x) * 3] == palette[k].r &&
									pixels[(y * ortho_tga_w + x) * 3 + 1] == palette[k].g &&
									pixels[(y * ortho_tga_w + x) * 3 + 2] == palette[k].b)
								{
									paletteIdx = k;
									break;
								}
							}
							if (paletteIdx == -1)
							{
								if (colors >= 256)
								{
									print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0167));
									delete tmpCQuantizer;
									return;
								}
								palette[colors].r = pixels[(y * ortho_tga_w + x) * 3];
								palette[colors].g = pixels[(y * ortho_tga_w + x) * 3 + 1];
								palette[colors].b = pixels[(y * ortho_tga_w + x) * 3 + 2];
								paletteIdx = colors;
								colors++;
							}
							indexedPixels[y * ortho_tga_w + x] = (unsigned char)paletteIdx;
						}
					}


					WriteBMP_PAL(g_working_dir + (SelectedMap ? (SelectedMap->bsp_name + ".bmp") : "overview.bmp"), indexedPixels.data(), ortho_tga_w, ortho_tga_h, palette);

					print_log("Saved to {} file!\n", (g_working_dir + "overview.bmp").c_str());
				}


				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glDeleteFramebuffers(1, &fbo);
				glDeleteTextures(1, &texture);
				glDeleteRenderbuffers(1, &rbo);
				ortho_save_tga = false;
				ortho_save_bmp = false;
			}

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

			if (std::abs(curTime - mouseTime) >= 0.016)
			{
				if (g_rend_vsync != (g_settings.vsync ? 1 : 0))
				{
					glfwSwapInterval(g_settings.vsync);
					g_rend_vsync = (g_settings.vsync ? 1 : 0);
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

			if (is_closing)
			{
				print_log(get_localized_string(LANG_0901));

#ifdef MINGW 
				std::set_terminate(NULL);
				std::terminate();
#else 
				std::quick_exit(0);
#endif
			}
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
	updateCullBox();

	print_log(get_localized_string(LANG_0908));
}

void Renderer::saveGuiSettings()
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
	g_settings.verboseLogs = g_settings.verboseLogs;
	g_settings.zfar = zFar;
	g_settings.fov = fov;
	g_settings.render_flags = g_render_flags;
	g_settings.fontSize = gui->fontSize;
	g_settings.moveSpeed = moveSpeed;
	g_settings.rotSpeed = rotationSpeed;
}

void Renderer::loadGuiSettings()
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

	g_settings.verboseLogs = g_settings.verboseLogs;
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
	for (size_t i = 0; i < modelVerts.size(); i++, cubeIdx++)
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

		modelVertCubes[cubeIdx] = cCube(min, max, color);
	}

	for (size_t i = 0; i < modelEdges.size(); i++, cubeIdx++)
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
		modelVertCubes[cubeIdx] = cCube(min, max, color);
	}

	matmodel.loadIdentity();
	matmodel.translate(rend->renderOffset.x, rend->renderOffset.y, rend->renderOffset.z);
	colorShader->updateMatrixes();

	modelVertBuff->uploaded = false;
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
	if (showDragAxes && transformMode == TRANSFORM_MODE_SCALE && transformTarget == TRANSFORM_OBJECT)
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
		drawingScaleAxes = true;
	}
	else if (showDragAxes)
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
		drawingMoveAxes = true;
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
	/*if (blockMoving)
	{
		if (!anyCtrlPressed || !pressed[GLFW_KEY_A])
			blockMoving = false;
	}*/

	if (canControl/* && !blockMoving*/)
	{
		/*	if (anyCtrlPressed && anyAltPressed && !oldPressed[GLFW_KEY_A] && pressed[GLFW_KEY_A]
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
			}*/

		cameraOrigin += getMoveDir() * (float)(curTime - oldTime) * moveSpeed;

		if (!ortho_overview && !make_screenshot)
		{
			moveGrabbedEnt();

			vertexEditControls();
		}
		else
		{
			ungrabEnt();
			hoverVert = hoverAxis = hoverEdge = -1;
			showDragAxes = false;
		}

		cameraContextMenus();

		if (!ortho_overview && !make_screenshot)
		{
			cameraRotationControls();
		}

		makeVectors(cameraAngles, cameraForward, cameraRight, cameraUp);

		if (!ortho_overview && !make_screenshot)
		{
			cameraObjectHovering();
		}

		if (!ortho_overview && !make_screenshot)
		{
			cameraPickingControls();
		}

		if (!gui->imgui_io->WantCaptureKeyboard)
		{
			shortcutControls();
			globalShortcutControls();
		}
	}
	else
	{
		if (oldControl/* && !blockMoving*/ && curLeftMouse == GLFW_PRESS)
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
	anyEdgeSelected = anyVertSelected = false;
	for (size_t i = 0; i < modelEdges.size(); i++)
	{
		if (modelEdges[i].selected)
		{
			anyEdgeSelected = true;
		}
	}
	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		if (modelVerts[i].selected)
		{
			anyVertSelected = true;
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
	Bsp* map = SelectedMap;
	auto entIdx = pickInfo.selectedEnts;

	if (curLeftMouse == GLFW_RELEASE && oldLeftMouse == GLFW_RELEASE)
	{
		last_face_idx = -1;
	}

	bool transforming = false;

	if (!pickClickHeld)
	{
		bool canTransform = (curLeftMouse == GLFW_PRESS || oldLeftMouse == GLFW_PRESS) &&
			pickMode == pick_modes::PICK_OBJECT &&
			!movingEnt &&
			!isTransformingWorld &&
			!entIdx.empty() &&
			(isTransformingValid || isMovingOrigin);

		if (canTransform)
		{
			transforming = transformAxisControls();
		}
		else
		{
			saveTranformResult = false;
		}
	}

	if (curLeftMouse == GLFW_PRESS && oldLeftMouse != GLFW_PRESS && !pickClickHeld)
	{
		bool anyHover = (hoverVert != -1) || (hoverEdge != -1);

		if (transformTarget == TRANSFORM_VERTEX && isTransformableSolid && anyHover)
		{
			if (!anyCtrlPressed)
			{
				for (auto& edge : modelEdges)
				{
					edge.selected = false;
				}
				for (auto& vert : modelVerts)
				{
					vert.selected = false;
				}
			}

			if (hoverVert != -1 && hoverVert < static_cast<int>(modelVerts.size()))
			{
				modelVerts[hoverVert].selected = anyCtrlPressed ? !modelVerts[hoverVert].selected : true;
			}
			else if (hoverEdge != -1 && hoverEdge < static_cast<int>(modelEdges.size()))
			{
				modelEdges[hoverEdge].selected = anyCtrlPressed ? !modelEdges[hoverEdge].selected : true;
				for (int i = 0; i < 2; ++i)
				{
					TransformVert& vert = modelVerts[modelEdges[hoverEdge].verts[i]];
					vert.selected = modelEdges[hoverEdge].selected;
				}
			}

			transforming = true;
		}

		if (transformTarget == TRANSFORM_ORIGIN && originHovered)
		{
			originSelected = !originSelected;
			transforming = true;
		}
	}

	// object picking
	if (!transforming && hoverAxis == -1)
	{
		bool shouldPickObject = ((curLeftMouse == GLFW_PRESS && oldLeftMouse == GLFW_RELEASE) ||
			(pickMode != pick_modes::PICK_OBJECT && curLeftMouse == GLFW_PRESS && facePickTime > 0.0 && curTime - facePickTime > 0.05));

		if (shouldPickObject)
		{
			facePickTime = -1.0;
			pickObject();
		}
	}

	if (curLeftMouse != GLFW_PRESS && oldLeftMouse != GLFW_PRESS)
	{
		pickClickHeld = false;
	}

	if (hoverAxis != -1 && curLeftMouse == GLFW_RELEASE && oldLeftMouse == GLFW_PRESS)
	{
		applyTransform(map, true);
	}
}

void Renderer::revertInvalidSolid(Bsp* map, int modelIdx)
{
	for (auto& vert : modelVerts)
	{
		vert.pos = vert.startPos = vert.undoPos;
		if (vert.ptr)
			*vert.ptr = vert.pos;
	}
	for (auto& faceVert : modelFaceVerts)
	{
		faceVert.pos = faceVert.startPos = faceVert.undoPos;
		if (faceVert.ptr)
			*faceVert.ptr = faceVert.pos;
	}
	if (map && modelIdx >= 0)
	{
		map->vertex_manipulation_sync(modelIdx, modelVerts, false);
		auto& model = map->models[modelIdx];
		map->get_model_vertex_bounds(modelIdx, model.nMins, model.nMaxs);
		map->getBspRender()->refreshModel(modelIdx);
	}
	pickCount++;
	vertPickCount++;
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

			if (modelTransform >= 0)
			{
				BSPMODEL& model = map->models[modelTransform];
				map->get_model_vertex_bounds(modelTransform, model.nMins, model.nMaxs);
			}
		}

		updateModelVerts();
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

			totalMouseDrag += vec2(std::fabs(drag.x), std::fabs(drag.y));

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

	if (showDragAxes && transformTarget == TRANSFORM_VERTEX && entIdx.size())
	{
		vec3 pickStart, pickDir;
		getPickRay(pickStart, pickDir);
		PickInfo vertPick = PickInfo();
		vertPick.bestDist = g_limits.fltMaxCoord * 2.0f + 1.0f;

		Entity* ent = map->ents[entIdx[0]];
		vec3 entOrigin = ent->origin;

		hoverEdge = -1;
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

		if (!anyVertSelected && !anyEdgeSelected)
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
	}

	if (hoverEdge != -1 || hoverVert != -1)
		return;

	PickInfo vertPick = PickInfo();
	vertPick.bestDist = g_limits.fltMaxCoord * 2.0f + 1.0f;

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
			axisPick.bestDist = g_limits.fltMaxCoord * 2.0f + 1.0f;

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
	if (!map/* || ortho_overview*/)
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

	/*if (DebugKeyPressed)
	{
		TraceResult& tr = debugTrace;
		mapRenderers[0]->map->traceHull(pickStart, pickStart + pickDir * 512, 1, &tr);
		print_log("Fraction={}, StartSolid={}, AllSolid={}, InOpen={}, PlaneDist={}\nStart=({},{},{}) End=({},{},{}) PlaneNormal=({},{},{})\n",
			tr.flFraction, tr.fStartSolid, tr.fAllSolid, tr.fInOpen, tr.flPlaneDist,
			pickStart.x, pickStart.y, pickStart.z,
			tr.vecEndPos.x, tr.vecEndPos.y, tr.vecEndPos.z,
			tr.vecPlaneNormal.x, tr.vecPlaneNormal.y, tr.vecPlaneNormal.z);
		debugTraceStart = pickStart;
	}
	else
	{

	}*/


	Bsp* oldmap = map;

	PickInfo tmpPickInfo = PickInfo();
	tmpPickInfo.bestDist = g_limits.fltMaxCoord * 2.0f + 1.0f;

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
		vertPickCount++;
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
		else if (curLeftMouse == GLFW_PRESS)
		{
			facePickTime = curTime;
		}

		if (tmpPickInfo.selectedFaces.size() > 0)
		{
			if ((int)tmpPickInfo.selectedFaces[0] != last_face_idx)
			{
				last_face_idx = -1;
				auto it = std::find(pickInfo.selectedFaces.begin(), pickInfo.selectedFaces.end(), tmpPickInfo.selectedFaces[0]);
				if (it == pickInfo.selectedFaces.end())
				{
					map->getBspRender()->highlightFace(tmpPickInfo.selectedFaces[0], 1);
					pickInfo.selectedFaces.push_back(tmpPickInfo.selectedFaces[0]);
				}
				else if (curLeftMouse == GLFW_PRESS && oldLeftMouse == GLFW_RELEASE)
				{
					last_face_idx = (int)tmpPickInfo.selectedFaces[0];
					map->getBspRender()->highlightFace(last_face_idx, 0);
					pickInfo.selectedFaces.erase(it);
					facePickTime = -1.0f;
				}
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

		pickClickHeld = true;

		updateEntConnections();

		pickCount++;
		if (tmpPickEnt.size())
			selectEnt(SelectedMap, tmpPickEnt[0], anyCtrlPressed && tmpPickEnt[0] != 0);
		else if (!anyCtrlPressed)
		{
			pickInfo.selectedEnts.clear();
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

	if (modelIdx <= 0 && transformMode == TRANSFORM_MODE_SCALE)
		transformMode = TRANSFORM_MODE_MOVE;

	if (showDragAxes && !movingEnt && hoverAxis != -1 &&
		curLeftMouse == GLFW_PRESS && oldLeftMouse == GLFW_RELEASE)
	{
		deltaMoveOffset = vec3();
		axisDragEntOriginStart = getEntOrigin(map, ent);
		axisDragStart = getAxisDragPoint(axisDragEntOriginStart);
	}

	bool retval = false;

	if (showDragAxes && !movingEnt && hoverAxis >= 0)
	{
		if (!modelVerts.size() && !modelFaceVerts.size())
		{
			updateModelVerts();
		}

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
							for (int tmpentIdx : pickInfo.selectedEnts)
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
							deltaMoveOffset += delta;
							/*vertPickCount++;
							map->move(delta, modelIdx, true, false, false);
							updateEntConnectionPositions();*/
						}
					}
					else if (transformTarget == TRANSFORM_ORIGIN)
					{
						for (size_t i = 0; i < pickInfo.selectedEnts.size(); i++)
						{
							Entity* tmpent = map->ents[pickInfo.selectedEnts[i]];
							int tmpmdlidx = tmpent->getBspModelIdx();

							if (tmpent->getBspModelIdx() >= 0)
							{
								vec3 neworigin = map->models[tmpmdlidx].vOrigin + delta;
								map->models[tmpmdlidx].vOrigin = neworigin;
								//map->getBspRender()->refreshModel(tmpent->getBspModelIdx());
								map->getBspRender()->refreshEnt((int)pickInfo.selectedEnts[i]);
							}

							vertPickCount++;
						}
					}
				}
				else
				{
					if (modelIdx > 0)
					{
						vec3 scaleDirs[6]{
							vec3(1.0f, 0.0f, 0.0f),
							vec3(0.0f, 1.0f, 0.0f),
							vec3(0.0f, 0.0f, 1.0f),
							vec3(-1.0f, 0.0f, 0.0f),
							vec3(0.0f, -1.0f, 0.0f),
							vec3(0.0f, 0.0f, -1.0f),
						};
						scaleSelectedObject(map, modelIdx, delta, scaleDirs[hoverAxis]);
						map->getBspRender()->refreshModel(modelIdx);
						vertPickCount++;
					}
				}

				retval = true;
			}
		}
	}

	if (curLeftMouse == GLFW_RELEASE && oldLeftMouse == GLFW_PRESS)
	{
		if (saveTranformResult)
		{
			if (transformMode == TRANSFORM_MODE_MOVE)
			{
				if (transformTarget == TRANSFORM_VERTEX && anyVertSelected)
				{
					saveTranformResult = false;
					map->regenerate_clipnodes(modelIdx, -1);
					if (invalidSolid)
					{
						revertInvalidSolid(map, modelIdx);
					}
					else
					{
						map->resize_all_lightmaps();
						map->getBspRender()->refreshModel(modelIdx);
						map->getBspRender()->refreshModelClipnodes(modelIdx);
						applyTransform(map, true);
						map->getBspRender()->pushModelUndoState("Move verts", EDIT_MODEL_LUMPS);
					}
				}
				else if (transformTarget == TRANSFORM_OBJECT)
				{
					if (moveOrigin || modelIdx < 0)
					{
						for (int tmpentIdx : pickInfo.selectedEnts)
						{
							saveTranformResult = false;
							map->getBspRender()->pushEntityUndoState("Move Entity", (int)tmpentIdx);
						}
					}
					else
					{
						saveTranformResult = false;
						vec3 neworigin = gridSnappingEnabled ? snapToGrid(deltaMoveOffset) : deltaMoveOffset;
						if (!neworigin.IsZero())
						{
							map->move(neworigin, modelIdx, true, false, false);
						}
						deltaMoveOffset = vec3();
						map->resize_all_lightmaps();

						applyTransform(map, true);
						map->regenerate_clipnodes(modelIdx, -1);

						map->getBspRender()->refreshEnt((int)entIdx[0]);
						map->getBspRender()->refreshModel(modelIdx);
						map->getBspRender()->refreshModelClipnodes(modelIdx);
						updateEntConnectionPositions();
						map->getBspRender()->pushModelUndoState("Move Model", EDIT_MODEL_LUMPS | FL_ENTITIES);
					}
				}
				else if (transformTarget == TRANSFORM_ORIGIN)
				{
					saveTranformResult = false;
					bool updateModels = false;

					for (size_t i = 0; i < pickInfo.selectedEnts.size(); i++)
					{
						Entity* tmpent = map->ents[pickInfo.selectedEnts[i]];
						int tmpmdlidx = tmpent->getBspModelIdx();

						if (tmpent->getBspModelIdx() >= 0)
						{
							vec3 neworigin = gridSnappingEnabled ? snapToGrid(map->models[tmpmdlidx].vOrigin) : map->models[tmpmdlidx].vOrigin;
							map->models[tmpmdlidx].vOrigin = neworigin;/*
							map->getBspRender()->refreshModel(tmpent->getBspModelIdx());
							map->getBspRender()->refreshEnt((int)pickInfo.selectedEnts[i]);*/
							updateModels = true;
						}

						//pickCount++;
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
						map->resize_all_lightmaps();
						map->getBspRender()->refreshModel(modelIdx);
						map->getBspRender()->refreshModelClipnodes(modelIdx);
						map->getBspRender()->pushModelUndoState("Scale Model", EDIT_MODEL_LUMPS);
					}
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
		AS_OnMapChange();
	}

	// TEMP DEBUG FOR CRASH DETECT
	if (SelectedMap != NULL && !mapRenderers.size())
	{
		if (g_settings.verboseLogs)
		{
			print_log(PRINT_RED, "CRITICAL ERROR! BAD MAP POINTER!!\n");
		}
		SelectedMap = NULL;
		AS_OnMapChange();
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
			AS_OnMapChange();
			return;
		}
	}
	SelectedMap = NULL;
	AS_OnMapChange();
}

void Renderer::selectMap(Bsp* map)
{
	SelectedMap = map;
	AS_OnMapChange();
}

void Renderer::deselectMap()
{
	SelectedMap = NULL;
	AS_OnMapChange();
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

void Renderer::setupFakeOrthoView(int forceW, int forceH, vec3 bboxMin, vec3 bboxMax)
{
	if (forceW == 0 || forceH == 0)
		glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
	else
	{
		windowWidth = forceW;
		windowHeight = forceH;
	}

	glViewport(0, 0, windowWidth, windowHeight);

	float aspect = (float)windowWidth / (float)windowHeight;

	if (std::fabs(ortho_custom_aspect) > EPSILON)
		aspect = ortho_custom_aspect;

	float bboxWidth = bboxMax.x - bboxMin.x;
	float bboxHeight = bboxMax.y - bboxMin.y;

	float newZNear = ortho_near / (2.0f * tan(ortho_fov * HL_PI / 360.0f));
	float newZFar = ortho_far / (2.0f * tan(ortho_fov * HL_PI / 360.0f));

	projection.perspective(ortho_fov, aspect, newZNear, newZFar);

	vec3 center = getCenter(bboxMin, bboxMax) + ortho_offset;

	float distanceX = bboxWidth / (2.0f * tan(ortho_fov * HL_PI / 360.0f));
	float distanceY = bboxHeight / (2.0f * tan(ortho_fov * HL_PI / 360.0f));
	float distance = std::max(distanceX, distanceY);

	matview.loadIdentity();
	matview.rotateX(90.0f * HL_PI / 180.0f);

	if (bboxWidth < bboxHeight)
	{
		matview.rotateY(90.0f * HL_PI / 180.0f);
	}

	matview.translate(-center.x, -distance, center.y);

	cameraAngles.x = 90.0f;
	cameraAngles.y = cameraAngles.z = 0.0f;

	if (bboxWidth < bboxHeight)
	{
		cameraAngles.z = 90.0f;
	}

	cameraOrigin.x = center.x;
	cameraOrigin.y = center.y;

	distance = std::max(bboxWidth, bboxHeight) / (2.0f * tan(this->fov * HL_PI / 360.0f));

	cameraOrigin.z = distance;
}


void Renderer::setupView(int forceW, int forceH)
{
	if (forceW == 0 || forceH == 0)
		glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
	else
	{
		windowWidth = forceW;
		windowHeight = forceH;
	}

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
					if (ends_with(toLowerCase(modelPath), ".bsp"))
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

	updateCullBox();

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

void Renderer::drawLine2D(vec2 start, vec2 end, COLOR4 color) {
	line_verts[0].pos = vec3(start.x, start.y, 0.0f).flip();
	line_verts[0].c = color;

	line_verts[1].pos = vec3(end.x, end.y, 0.0f).flip();
	line_verts[1].c = color;

	lineBuf->uploaded = false;
	lineBuf->drawFull();
}

void Renderer::drawBox(vec3 center, float width, COLOR4 color) {
	width *= 0.5f;
	vec3 sz = vec3(width, width, width);
	vec3 pos = vec3(center.x, center.z, -center.y);
	cCube cube(pos - sz, pos + sz, color);
	VertexBuffer buffer(g_app->colorShader, &cube, 6 * 6, GL_TRIANGLES);
	buffer.drawFull();
}

void Renderer::drawBox(vec3 mins, vec3 maxs, COLOR4 color) {
	mins = vec3(mins.x, mins.z, -mins.y);
	maxs = vec3(maxs.x, maxs.z, -maxs.y);

	cCube cube(mins, maxs, color);

	VertexBuffer buffer(g_app->colorShader, &cube, 6 * 6, GL_TRIANGLES);
	buffer.drawFull();
}

void Renderer::drawPolygon3D(Polygon3D& poly, COLOR4 color) {
	static cVert verts[64];

	for (size_t i = 0; i < poly.verts.size() && i < 64; i++) {
		vec3 pos = poly.verts[i];
		verts[i].pos = vec3(pos.x, pos.z, -pos.y);
		verts[i].c = color;
	}

	VertexBuffer buffer(g_app->colorShader, verts, (int)poly.verts.size(), GL_TRIANGLE_FAN);
	buffer.drawFull();
}

float Renderer::drawPolygon2D(Polygon3D poly, vec2 pos, vec2 maxSz, COLOR4 color) {
	vec2 sz = poly.localMaxs - poly.localMins;
	float scale = std::min(maxSz.y / sz.y, maxSz.x / sz.x);

	vec2 offset = poly.localMins * -scale + pos;

	for (size_t i = 0; i < poly.verts.size(); i++) {
		vec2 v1 = poly.localVerts[i];
		vec2 v2 = poly.localVerts[(i + 1) % poly.verts.size()];
		drawLine2D(offset + v1 * scale, offset + v2 * scale, color);
		if (i == 0) {
			drawLine2D(offset + v1 * scale, offset + (v1 + (v2 - v1) * 0.5f) * scale, COLOR4(0, 255, 0, 255));
		}
	}

	// draw camera origin in the same coordinate space
	{
		vec2 cam = poly.project(cameraOrigin);
		drawBox2D(offset + cam * scale, 16, poly.isInside(cam) ? COLOR4(0, 255, 0, 255) : COLOR4(255, 32, 0, 255));
	}


	return scale;
}

void Renderer::drawBox2D(vec2 center, float width, COLOR4 color) {
	vec2 pos = vec2(center.x, center.y) - vec2(width * 0.5f, width * 0.5f);
	cQuad cube(pos.x, pos.y, width, width, color);

	VertexBuffer buffer(g_app->colorShader, &cube, 6, GL_TRIANGLES);
	buffer.drawFull();
}



void Renderer::drawPlane(BSPPLANE& plane, COLOR4 color, vec3 offset)
{
	vec3 ori = offset + plane.vNormal * plane.fDist;
	vec3 crossDir = std::fabs(plane.vNormal.z) > 0.9f ? vec3(1.0f, 0.0f, 0.0f) : vec3(0.0f, 0.0f, 1.0f);
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
			drawClipnodes(map, node.iChildren[i], currentPlane, activePlane, offset);
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
			drawNodes(map, node.iChildren[i], currentPlane, activePlane, offset);
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
		int mdl = ent->getBspModelIdx();
		if (mdl >= 0 && mdl < map->modelCount)
		{
			BSPMODEL& tmodel = map->models[ent->getBspModelIdx()];
			return tmodel.nMins + (tmodel.nMaxs - tmodel.nMins) * 0.5f;
		}
	}
	return vec3();
}

void Renderer::updateDragAxes()
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
			entMin = vec3(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
			entMax = vec3(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);

			map->get_model_vertex_bounds(modelIdx, entMin, entMax);

			vec3 modelOrigin = entMin + (entMax - entMin) * 0.5f;

			entMax -= modelOrigin;
			entMin -= modelOrigin;

			scaleAxes.origin = modelOrigin;
			scaleAxes.origin += ent->origin;
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
					/*entMin = vec3(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
					entMax = vec3(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);

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

				moveAxes.origin += deltaMoveOffset;
			}
			else
			{
				moveAxes.origin = getEntOrigin(map, ent);
				moveAxes.origin += deltaMoveOffset;
			}
		}

		if (entIdx.empty())
		{
			moveAxes.origin -= mapOffset;
		}

		if (transformTarget == TRANSFORM_VERTEX)
		{
			vec3 entOrigin = ent ? ent->origin : vec3();
			vec3 min(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
			vec3 max(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);
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
				moveAxes.origin += deltaMoveOffset;
			}
		}
	}

	// create the meshes
	if (transformMode == TRANSFORM_MODE_SCALE)
	{
		float baseScale = (scaleAxes.origin - localCameraOrigin).length() * 0.005f;
		float s = baseScale;
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
		dots[i] = std::fabs(dotProduct(cameraForward, axisNormals[i]));
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
		return;
	}

	//map->getBspRender()->refreshModel(modelIdx);

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
	modelVertBuff = new VertexBuffer(colorShader, modelVertCubes, (6 * 6 * (int)numCubes), GL_TRIANGLES);
	updateSelectionSize(map, modelIdx);

	//print_log(get_localized_string(LANG_0913),modelVerts.size());
}

void Renderer::updateSelectionSize(Bsp* map, int modelIdx)
{
	selectionSize = vec3();
	if (!map)
	{
		return;
	}
	if (modelIdx <= 0)
	{
		vec3 mins, maxs;
		map->get_bounding_box(mins, maxs);
		selectionSize = maxs - mins;
	}
	else
	{
		vec3 mins, maxs;
		//map->get_model_vertex_bounds(modelIdx, mins, maxs);
		map->get_bounding_box(modelIdx, mins, maxs);
		selectionSize = maxs - mins;
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

	entConnections = new VertexBuffer(colorShader, lines, (int)numVerts, GL_LINES);
	entConnectionPoints = new VertexBuffer(colorShader, points, ((int)(numPoints) * 6 * 6), GL_TRIANGLES);
	entConnections->ownData = true;
	entConnectionPoints->ownData = true;
	updateCullBox();
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

	updateCullBox();
}

void Renderer::updateCullBox() {
	if (!mapRenderers.size()) {
		hasCullbox = false;
		return;
	}

	Bsp* map = mapRenderers[0]->map;

	cullMins = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	cullMaxs = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	int findCount = 0;
	for (Entity* ent : map->ents) {
		if (ent->hasKey("classname") && ent->keyvalues["classname"] == "cull") {
			expandBoundingBox(ent->origin, cullMins, cullMaxs);
			findCount++;
		}
	}

	hasCullbox = findCount > 1;
}

bool Renderer::getModelSolid(std::vector<TransformVert>& hullVerts, Bsp* map, Solid& outSolid)
{
	outSolid.faces.clear();
	outSolid.hullEdges.clear();
	outSolid.hullVerts.clear();
	outSolid.hullVerts = hullVerts;

	// get verts for each plane
	std::map<int, std::vector<int>> planeVerts;
	for (int i = 0; i < (int)hullVerts.size(); i++)
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
		std::vector<int> verts = it->second;
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

		std::vector<int> orderedVerts = getSortedPlanarVertOrder(tempVerts);
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
				if (std::fabs(dist) < ON_EPSILON)
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

void Renderer::scaleSelectedObject(Bsp* map, int modelIdx, float x, float y, float z)
{
	/*vec3 minDist;
	vec3 maxDist;

	if (map)
	{
		map->get_model_vertex_bounds(modelTransform, minDist, maxDist);
	}
	else
	{
		for (size_t i = 0; i < modelVerts.size(); i++)
		{
			vec3 v = modelVerts[i].startPos;
			expandBoundingBox(v, minDist, maxDist);
		}
	}

	vec3 distRange = maxDist - minDist;*/

	vec3 dir;
	dir.x = x;
	dir.y = y;
	dir.z = z;

	scaleSelectedObject(map, modelIdx, dir, vec3());
}

void Renderer::scaleSelectedObject(Bsp* map, int modelIdx, vec3 dir, const vec3& fromDir, bool logging)
{
	bool scaleFromOrigin = std::fabs(fromDir.x) < EPSILON && std::fabs(fromDir.y) < EPSILON && std::fabs(fromDir.z) < EPSILON;

	vec3 minDist(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
	vec3 maxDist(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);

	for (const auto& vert : modelVerts)
	{
		expandBoundingBox(vert.startPos, minDist, maxDist);
	}
	for (const auto& faceVert : modelFaceVerts)
	{
		expandBoundingBox(faceVert.startPos, minDist, maxDist);
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

	// Scale planes
	for (auto& vert : modelVerts)
	{
		vec3 stretchFactor = (vert.startPos - scaleFromDist) / distRange;
		vert.pos += dir * stretchFactor;
		if (gridSnappingEnabled)
		{
			vert.pos = snapToGrid(vert.pos);
		}
	}

	// Scale visible faces
	for (auto& faceVert : modelFaceVerts)
	{
		vec3 stretchFactor = (faceVert.startPos - scaleFromDist) / distRange;
		faceVert.pos += dir * stretchFactor;
		if (gridSnappingEnabled)
		{
			faceVert.pos = snapToGrid(faceVert.pos);
		}
		if (faceVert.ptr)
			*faceVert.ptr = faceVert.pos;
	}

	map->vertex_manipulation_sync(modelTransform, modelVerts, false);

	if (textureLock)
	{
		minDist = vec3(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
		maxDist = vec3(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);

		for (const auto& faceVert : modelFaceVerts)
		{
			expandBoundingBox(faceVert.pos, minDist, maxDist);
		}
		vec3 newDistRange = maxDist - minDist;
		vec3 scaleFactor = distRange / newDistRange;

		mat4x4 scaleMat;
		scaleMat.loadIdentity();
		scaleMat.scale(scaleFactor.x, scaleFactor.y, scaleFactor.z);

		for (auto& oldinfo : scaleTexinfos)
		{
			BSPTEXTUREINFO& info = map->texinfos[oldinfo.texinfoIdx];

			info.vS = (scaleMat * vec4(oldinfo.oldS, 1)).xyz();
			info.vT = (scaleMat * vec4(oldinfo.oldT, 1)).xyz();

			float shiftS = oldinfo.oldShiftS;
			float shiftT = oldinfo.oldShiftT;

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

				shiftS += (refDist * vsdiff * std::fabs(dotS)) * dotSm;
				shiftT += (refDist * vtdiff * std::fabs(dotT)) * dotTm;
			}

			info.shiftS = shiftS;
			info.shiftT = shiftT;
		}
	}

	updateSelectionSize(map, modelIdx);

}

void Renderer::moveSelectedVerts(const vec3& delta)
{
	if (!SelectedMap)
	{
		print_log(get_localized_string(LANG_0924));
		return;
	}

	for (size_t i = 0; i < modelVerts.size(); i++)
	{
		if (modelVerts[i].selected)
		{
			modelVerts[i].pos += delta;
			/*
			if (gridSnappingEnabled)
				modelVerts[i].pos = snapToGrid(modelVerts[i].pos);*/
			if (modelVerts[i].ptr)
				*modelVerts[i].ptr = modelVerts[i].pos;
		}
	}

	Bsp* map = SelectedMap;
	auto entIdx = pickInfo.selectedEnts;
	if (entIdx.size())
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
	std::vector<int> selectedEdges;
	for (int i = 0; i < (int)modelEdges.size(); i++)
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
	for (int i = 0; i < 2 && commonPlane == -1; i++)
	{
		int thisPlane = edge1.planes[i];
		for (int k = 0; k < 2; k++)
		{
			int otherPlane = edge2.planes[k];
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
		int i0 = edge1.verts[0];
		int i1 = edge1.verts[1];
		int i2 = edge2.verts[0];
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
			float bestdot = -g_limits.fltMaxCoord;
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


	map->resize_all_lightmaps();
	mapRenderer->loadLightmaps();
	mapRenderer->refreshModel(modelIdx);
	pickCount++;
	vertPickCount++;
	map->getBspRender()->pushModelUndoState("Split Face", EDIT_MODEL_LUMPS);
	return true;
}

void Renderer::scaleSelectedVerts(Bsp* map, int modelIdx, float x, float y, float z)
{
	if (!map)
	{
		print_log(get_localized_string(LANG_0924));
		return;
	}

	TransformAxes& activeAxes = *(transformMode == TRANSFORM_MODE_SCALE ? &scaleAxes : &moveAxes);
	vec3 fromOrigin = activeAxes.origin;

	vec3 min(g_limits.fltMaxCoord, g_limits.fltMaxCoord, g_limits.fltMaxCoord);
	vec3 max(-g_limits.fltMaxCoord, -g_limits.fltMaxCoord, -g_limits.fltMaxCoord);
	int selectTotal = 0;
	for (const auto& vert : modelVerts)
	{
		if (vert.selected)
		{
			vec3 v = vert.pos;
			min.x = std::min(min.x, v.x);
			min.y = std::min(min.y, v.y);
			min.z = std::min(min.z, v.z);
			max.x = std::max(max.x, v.x);
			max.y = std::max(max.y, v.y);
			max.z = std::max(max.z, v.z);
			selectTotal++;
		}
	}
	if (selectTotal != 0)
		fromOrigin = min + (max - min) * 0.5f;

	debugVec1 = fromOrigin;

	for (auto& vert : modelVerts)
	{
		if (vert.selected)
		{
			vec3 delta = vert.startPos - fromOrigin;
			vert.pos = fromOrigin + delta * vec3(x, y, z);
			if (gridSnappingEnabled)
				vert.pos = snapToGrid(vert.pos);
			if (vert.ptr)
				*vert.ptr = vert.pos;
		}
	}

	map->vertex_manipulation_sync(modelTransform, modelVerts, false);
	map->getBspRender()->refreshModel(modelIdx);
	updateSelectionSize(map, modelIdx);
}

vec3 Renderer::snapToGrid(vec3 pos)
{
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

	std::ostringstream ss;

	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i] <= 0)
			continue;
		ss << map->ents[ents[i]]->serialize();

		if (map->ents[ents[i]]->getBspModelIdx() > 0)
		{
			removeFile(g_working_dir + "copyModel" + std::to_string(map->ents[ents[i]]->getBspModelIdx()) + ".bsp");
			ExportModel(map, g_working_dir + "copyModel" + std::to_string(map->ents[ents[i]]->getBspModelIdx()) + ".bsp", map->ents[ents[i]]->getBspModelIdx(), 2, true);
		}

		DeleteEntityCommand* deleteCommand = new DeleteEntityCommand("Cut Entity", ents[i]);
		map->getBspRender()->pushUndoCommand(deleteCommand);
	}

	ImGui::SetClipboardText(ss.str().c_str());
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

	std::ostringstream ss;

	for (size_t i = 0; i < ents.size(); i++)
	{
		if (ents[i] <= 0)
			continue;
		ss << map->ents[ents[i]]->serialize();
		if (map->ents[ents[i]]->getBspModelIdx() > 0)
		{
			ExportModel(map, g_working_dir + "copyModel" + std::to_string(map->ents[ents[i]]->getBspModelIdx()) + ".bsp", map->ents[ents[i]]->getBspModelIdx(), 2, true);
		}
	}

	ImGui::SetClipboardText(ss.str().c_str());
}

void Renderer::pasteEnt(bool noModifyOrigin, bool copyModel)
{
	auto clipboardText = ImGui::GetClipboardText();
	if (!clipboardText)
		return;

	Bsp* map = SelectedMap;
	if (!map)
	{
		print_log(get_localized_string(LANG_0925));
		return;
	}

	BspRenderer* rend = map->getBspRender();

	std::vector<Entity*> copiedEnts;
	std::istringstream in(clipboardText);

	int lineNum = 0;
	int lastBracket = -1;
	Entity* ent = NULL;
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

			if (ent != NULL)
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
				copiedEnts.push_back(ent);
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
			for (size_t k = 0; k < keyvals.keys.size(); k++)
			{
				if (keyvals.keys[k].length() && keyvals.values[k].length())
					ent->addKeyvalue(keyvals.keys[k], keyvals.values[k]);
			}
		}
	}

	clearSelection();
	selectMap(map);

	for (size_t i = 0; i < copiedEnts.size(); i++)
	{
		if (copiedEnts[i]->getBspModelIdxForce() > 0 && copyModel)
		{
			int mdlIdx = ImportModel(map, g_working_dir + "copyModel" + std::to_string(copiedEnts[i]->getBspModelIdx()) + ".bsp");
			if (mdlIdx > 0)
			{
				copiedEnts[i]->setOrAddKeyvalue("model", "*" + std::to_string(mdlIdx));
			}
			rend->refreshModelClipnodes(mdlIdx);
		}

		vec3 baseOrigin = copyModel ? copiedEnts[0]->origin : getEntOffset(map, copiedEnts[0]);

		if (!noModifyOrigin)
		{
			vec3 entOrigin = copyModel ? copiedEnts[i]->origin : getEntOrigin(map, copiedEnts[i]);
			vec3 offset = entOrigin - baseOrigin;
			vec3 mapOffset = map->getBspRender()->mapOffset;
			vec3 moveDist = (cameraOrigin + cameraForward * 100) - entOrigin;
			vec3 newOri = (entOrigin + moveDist) - (entOrigin + mapOffset);

			newOri += offset;

			vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
			copiedEnts[i]->setOrAddKeyvalue("origin", rounded.toKeyvalueString());
		}

		CreateEntityCommand* createCommand = new CreateEntityCommand("Paste Entity", getSelectedMapId(), copiedEnts[i]);
		map->getBspRender()->pushUndoCommand(createCommand);
		selectEnt(map, map->ents.size() > 1 ? ((int)map->ents.size() - 1) : 0, true);
	}
}

void Renderer::pasteEntsFromText(std::string text) {
	Bsp* map = getSelectedMap();
	if (!map)
	{
		return;
	}
	BspRenderer* rend = map->getBspRender();

	CreateEntityFromTextCommand* createCommand =
		new CreateEntityFromTextCommand("Paste entities from clipboard", getSelectedMapId(), text);
	rend->pushUndoCommand(createCommand);

	if (createCommand->createdEnts == 1) {
		Entity* createdEnt = map->ents[map->ents.size() - 1];
		vec3 oldOrigin = getEntOrigin(map, createdEnt);
		vec3 modelOffset = getEntOffset(map, createdEnt);
		vec3 mapOffset = rend->mapOffset;

		vec3 moveDist = (cameraOrigin + cameraForward * 100) - oldOrigin;
		vec3 newOri = (oldOrigin + moveDist) - (modelOffset + mapOffset);
		vec3 rounded = gridSnappingEnabled ? snapToGrid(newOri) : newOri;
		createdEnt->setOrAddKeyvalue("origin", rounded.toKeyvalueString(!gridSnappingEnabled));
		createCommand->refresh();
	}

	if (map->ents.size() > 0)
		selectEnt(map, (int)map->ents.size() - 1);
}

void Renderer::deleteEnt(int entIdx)
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
				ends_with(toLowerCase(map->ents[entIdx]->keyvalues["model"]), ".bsp"))
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

void Renderer::selectEnt(Bsp* map, int entIdx, bool add)
{
	if (!map)
		return;

	pickMode = PICK_OBJECT;
	pickInfo.selectedFaces.clear();

	Entity* ent = NULL;
	if (entIdx < (int)map->ents.size())
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
		add = false;
		pickInfo.selectedEnts.clear();
	}

	if (add)
	{
		int modelIdx = map->ents[pickInfo.selectedEnts[0]]->getBspModelIdx();
		if (modelIdx > 0)
		{
			modelUsesSharedStructures = SelectedMap->does_model_use_shared_structures(modelIdx);
			updateSelectionSize(map, modelIdx);
		}
		filterNeeded = true;
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

		planeNormal = planeNormal.normalize();

		int model = map->get_model_from_face(faceIdx);

		bool flip = false;

		vec3 offset = {};
		if (model >= 0 && model < map->modelCount)
		{
			int ent = map->get_ent_from_model(model);
			if (ent > 0 && ent < (int)map->ents.size())
			{
				offset = map->ents[ent]->origin;
				flip = true;
			}
		}

		std::vector<vec3> edgeVerts;
		for (int i = 0; i < face.nEdges; i++)
		{
			int edgeIdx = map->surfedges[face.iFirstEdge + i];
			BSPEDGE32& edge = map->edges[abs(edgeIdx)];
			int vertIdx = edgeIdx > 0 ? edge.iVertex[0] : edge.iVertex[1];
			edgeVerts.push_back(map->verts[vertIdx]);
		}

		vec3 center_object = getCentroid(edgeVerts) + offset;

		vec3 center_camera = center_object + (planeNormal * 200.0f);

		goToCoords(center_camera.x, center_camera.y, center_camera.z);

		vec3 direction = (center_camera - center_object).normalize();

		direction = direction.flip();

		float pitch = asin(direction.y) * (180.0f / HL_PI);
		float yaw = atan2(direction.x, direction.z) * (180.0f / HL_PI);

		if (flip)
			yaw *= -1.0f;

		cameraAngles = { pitch, 0.0f , yaw };
		cameraAngles = cameraAngles.normalize_angles();
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

void Renderer::goToEnt(Bsp* map, int entIdx)
{
	if (entIdx >= (int)map->ents.size())
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
	if (!movingEnt)
		return;
	Bsp* map = SelectedMap;
	auto pickEnts = pickInfo.selectedEnts;
	if (!map || pickEnts.empty())
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
Texture* Renderer::giveMeTexture(const std::string& texname, const std::string& wadpart)
{
	if (!texname.size())
	{
		return missingTex;
	}

	auto it = std::find(glExteralTextures_names.begin(), glExteralTextures_names.end(), texname);
	if (it != glExteralTextures_names.end())
	{
		auto idx = std::distance(glExteralTextures_names.begin(), it);
		if (wadpart.empty())
		{
			return glExteralTextures_textures[idx];
		}
		else
		{
			std::string lowerWadPart = toLowerCase(wadpart);

			if (glExteralTextures_wads[idx].find(lowerWadPart) != std::string::npos)
			{
				return glExteralTextures_textures[idx];
			}
			else
			{
				++it;
				while (it != glExteralTextures_names.end())
				{
					idx = std::distance(glExteralTextures_names.begin(), it);
					if (glExteralTextures_wads[idx].find(toLowerCase(wadpart)) != std::string::npos)
					{
						return glExteralTextures_textures[idx];
					}
					it = std::find(std::next(it), glExteralTextures_names.end(), texname);
				}
			}
		}
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
						glExteralTextures_names.emplace_back(texname);
						glExteralTextures_wads.emplace_back(toLowerCase(wad->wadname));
						glExteralTextures_textures.emplace_back(tmpTex);
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

void Renderer::merge(std::string fpath)
{
	Bsp* thismap = SelectedMap;
	if (!thismap)
		return;

	thismap->update_ent_lump();

	Bsp* map2 = new Bsp(fpath);
	Bsp* thisCopy = new Bsp(*thismap);

	if (!map2->bsp_valid) {
		delete map2;
		print_log("Merge aborted because the BSP load failed.\n");
		return;
	}

	std::vector<Bsp*> maps;

	maps.push_back(thisCopy);
	maps.push_back(map2);

	BspMerger merger;
	mergeResult = merger.merge(maps, vec3(), thismap->bsp_name, true, true, true, false);

	if (!mergeResult.map || !mergeResult.map->bsp_valid) {
		delete map2;
		if (mergeResult.map)
			delete mergeResult.map;

		mergeResult.map = NULL;
		return;
	}

	if (mergeResult.overflow) {
		return; // map deleted later in gui modal, after displaying limit overflows
	}

	mapRenderers.clear();
	addMap(mergeResult.map);

	gui->refresh();
	updateCullBox();

	print_log("Merged maps!\n");
}

bool Renderer::hasCopiedEnt()
{
	auto clipText = ImGui::GetClipboardText();
	if (clipText && clipText[0] == '{')
	{
		return true;
	}
	return false;
}