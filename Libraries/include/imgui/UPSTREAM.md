# Bundled Dear ImGui

Pinned source: [ocornut/imgui, v1.90.9-docking](https://github.com/ocornut/imgui/tree/v1.90.9-docking).

Core source/headers, bundled STB headers, GLFW and OpenGL3 backends (including the OpenGL loader), demo source and LICENSE.txt are based on this same tag. Backend files remain flattened into this directory to match both existing build projects. The existing imconfig.h is retained.

Local OpenGL3 backend extensions are retained: ImGui_ImplOpenGL3_RenderTimings/GetLastRenderTimings, setup/upload/command/draw/cleanup timing, single-context state ownership, a persistent VAO, and orphan-then-upload streaming buffers. These are consumed by EditorApplication.cpp and must be preserved when updating the backend. The custom state-setup/draw paths are ported from the pre-docking backend; docking's platform interface and current capability detection remain in place. OS multi-viewports must remain disabled with the single-context fast path.

The editor enables docking but does not enable multi-viewport OS windows. EditorDockLayout.h uses this version's internal DockBuilder API only to seed/reset the default layout. Layout persistence uses ImGui's existing imgui.ini settings mechanism.

When updating ImGui, keep the core, internal header, STB headers and backends in sync. Verify both Windows and Linux builds and the default/reset/saved layouts; do not update only imgui.h.
