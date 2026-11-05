#pragma once

#include <string>

class BrxDocument;
class EditorCamera;
class SceneRenderer;

void DrawBreakableStudioPanel(bool &open, BrxDocument &, SceneRenderer &, EditorCamera &, std::string &);
void DrawSecurityStudioPanel(bool &open, BrxDocument &, SceneRenderer &, EditorCamera &, std::string &);
void DrawMechanismStudioPanel(bool &open, BrxDocument &, SceneRenderer &, EditorCamera &, std::string &);
void DrawPickupStudioPanel(bool &open, BrxDocument &, SceneRenderer &, EditorCamera &, std::string &);
void DrawWeaponStudioPanel(bool &open, BrxDocument &, SceneRenderer &, EditorCamera &, std::string &);
void DrawWaterStudioPanel(bool &open, BrxDocument &, SceneRenderer &, EditorCamera &, std::string &);
void DrawShadowStudioPanel(bool &open, BrxDocument &, SceneRenderer &, EditorCamera &, std::string &);
