cmake_minimum_required(VERSION 3.21)
project(MRBNNBridge LANGUAGES C CXX CUDA)

set(MRBNN_ROOT "" CACHE PATH "Path to the cloned Extra-Creativity/MRBNN repository")
if(NOT MRBNN_ROOT)
	message(FATAL_ERROR "Set MRBNN_ROOT to the cloned MRBNN repository.")
endif()

option(MRBNN_BUILD_BAKE_CONSOLE "Build the MRBNN Dear ImGui bake/debug console." OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_ARCHITECTURES 89 CACHE STRING "CUDA architectures for MRBNN")

find_package(CUDAToolkit REQUIRED)

include(FetchContent)

if(MSVC)
	set(GlobalCXXFlags /utf-8 /W3 /wd4996)
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${GlobalCXXFlags}>")
	foreach(flag IN LISTS GlobalCXXFlags)
		add_compile_options("$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=${flag}>")
	endforeach()
else()
	set(GlobalCXXFlags -fexec-charset=UTF-8 -finput-charset=UTF-8 -Wall)
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${GlobalCXXFlags}>")
	foreach(flag IN LISTS GlobalCXXFlags)
		add_compile_options("$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=${flag}>")
	endforeach()
endif()

set(GlobalCUDAFlags "--expt-relaxed-constexpr" "--extended-lambda" "--diag-suppress" "20012")
foreach(flag IN LISTS GlobalCUDAFlags)
	add_compile_options("$<$<COMPILE_LANGUAGE:CUDA>:${flag}>")
endforeach()

FetchContent_Declare(
	glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG        0af55ccecd98d4e5a8d1fad7de25ba429d60e863
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
	json
	GIT_REPOSITORY https://github.com/nlohmann/json.git
	GIT_TAG        55f93686c01528224f448c19128836e7df245f72
)
FetchContent_MakeAvailable(json)
file(REMOVE_RECURSE ${json_SOURCE_DIR}/tools)

set(MRBNN_SOURCE_ROOT "${MRBNN_ROOT}")
add_subdirectory("${MRBNN_ROOT}/external" "${CMAKE_BINARY_DIR}/MRBNN/external")
add_subdirectory("${MRBNN_ROOT}/src/utils" "${CMAKE_BINARY_DIR}/MRBNN/src/utils")
add_subdirectory("${MRBNN_ROOT}/src/render" "${CMAKE_BINARY_DIR}/MRBNN/src/render")
target_include_directories(RenderLib PRIVATE "${MRBNN_ROOT}/external")
get_target_property(RenderLibCompileDefinitions RenderLib COMPILE_DEFINITIONS)
if(RenderLibCompileDefinitions)
	list(FILTER RenderLibCompileDefinitions EXCLUDE REGEX "^RELA_PATH_ROOT=")
	set_property(TARGET RenderLib PROPERTY COMPILE_DEFINITIONS ${RenderLibCompileDefinitions})
endif()
target_compile_definitions(RenderLib PRIVATE RELA_PATH_ROOT="${MRBNN_ROOT}")

add_library(MRBNNBridge SHARED MRBNNBridge.cpp)
target_compile_features(MRBNNBridge PRIVATE cxx_std_20)
target_include_directories(MRBNNBridge PRIVATE
	"${MRBNN_ROOT}/src"
	"${MRBNN_ROOT}/src/render"
	"${MRBNN_ROOT}/src/utils"
	"${MRBNN_ROOT}/external")
target_link_libraries(MRBNNBridge PRIVATE RenderLib CUDA::cudart)

if(MSVC)
	target_compile_options(MRBNNBridge PRIVATE /utf-8)
endif()

add_executable(MRBNNBridgeSmokeTest MRBNNBridgeSmokeTest.cpp)
target_compile_features(MRBNNBridgeSmokeTest PRIVATE cxx_std_20)
target_link_libraries(MRBNNBridgeSmokeTest PRIVATE MRBNNBridge)

if(WIN32)
	add_custom_command(TARGET MRBNNBridgeSmokeTest POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:MRBNNBridge>
			$<TARGET_FILE_DIR:MRBNNBridgeSmokeTest>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:ExternalTCNN>
			$<TARGET_FILE_DIR:MRBNNBridgeSmokeTest>)
endif()

if(MRBNN_BUILD_BAKE_CONSOLE)
	set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

	FetchContent_Declare(
		glfw
		GIT_REPOSITORY https://github.com/glfw/glfw.git
		GIT_TAG        7b6aead9fb88b3623e3b3725ebb42670cbe4c579
	)
	FetchContent_MakeAvailable(glfw)

	FetchContent_Declare(
		imgui
		GIT_REPOSITORY https://github.com/ocornut/imgui.git
		GIT_TAG        9a5d5c45f54b1301ea471622eddede70384243af
	)
	FetchContent_MakeAvailable(imgui)

	add_library(MRBNNBakeConsoleImGui STATIC
		${imgui_SOURCE_DIR}/imgui.cpp
		${imgui_SOURCE_DIR}/imgui_draw.cpp
		${imgui_SOURCE_DIR}/imgui_tables.cpp
		${imgui_SOURCE_DIR}/imgui_widgets.cpp
		${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
		${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
	target_compile_definitions(MRBNNBakeConsoleImGui PUBLIC GLFW_INCLUDE_NONE IMGUI_IMPL_OPENGL_LOADER_GLAD2)
	target_include_directories(MRBNNBakeConsoleImGui PUBLIC
		${imgui_SOURCE_DIR}
		${imgui_SOURCE_DIR}/backends
		"${MRBNN_ROOT}/src/display/glad/include")
	target_link_libraries(MRBNNBakeConsoleImGui PUBLIC glfw)

	add_library(MRBNNBakeConsoleGlad STATIC "${MRBNN_ROOT}/src/display/glad/src/gl.c")
	target_include_directories(MRBNNBakeConsoleGlad PUBLIC "${MRBNN_ROOT}/src/display/glad/include")

	add_executable(MRBNNBakeConsole MRBNNBakeConsole.cpp MRBNNCloudInfoBake.cu)
	target_compile_features(MRBNNBakeConsole PRIVATE cxx_std_20)
	target_link_libraries(MRBNNBakeConsole PRIVATE
		MRBNNBridge
		MRBNNBakeConsoleImGui
		MRBNNBakeConsoleGlad
		nlohmann_json::nlohmann_json
		CUDA::cudart
		CUDA::nvrtc)

	if(WIN32)
		target_link_libraries(MRBNNBakeConsole PRIVATE opengl32)
		add_custom_command(TARGET MRBNNBakeConsole POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:MRBNNBridge>
				$<TARGET_FILE_DIR:MRBNNBakeConsole>
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:ExternalTCNN>
				$<TARGET_FILE_DIR:MRBNNBakeConsole>)
	endif()
endif()
