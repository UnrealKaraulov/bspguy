cmake_minimum_required (VERSION 3.28)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
project(bspguy LANGUAGES C CXX)

include_directories(src)
include_directories(src/bsp)
include_directories(src/res)
include_directories(src/cli)
include_directories(src/data)
include_directories(src/editor)
include_directories(src/filedialog)
include_directories(src/gl)
include_directories(src/qtools)
include_directories(src/nav)
include_directories(src/util)
include_directories(src/mdl)
include_directories(imgui)
include_directories(imgui/examples)
include_directories(imgui/misc/cpp)
include_directories(imgui/backends)
include_directories(glew/include)
include_directories(ascript/angelscript)
include_directories(ascript/angelscript/include)

add_subdirectory(fmt)
add_subdirectory(glfw)
add_subdirectory(ascript/angelscript/projects/cmake)

set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SOURCE_FILES
	src/main.cpp

	# command line
	src/cli/CommandLine.h			src/cli/CommandLine.cpp
	src/cli/ProgressMeter.h			src/cli/ProgressMeter.cpp

	# BSP and related structures
	src/bsp/forcecrc32.h			src/bsp/forcecrc32.cpp
	src/bsp/BspMerger.h				src/bsp/BspMerger.cpp
	src/bsp/Bsp.h					src/bsp/Bsp.cpp
	src/bsp/bsplimits.h				src/bsp/bsplimits.cpp
	src/bsp/bsptypes.h				src/bsp/bsptypes.cpp
	src/bsp/Entity.h				src/bsp/Entity.cpp
	src/bsp/Keyvalue.h				src/bsp/Keyvalue.cpp
	src/bsp/remap.h					src/bsp/remap.cpp

	# Math and stuff
	src/util/util.h					src/util/util.cpp
	src/util/vectors.h				src/util/vectors.cpp
	src/util/mat4x4.h				src/util/mat4x4.cpp

	# OpenGL rendering
	src/gl/shaders.h				src/gl/shaders.cpp
	src/gl/primitives.h				src/gl/primitives.cpp
	src/gl/Shader.h					src/gl/Shader.cpp
	src/gl/ShaderProgram.h			src/gl/ShaderProgram.cpp
	src/gl/VertexBuffer.h			src/gl/VertexBuffer.cpp
	src/gl/Texture.h				src/gl/Texture.cpp
	src/editor/LightmapNode.h		src/editor/LightmapNode.cpp

	# 3D editor
	src/editor/Settings.h			src/editor/Settings.cpp
	src/editor/Renderer.h			src/editor/Renderer.cpp
	src/editor/Gui.h				src/editor/Gui.cpp
	src/editor/BspRenderer.h		src/editor/BspRenderer.cpp
	src/editor/PointEntRenderer.h	src/editor/PointEntRenderer.cpp
	src/editor/Fgd.h				src/editor/Fgd.cpp
	src/editor/Clipper.h			src/editor/Clipper.cpp
	src/editor/Command.h			src/editor/Command.cpp

	# map compiler code
	src/qtools/rad.h				src/qtools/rad.cpp
	src/qtools/vis.h				src/qtools/vis.cpp
	src/qtools/winding.h			src/qtools/winding.cpp
	
	# utils
	src/util/lodepng.h				src/util/lodepng.cpp
	src/util/quantizer.h			src/util/quantizer.cpp
	src/util/Polygon3D.h			src/util/Polygon3D.cpp
	src/util/Line2D.h				src/util/Line2D.cpp
	src/util/ini.h
	src/util/lang_defs.h
	src/util/lang.h					src/util/lang.cpp
	src/util/log.h					src/util/log.cpp
	
	# nav
	src/nav/LeafNavMesh.h			src/nav/LeafNavMesh.cpp
	src/nav/LeafNavMeshGenerator.h	src/nav/LeafNavMeshGenerator.cpp
	src/nav/LeafOctree.h			src/nav/LeafOctree.cpp
	src/nav/NavMesh.h				src/nav/NavMesh.cpp
	src/nav/NavMeshGenerator.h		src/nav/NavMeshGenerator.cpp
	src/nav/PolyOctree.h		src/nav/PolyOctree.cpp
	
	# 3D model viewer
	src/mdl/mdl_studio.h			src/mdl/mdl_studio.cpp

	# OPENFILEDIALOG
	src/filedialog/stb_image.h		src/filedialog/stb_image_write.h
	src/filedialog/ImFileDialog.h	src/filedialog/ImFileDialog.cpp
	
	src/res/Wad.h					src/res/Wad.cpp
	src/res/Sprite.h				src/res/Sprite.cpp
	src/res/XASH_csm.h				src/res/XASH_csm.cpp
	src/res/JACK_jmf.h
	
	# library files
	imgui/imgui.cpp
	imgui/imgui_tables.cpp
	imgui/imgui_widgets.cpp
	imgui/imgui_draw.cpp
	imgui/backends/imgui_impl_glfw.cpp
	imgui/backends/imgui_impl_opengl3.cpp
	imgui/misc/cpp/imgui_stdlib.cpp
	
	ascript/add_on/scriptarray/scriptarray.h
	ascript/add_on/scriptarray/scriptarray.cpp
	ascript/add_on/scriptbuilder/scriptbuilder.h
	ascript/add_on/scriptbuilder/scriptbuilder.cpp
	ascript/add_on/scriptstdstring/scriptstdstring.h
	ascript/add_on/scriptstdstring/scriptstdstring.cpp
	ascript/add_on/scriptstdstring/scriptstdstring_utils.cpp
)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

