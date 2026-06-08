cmake_minimum_required(VERSION 3.16)
project(scout_engine LANGUAGES C CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SCOUT_BUILD_APP "Build standalone scout_engine desktop application" ON)
option(SCOUT_ENABLE_ONNXRUNTIME "Enable ONNX Runtime inference commands" ON)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Path to ONNX Runtime SDK root (contains include/ and lib/)")
option(SCOUT_USE_LIBSODIUM "Enable libsodium for secure random UUID generation" ON)

set(SCOUT_ENGINE_SOURCES
	src/scout_core.cpp
	src/scout_ocr.cpp
	src/timer_metric.cpp
    src/point_store_csv.cpp
    src/point_store_sqlite.cpp
    src/sync_service.cpp
    src/travel_log.cpp
	src/uuid.cpp
	src/json_exchange_datapoint.cpp
)

# Build core library so tests can link against project code
add_library(scout_core_lib STATIC ${SCOUT_ENGINE_SOURCES})
target_include_directories(scout_core_lib PUBLIC src)

# If vendored sqlite headers are present, make them available to the core library
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite3.h")
	target_include_directories(scout_core_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
endif()

# Application executable now links the core library and contains UI-specific sources
add_executable(scout_engine
	src/scout_render.h
	src/scout_render.cpp
	src/timer_display.cpp
	src/data_table.cpp)
target_include_directories(scout_engine PRIVATE src)
target_link_libraries(scout_engine PRIVATE scout_core_lib)

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
		glad
		GIT_REPOSITORY https://github.com/Dav1dde/glad.git
		GIT_TAG v0.1.36
	)
	FetchContent_Declare(
		imgui
		GIT_REPOSITORY https://github.com/ocornut/imgui.git
		GIT_TAG v1.91.9b
	)
	FetchContent_MakeAvailable(glfw glad imgui)

	# JSON parsing library (header-only)
	FetchContent_Declare(
		nlohmann_json
		GIT_REPOSITORY https://github.com/nlohmann/json.git
		GIT_TAG v3.11.2
	)
	FetchContent_MakeAvailable(nlohmann_json)

	# Make nlohmann_json headers available to the core library as well
	target_link_libraries(scout_core_lib PUBLIC nlohmann_json::nlohmann_json)

	add_library(imgui STATIC
		${imgui_SOURCE_DIR}/imgui.cpp
		${imgui_SOURCE_DIR}/imgui_draw.cpp
		${imgui_SOURCE_DIR}/imgui_tables.cpp
		${imgui_SOURCE_DIR}/imgui_widgets.cpp
		${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
		${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
	)
	target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)

	# Use GLAD as the OpenGL loader for ImGui backend and link glad
	target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)
	target_link_libraries(imgui PUBLIC glfw glad)
	find_package(cpr CONFIG REQUIRED)
	find_package(ixwebsocket CONFIG REQUIRED)

		target_sources(scout_engine PRIVATE src/scout_app.cpp src/scout_engine.cpp src/timer_footer_renderer.cpp)

	# SQLite3 for local storage: prefer vendored SQLite under project; ensure core lib can compile and app links runtime deps
	set(_base_libs imgui glfw opengl32 gdiplus cpr::cpr ixwebsocket::ixwebsocket)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
		message(STATUS "Using vendored SQLite at ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
		target_include_directories(scout_core_lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite3.c")
			target_sources(scout_core_lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite3.c")
		endif()
		target_link_libraries(scout_engine PRIVATE ${_base_libs})
	else()
		find_package(SQLite3 QUIET)
		if(SQLite3_FOUND)
			if(TARGET SQLite::SQLite3)
				# ensure core library gets usage requirements (include directories) from SQLite target
				target_link_libraries(scout_core_lib PRIVATE SQLite::SQLite3)
				target_link_libraries(scout_engine PRIVATE ${_base_libs} SQLite::SQLite3)
			else()
				if(DEFINED SQLite3_INCLUDE_DIRS)
					target_include_directories(scout_core_lib PRIVATE ${SQLite3_INCLUDE_DIRS})
				endif()
				if(DEFINED SQLite3_LIBRARIES)
					target_link_libraries(scout_engine PRIVATE ${_base_libs} ${SQLite3_LIBRARIES})
				else()
					target_link_libraries(scout_engine PRIVATE ${_base_libs})
				endif()
			endif()
		else()
			message(WARNING "SQLite3 not found; building without SQLite support.")
			target_link_libraries(scout_engine PRIVATE ${_base_libs})
		endif()
	endif()

	# Link nlohmann_json (header-only target) to make include available
	target_link_libraries(scout_engine PRIVATE nlohmann_json::nlohmann_json)

	endif()

# Ensure core library can find SQLite headers/sources when SCOUT_BUILD_APP is disabled (so tests compile)
if(NOT SCOUT_BUILD_APP)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
		message(STATUS "Using vendored SQLite at ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite (tests)")
		target_include_directories(scout_core_lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
		if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite3.c")
			target_sources(scout_core_lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite3.c")
		endif()
	else()
		find_package(SQLite3 QUIET)
		if(SQLite3_FOUND)
			if(TARGET SQLite::SQLite3)
				target_link_libraries(scout_core_lib PRIVATE SQLite::SQLite3)
			elseif(DEFINED SQLite3_INCLUDE_DIRS)
				target_include_directories(scout_core_lib PRIVATE ${SQLite3_INCLUDE_DIRS})
			endif()
		else()
			message(WARNING "SQLite3 not found; building without SQLite support.")
		endif()
	endif()
endif()

# Optional: use libsodium for secure random bytes (UUIDv4)
if(SCOUT_USE_LIBSODIUM)
	# Prefer vendored libsodium under cpp_engine/third_party/libsodium
	find_path(SODIUM_INCLUDE_DIR sodium.h
		PATHS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium/include"
			  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium"
			  ENV PATH
		)
	find_library(SODIUM_LIBRARY NAMES sodium libsodium sodium.lib libsodium.lib
		PATHS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium/lib"
			  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium/bin"
			  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsodium"
		)
	if(SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY)
		target_include_directories(scout_engine PRIVATE ${SODIUM_INCLUDE_DIR})
		target_link_libraries(scout_engine PRIVATE ${SODIUM_LIBRARY})
		target_compile_definitions(scout_engine PRIVATE SCOUT_USE_LIBSODIUM=1)
		message(STATUS "Using libsodium for secure random (include=${SODIUM_INCLUDE_DIR}, lib=${SODIUM_LIBRARY})")
	else()
		message(WARNING "SCOUT_USE_LIBSODIUM=ON but libsodium not found; continuing without libsodium")
	endif()
endif()

# Optional: unit tests
option(SCOUT_BUILD_TESTS "Build unit tests" OFF)
if(SCOUT_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()

if(SCOUT_ENABLE_ONNXRUNTIME)
	set(_scout_onnx_enabled OFF)

	# If the user provided ONNXRUNTIME_ROOT, prefer it; otherwise try find_package.
	if(ONNXRUNTIME_ROOT AND EXISTS "${ONNXRUNTIME_ROOT}/include")
		set(_onnx_root "${ONNXRUNTIME_ROOT}")
		target_include_directories(scout_engine PRIVATE "${_onnx_root}/include")
		# Also expose ONNX headers to the core library so its sources compile
		target_include_directories(scout_core_lib PRIVATE "${_onnx_root}/include")
		if(EXISTS "${_onnx_root}/lib/onnxruntime.lib")
			target_link_libraries(scout_engine PRIVATE "${_onnx_root}/lib/onnxruntime.lib")
			# Link ONNX runtime directly to the core library as well
			target_link_libraries(scout_core_lib PRIVATE "${_onnx_root}/lib/onnxruntime.lib")
		endif()
		if(EXISTS "${_onnx_root}/bin/onnxruntime.dll")
			add_custom_command(TARGET scout_engine POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${_onnx_root}/bin/onnxruntime.dll"
				"$<TARGET_FILE_DIR:scout_engine>/onnxruntime.dll")
		elseif(EXISTS "${_onnx_root}/lib/onnxruntime.dll")
			add_custom_command(TARGET scout_engine POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${_onnx_root}/lib/onnxruntime.dll"
				"$<TARGET_FILE_DIR:scout_engine>/onnxruntime.dll")
		endif()
		target_compile_definitions(scout_engine PRIVATE SCOUT_HAS_ONNXRUNTIME=1)
		# Ensure core library compilation sees the ONNX definition
		target_compile_definitions(scout_core_lib PRIVATE SCOUT_HAS_ONNXRUNTIME=1)
		set(_scout_onnx_enabled ON)
		message(STATUS "ONNX Runtime enabled via ONNXRUNTIME_ROOT=${ONNXRUNTIME_ROOT}")
	else()
		find_package(onnxruntime CONFIG QUIET)
		if(onnxruntime_FOUND)
			target_compile_definitions(scout_engine PRIVATE SCOUT_HAS_ONNXRUNTIME=1)
			# Ensure core library compilation sees the ONNX definition
			target_compile_definitions(scout_core_lib PRIVATE SCOUT_HAS_ONNXRUNTIME=1)
			if(TARGET onnxruntime::onnxruntime)
				target_link_libraries(scout_engine PRIVATE onnxruntime::onnxruntime)
				target_link_libraries(scout_core_lib PRIVATE onnxruntime::onnxruntime)
			endif()
			set(_scout_onnx_enabled ON)
			message(STATUS "ONNX Runtime enabled via find_package")
		endif()
	endif()

	if(NOT _scout_onnx_enabled)
		message(WARNING "SCOUT_ENABLE_ONNXRUNTIME=ON but ONNX Runtime was not found. Building without ONNX support.")
	endif()
endif()
