
#pragma once

#if defined(BUILD_MONOLITHIC)

#ifdef __cplusplus
extern "C" {
#endif

	int glfw_boing_example_main(int argc, const char** argv);
	int glfw_gears_example_main(int argc, const char** argv);
	int glfw_heightmap_example_main(int argc, const char** argv);
	int glfw_offscreen_example_main(int argc, const char** argv);
	int glfw_particles_example_main(int argc, const char** argv);
	int glfw_sharing_example_main(int argc, const char** argv);
	int glfw_splitview_example_main(int argc, const char** argv);
	int glfw_triangle_opengl_example_main(int argc, const char** argv);
	int glfw_triangle_opengles_example_main(int argc, const char** argv);
	int glfw_wave_example_main(int argc, const char** argv);
	int glfw_windows_example_main(int argc, const char** argv);
	int glfw_allocator_test_main(int argc, const char** argv);
	int glfw_clipboard_test_main(int argc, const char** argv);
	int glfw_cursor_test_main(int argc, const char** argv);
	int glfw_empty_test_main(int argc, const char** argv);
	int glfw_events_test_main(int argc, const char** argv);
	int glfw_gamma_test_main(int argc, const char** argv);
	int glfw_glfwindow_test_main(int argc, const char** argv);
	int glfw_glfwinfo_test_main(int argc, const char** argv);
	int glfw_icon_test_main(int argc, const char** argv);
	int glfw_iconify_test_main(int argc, const char** argv);
	int glfw_inputlag_test_main(int argc, const char** argv);
	int glfw_joysticks_test_main(int argc, const char** argv);
	int glfw_monitors_test_main(int argc, const char** argv);
	int glfw_msaa_test_main(int argc, const char** argv);
	int glfw_reopen_test_main(int argc, const char** argv);
	int glfw_tearing_test_main(int argc, const char** argv);
	int glfw_threads_test_main(int argc, const char** argv);
	int glfw_timeout_test_main(int argc, const char** argv);
	int glfw_title_test_main(int argc, const char** argv);
	int glfw_triangle_vulkan_example_main(int argc, const char** argv);

#ifdef __cplusplus
}
#endif

#endif