if(NOT MSVC)
	add_definitions(-DNDEBUG)
	if(WIN32)
		if(CMAKE_SIZEOF_VOID_P EQUAL 8)
			target_link_libraries(${PROJECT_NAME} fmt::fmt glfw angelscript ${CMAKE_CURRENT_SOURCE_DIR}/glew/lib/Release/x64/glew32.lib opengl32)
		elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
			target_link_libraries(${PROJECT_NAME} fmt::fmt glfw angelscript ${CMAKE_CURRENT_SOURCE_DIR}/glew/lib/Release/Win32/glew32.lib opengl32)
		endif()
		add_definitions(-DWIN32)
		add_definitions(-DMINGW)
	else()
		target_link_libraries(${PROJECT_NAME} GL GLU fmt::fmt glfw Xxf86vm Xrandr pthread Xi GLEW stdc++fs )
	endif()
	set(CMAKE_CXX_STANDARD 20)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(PICO_ENABLE_CXX_EXCEPTIONS 1)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++2a -g0 -O3 -fno-stack-protector -w -Wfatal-errors\
	-fcf-protection=none -ffunction-sections -fdata-sections -mtune=generic -fpermissive -fno-sized-deallocation\
	-Wno-delete-non-virtual-dtor -Wno-invalid-offsetof -Wno-unused-variable -Wno-unused-value -Wno-unused-result\
	-Wno-unused-function -Wno-write-strings -Wno-switch -Wno-enum-compare -Wno-sign-compare -Wno-format\
	-Wno-ignored-attributes -Wno-strict-aliasing -fno-plt -fno-devirtualize -Wno-unused-local-typedefs\
	-Wno-unused-but-set-variable -Wno-stringop-truncation -Wno-format-truncation -Wno-class-memaccess")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O3 -fno-stack-protector -Wfatal-errors -ffunction-sections\
	-fdata-sections -mtune=generic -fpermissive -fno-sized-deallocation -Wno-unused-variable -Wno-unused-value\
	-Wno-unused-result -Wno-unused-function -Wno-write-strings -Wno-switch -Wno-enum-compare -Wno-sign-compare\
	-Wno-format -Wno-ignored-attributes -Wno-strict-aliasing -fno-plt -fno-devirtualize -Wno-unused-local-typedefs\
	-Wno-unused-but-set-variable -Wno-stringop-truncation -Wno-format-truncation -Wno-class-memaccess")
else()
	message(FATAL_ERROR "Use vs-project/bspguy.sln for Visual Studio")
endif()
