#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include "raylib.h"

// Scene state structure (GPU and camera info)
typedef struct {
    // GPU data
    unsigned int ssboTris;
    unsigned int ssboObjs;
    int triCount;
    int objCount;
    Vector3 lightPos;

    // Camera and its last state (frame accumulation)
    Camera3D camera;
    Vector3 lastCamPos;
    Vector3 lastCamTarget;
    bool showGizmo;
} SceneState;

// Init scene with default values
SceneState InitSceneState(void);

// Reloads scene (input 1,2 scene switching)
// Returns true if scene changed (reset accumFrame)
bool HandleSceneInput(SceneState* state);

// Updates camera and checks if it moved
// Returns true if movement occurred (signal to reset accumFrame)
bool UpdateSceneCamera(SceneState* state);

// Draws Gizmo (helper lines)
void DrawSceneGizmo(SceneState* state);

// Clear GPU data
void UnloadSceneState(SceneState* state);

// --- Helpers ---
typedef struct {
    void* tris;      // Tris data
    void* objects;   // Object data
    int triCount;
    int objCount;
    int maxTriangles;
    int maxObjects;
} Scene;

Scene InitScene(int maxTri, int maxObj);
void AddEntity(Scene* scene, Mesh mesh, Vector3 pos, int matID, int objID);
void AddModelObj(Scene* scene, const char* fileName, Vector3 pos, float scale, int matID, int objID);
void LoadScene_StressTest(Scene* scene);
void LoadScene_ComplexModel(Scene* scene);
void FreeSceneRAM(Scene* scene);
void UploadSceneToGPU(Scene* scene, unsigned int* ssboTris, unsigned int* ssboObjs);

#endif