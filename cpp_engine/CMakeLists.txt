cmake_minimum_required(VERSION 3.16)
project(scout_engine LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SCOUT_BUILD_APP "Build standalone scout_engine desktop application" ON)
option(SCOUT_ENABLE_ONNXRUNTIME "Enable ONNX Runtime inference commands" OFF)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to ONNX Runtime SDK root (contains include/ and lib/)")

set(SCOUT_ENGINE_SOURCES
	src/scout_engine.cpp
	src/scout_core.cpp
	src/scout_ocr.cpp
	src/timer_metric.cpp
	src/timer_display.cpp
	src/timer_footer_renderer.cpp
)

add_executable(scout_engine ${SCOUT_ENGINE_SOURCES})
target_include_directories(scout_engine PRIVATE src)

if(SCOUT_BUILD_APP)
	if(NOT WIN32)
		message(FATAL_ERROR "SCOUT_BUILD_APP is currently supported only on Windows")
	endif()

	FetchContent_Declare(
		glfw
		GIT_REPOSITORY https://github.com/glfw/glfw.git
		GIT_TAG 3.4
	)
	FetchContent_Declare(
		imgui
		GIT_REPOSITORY https://github.com/ocornut/imgui.git
		GIT_TAG v1.91.9b
	)
	FetchContent_MakeAvailable(glfw imgui)

	add_library(imgui STATIC
		${imgui_SOURCE_DIR}/imgui.cpp
		${imgui_SOURCE_DIR}/imgui_draw.cpp
		${imgui_SOURCE_DIR}/imgui_tables.cpp
		${imgui_SOURCE_DIR}/imgui_widgets.cpp
		${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
		${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
	)
	target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
	target_link_libraries(imgui PUBLIC glfw)

	target_sources(scout_engine PRIVATE src/scout_app.cpp)
	target_compile_definitions(scout_engine PRIVATE IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
	target_link_libraries(scout_engine PRIVATE imgui glfw opengl32 gdiplus)
endif()

if(SCOUT_ENABLE_ONNXRUNTIME)
	set(_scout_onnx_enabled OFF)

	if(ONNXRUNTIME_ROOT)
		set(_onnx_include "${ONNXRUNTIME_ROOT}/include")
		set(_onnx_lib "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
		set(_onnx_dll_bin "${ONNXRUNTIME_ROOT}/bin/onnxruntime.dll")
		set(_onnx_dll_lib "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")

		if(EXISTS "${_onnx_include}" AND EXISTS "${_onnx_lib}")
			target_include_directories(scout_engine PRIVATE "${_onnx_include}")
			target_link_libraries(scout_engine PRIVATE "${_onnx_lib}")
		target_compile_definitions(scout_engine PRIVATE SCOUT_HAS_ONNXRUNTIME=1)
			set(_scout_onnx_enabled ON)

			if(EXISTS "${_onnx_dll_bin}")
				add_custom_command(TARGET scout_engine POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${_onnx_dll_bin}"
					"$<TARGET_FILE_DIR:scout_engine>/onnxruntime.dll")
			elseif(EXISTS "${_onnx_dll_lib}")
				add_custom_command(TARGET scout_engine POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${_onnx_dll_lib}"
					"$<TARGET_FILE_DIR:scout_engine>/onnxruntime.dll")
			endif()

			message(STATUS "ONNX Runtime enabled via ONNXRUNTIME_ROOT=${ONNXRUNTIME_ROOT}")
		else()
			message(WARNING "ONNXRUNTIME_ROOT was provided but include/lib files were not found. Expected include at ${_onnx_include} and lib at ${_onnx_lib}.")
		endif()
	endif()

	if(NOT _scout_onnx_enabled)
		find_package(onnxruntime CONFIG QUIET)
		if(onnxruntime_FOUND)
			target_compile_definitions(scout_engine PRIVATE SCOUT_HAS_ONNXRUNTIME=1)
			if(TARGET onnxruntime::onnxruntime)
				target_link_libraries(scout_engine PRIVATE onnxruntime::onnxruntime)
			endif()
			set(_scout_onnx_enabled ON)
			message(STATUS "ONNX Runtime enabled via find_package")
		endif()
	endif()

	if(NOT _scout_onnx_enabled)
		message(WARNING "SCOUT_ENABLE_ONNXRUNTIME=ON but ONNX Runtime was not found. Set ONNXRUNTIME_ROOT or install a package config. Building without ONNX support.")
	endif()
endif()
