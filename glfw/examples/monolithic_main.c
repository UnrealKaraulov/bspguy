
#include "monolithic_examples.h"

#define USAGE_NAME   "glfw_tests"

#include "monolithic_main_internal_defs.h"

MONOLITHIC_CMD_TABLE_START()

	{ "glfw_allocator", {.fa = glfw_allocator_test_main } },
	{ "glfw_boing", {.fa = glfw_boing_example_main } },
	{ "glfw_clipboard", {.fa = glfw_clipboard_test_main } },
	{ "glfw_cursor", {.fa = glfw_cursor_test_main } },
	{ "glfw_empty", {.fa = glfw_empty_test_main } },
	{ "glfw_events", {.fa = glfw_events_test_main } },
	{ "glfw_gamma", {.fa = glfw_gamma_test_main } },
	{ "glfw_gears", {.fa = glfw_gears_example_main } },
	{ "glfw_glfwindow", {.fa = glfw_glfwindow_test_main } },
	{ "glfwinfo", {.fa = glfw_glfwinfo_test_main } },
	{ "glfw_heightmap", {.fa = glfw_heightmap_example_main } },
	{ "glfw_icon", {.fa = glfw_icon_test_main } },
	{ "glfw_iconify", {.fa = glfw_iconify_test_main } },
	{ "glfw_inputlag", {.fa = glfw_inputlag_test_main } },
	{ "glfw_joysticks", {.fa = glfw_joysticks_test_main } },
	{ "glfw_monitors", {.fa = glfw_monitors_test_main } },
	{ "glfw_msaa", {.fa = glfw_msaa_test_main } },
	{ "glfw_offscreen", {.fa = glfw_offscreen_example_main } },
	{ "glfw_particles", {.fa = glfw_particles_example_main } },
	{ "glfw_reopen", {.fa = glfw_reopen_test_main } },
	{ "glfw_sharing", {.fa = glfw_sharing_example_main } },
	{ "glfw_splitview", {.fa = glfw_splitview_example_main } },
	{ "glfw_tearing", {.fa = glfw_tearing_test_main } },
	{ "glfw_threads", {.fa = glfw_threads_test_main } },
	{ "glfw_timeout", {.fa = glfw_timeout_test_main } },
	{ "glfw_title", {.fa = glfw_title_test_main } },
	{ "glfw_triangle_opengl", {.fa = glfw_triangle_opengl_example_main } },
	{ "glfw_triangle_opengles", {.fa = glfw_triangle_opengles_example_main } },
	{ "glfw_triangle_vulkan", {.fa = glfw_triangle_vulkan_example_main } },
	{ "glfw_wave", {.fa = glfw_wave_example_main } },
	{ "glfw_windows", {.fa = glfw_windows_example_main } },

MONOLITHIC_CMD_TABLE_END();

#define MONOLITHIC_SUBCLUSTER_MAIN  glfw_monolithic_subcluster_main

#include "monolithic_main_tpl.h"
