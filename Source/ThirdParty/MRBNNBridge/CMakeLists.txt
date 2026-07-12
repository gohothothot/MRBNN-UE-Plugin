cmake_minimum_required(VERSION 3.21)
project(MRBNNBridge LANGUAGES CXX CUDA)

set(MRBNN_ROOT "" CACHE PATH "Path to the cloned Extra-Creativity/MRBNN repository")
if(NOT MRBNN_ROOT)
	message(FATAL_ERROR "Set MRBNN_ROOT to the cloned MRBNN repository.")
endif()

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
