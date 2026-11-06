cmake_minimum_required(VERSION 3.20)
project(ProjectCaneMapEditor LANGUAGES C CXX)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This editor currently supports Windows and Linux.")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The Linux editor requires a 64-bit toolchain.")
endif()
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(glfw3 3.3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
# Distro libstb-dev supplies these headers. The sibling reference repository
# remains an optional fallback, not a dependency on Windows-built libraries.
find_path(STB_INCLUDE_ROOT NAMES stb/stb_dxt.h
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../ProjectCane/Libraries/include" REQUIRED)
if(NOT EXISTS "${STB_INCLUDE_ROOT}/stb/stb_image_write.h")
    message(FATAL_ERROR "stb_image_write.h is required alongside stb_dxt.h.")
endif()

set(EDITOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ProjectCane Map Editor")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Libraries/include/imgui")
# Stage only the bundled headers that must match their bundled C source.
# Do not include Libraries/include wholesale: its GLFW headers may describe a
# newer API than the system GLFW library.
set(BUNDLED_HEADERS "${CMAKE_CURRENT_BINARY_DIR}/bundled-headers")
file(MAKE_DIRECTORY "${BUNDLED_HEADERS}/glad" "${BUNDLED_HEADERS}/KHR" "${BUNDLED_HEADERS}/stb")
configure_file("Libraries/include/glad/glad.h" "${BUNDLED_HEADERS}/glad/glad.h" COPYONLY)
configure_file("Libraries/include/KHR/khrplatform.h" "${BUNDLED_HEADERS}/KHR/khrplatform.h" COPYONLY)
configure_file("Libraries/include/ufbx.h" "${BUNDLED_HEADERS}/ufbx.h" COPYONLY)
configure_file("${STB_INCLUDE_ROOT}/stb/stb_dxt.h" "${BUNDLED_HEADERS}/stb/stb_dxt.h" COPYONLY)
configure_file("${STB_INCLUDE_ROOT}/stb/stb_image_write.h" "${BUNDLED_HEADERS}/stb/stb_image_write.h" COPYONLY)

add_executable(projectcane-map-editor
    "${EDITOR_DIR}/AudioPreviewService.cpp"
    "${EDITOR_DIR}/SoundBank.cpp"
    "${EDITOR_DIR}/SoundManagerPanel.cpp"
    "${EDITOR_DIR}/ModelImport.cpp"
    "${EDITOR_DIR}/ufbx.c"
    "${EDITOR_DIR}/MapExport.cpp"
    "${EDITOR_DIR}/CharacterExport.cpp"
    "${EDITOR_DIR}/CreateObjectCatalogPanel.cpp"
    "${EDITOR_DIR}/PathZoneRuntime.cpp"
    "${EDITOR_DIR}/SmPreviewRuntime.cpp"
    "${EDITOR_DIR}/SpliceRuntime.cpp"
    "${EDITOR_DIR}/SpliceEventsPanel.cpp"
    "${EDITOR_DIR}/SpliceTutorialPanel.cpp"
    "${EDITOR_DIR}/ObjectOperationsPanel.cpp"
    "${EDITOR_DIR}/EditorTutorialsPanel.cpp"
    "${EDITOR_DIR}/WorldBuilderPanel.cpp"
    "${EDITOR_DIR}/AssetBrowserPanel.cpp"
    "${EDITOR_DIR}/AssetDatabase.cpp"
    "${EDITOR_DIR}/BrxCompiler.cpp"
    "${EDITOR_DIR}/BrxDocument.cpp"
    "${EDITOR_DIR}/BrxLoader.cpp"
    "${EDITOR_DIR}/BrxSourceDocument.cpp"
    "${EDITOR_DIR}/CameraRuntimePanel.cpp"
    "${EDITOR_DIR}/CharacterStudioPanel.cpp"
    "${EDITOR_DIR}/DialogManagerPanel.cpp"
    "${EDITOR_DIR}/EditorApplication.cpp"
    "${EDITOR_DIR}/EditorMcpBridge.h"
    "${EDITOR_DIR}/EditorMcpTools.h"
    "${EDITOR_DIR}/EditorMcpOperations.h"
    "${EDITOR_DIR}/EditorMcpFiles.h"
    "${EDITOR_DIR}/EditorMcpRecords.h"
    "${EDITOR_DIR}/EditorMcpUi.h"
    "${EDITOR_DIR}/EditorMcpTools.cpp"
    "${EDITOR_DIR}/EditorMcpOperations.cpp"
    "${EDITOR_DIR}/EditorMcpFiles.cpp"
    "${EDITOR_DIR}/EditorMcpRecords.cpp"
    "${EDITOR_DIR}/EditorCamera.cpp"
    "${EDITOR_DIR}/EditorDocumentOperations.cpp"
    "${EDITOR_DIR}/EditorOptions.cpp"
    "${EDITOR_DIR}/EditorProject.cpp"
    "${EDITOR_DIR}/EditorTheme.cpp"
    "${EDITOR_DIR}/EditorWorkspace.cpp"
    "${EDITOR_DIR}/EmitterPreviewRuntime.cpp"
    "${EDITOR_DIR}/GuidedObjectStudiosPanel.cpp"
    "${EDITOR_DIR}/HubProgressionStudioPanel.cpp"
    "${EDITOR_DIR}/ObjectTypeRegistry.cpp"
    "${EDITOR_DIR}/PlatformDialogs.cpp"
    "${EDITOR_DIR}/RenderShaders.cpp"
    "${EDITOR_DIR}/ResolvedScene.cpp"
    "${EDITOR_DIR}/RuntimePreviewWorld.cpp"
    "${EDITOR_DIR}/SceneHierarchyPanel.cpp"
    "${EDITOR_DIR}/SceneWorldPanel.cpp"
    "${EDITOR_DIR}/TextureImport.cpp"
    "${EDITOR_DIR}/TextureManagerPanel.cpp"
    "${EDITOR_DIR}/TnCameraPanel.cpp"
    "${EDITOR_DIR}/ValidationReport.cpp"
    "${EDITOR_DIR}/ValidationSystem.cpp"
    "${EDITOR_DIR}/glad.c"
    "${EDITOR_DIR}/main.cpp"
    "${EDITOR_DIR}/PlacementTools.cpp"
    "${EDITOR_DIR}/PrefabLibrary.cpp"
    "${EDITOR_DIR}/ValidationPanel.cpp"
    "${EDITOR_DIR}/ViewportPanel.cpp"
    "${EDITOR_DIR}/InspectorPanel.cpp"
    "${EDITOR_DIR}/ExitManagerPanel.cpp"
    "${EDITOR_DIR}/SpeakerManagerPanel.cpp"
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/imgui_impl_opengl3.cpp"
)
target_include_directories(projectcane-map-editor PRIVATE
    "${EDITOR_DIR}" "${BUNDLED_HEADERS}" "${IMGUI_DIR}")
target_compile_definitions(projectcane-map-editor PRIVATE GLFW_INCLUDE_NONE GLM_ENABLE_EXPERIMENTAL)
target_link_libraries(projectcane-map-editor PRIVATE
    glfw glm::glm nlohmann_json::nlohmann_json OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})

if(WIN32)
    enable_language(RC)
    target_sources(projectcane-map-editor PRIVATE "${EDITOR_DIR}/Resources/AppIcon.rc")
    target_include_directories(projectcane-map-editor PRIVATE "${EDITOR_DIR}/Resources")
    target_compile_definitions(projectcane-map-editor PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(projectcane-map-editor PRIVATE
        Comdlg32 winmm Shell32 Ole32 windowscodecs User32)
    if(MSVC)
        target_compile_options(projectcane-map-editor PRIVATE /bigobj /utf-8)
        target_link_options(projectcane-map-editor PRIVATE /STACK:67108864)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0>=3.22)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2>=2.0.7)
    target_link_libraries(projectcane-map-editor PRIVATE PkgConfig::GTK3 PkgConfig::SDL2)
endif()
install(TARGETS projectcane-map-editor RUNTIME DESTINATION bin)
