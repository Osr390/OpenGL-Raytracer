#include "scene_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h> // sinf/cosf

// --- GPU STRUCTURES---

typedef struct {
    Vector4 p0;
    Vector4 p1;
    Vector4 p2;
    Vector4 normal;
} GpuTriangle;

typedef struct {
    Vector4 aabbMin;
    Vector4 aabbMax;
    int startIndex;
    int triangleCount;
    int materialID;
    int objectID;
} GpuObject;

// --- Helpers ---

// Converts vec3 to vec4 with w padding
Vector4 ToVec4(Vector3 v) {
    return (Vector4) { v.x, v.y, v.z, 0.0f };
}

// Raylib mesh into own structures
void AddMeshToScene(Scene* scene, Mesh mesh, Vector3 pos, int matID, int objID, bool freeMesh) {

    if (scene->triCount + mesh.triangleCount > scene->maxTriangles) {
        printf("[SCENE] ERROR: Max triangles reached! (%d/%d)\n", scene->triCount, scene->maxTriangles);
        return;
    }
    if (scene->objCount >= scene->maxObjects) {
        printf("[SCENE] ERROR: Max objects reached!\n");
        return;
    }

    GpuTriangle* triBuffer = (GpuTriangle*)scene->tris;
    GpuObject* objBuffer = (GpuObject*)scene->objects;

    int startIndex = scene->triCount;

    // Calculate AABB
    Vector3 minBox = (Vector3){ 1e30f, 1e30f, 1e30f };
    Vector3 maxBox = (Vector3){ -1e30f, -1e30f, -1e30f };

    for (int i = 0; i < mesh.triangleCount; i++) {
        Vector3 v0, v1, v2;

        if (mesh.indices) {
            v0 = (Vector3){ mesh.vertices[mesh.indices[i * 3 + 0] * 3], mesh.vertices[mesh.indices[i * 3 + 0] * 3 + 1], mesh.vertices[mesh.indices[i * 3 + 0] * 3 + 2] };
            v1 = (Vector3){ mesh.vertices[mesh.indices[i * 3 + 1] * 3], mesh.vertices[mesh.indices[i * 3 + 1] * 3 + 1], mesh.vertices[mesh.indices[i * 3 + 1] * 3 + 2] };
            v2 = (Vector3){ mesh.vertices[mesh.indices[i * 3 + 2] * 3], mesh.vertices[mesh.indices[i * 3 + 2] * 3 + 1], mesh.vertices[mesh.indices[i * 3 + 2] * 3 + 2] };
        }
        else {
            v0 = (Vector3){ mesh.vertices[i * 9 + 0], mesh.vertices[i * 9 + 1], mesh.vertices[i * 9 + 2] };
            v1 = (Vector3){ mesh.vertices[i * 9 + 3], mesh.vertices[i * 9 + 4], mesh.vertices[i * 9 + 5] };
            v2 = (Vector3){ mesh.vertices[i * 9 + 6], mesh.vertices[i * 9 + 7], mesh.vertices[i * 9 + 8] };
        }

        // Transform to world pos
        v0 = Vector3Add(v0, pos);
        v1 = Vector3Add(v1, pos);
        v2 = Vector3Add(v2, pos);

        // Update AABB
        minBox = Vector3Min(minBox, v0); minBox = Vector3Min(minBox, v1); minBox = Vector3Min(minBox, v2);
        maxBox = Vector3Max(maxBox, v0); maxBox = Vector3Max(maxBox, v1); maxBox = Vector3Max(maxBox, v2);

        // Calculate normal
        Vector3 edge1 = Vector3Subtract(v1, v0);
        Vector3 edge2 = Vector3Subtract(v2, v0);
        Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge1, edge2));

        // Save to buffer
        GpuTriangle* t = &triBuffer[startIndex + i];
        t->p0 = ToVec4(v0);
        t->p1 = ToVec4(v1);
        t->p2 = ToVec4(v2);
        t->normal = ToVec4(normal);
    }

    // Create object
    GpuObject* obj = &objBuffer[scene->objCount];
    obj->aabbMin = ToVec4(minBox);
    obj->aabbMax = ToVec4(maxBox);
    obj->startIndex = startIndex;
    obj->triangleCount = mesh.triangleCount;
    obj->materialID = matID;
    obj->objectID = objID;

    // Counters
    scene->triCount += mesh.triangleCount;
    scene->objCount++;

    if (freeMesh) UnloadMesh(mesh);
}


Scene InitScene(int maxTri, int maxObj) {
    Scene s = { 0 };
    s.maxTriangles = maxTri;
    s.maxObjects = maxObj;
    s.triCount = 0;
    s.objCount = 0;

	// Alocate CPU-side buffers
    s.tris = malloc(maxTri * sizeof(GpuTriangle));
    s.objects = malloc(maxObj * sizeof(GpuObject));

    return s;
}

void FreeSceneRAM(Scene* scene) {
    if (scene->tris) free(scene->tris);
    if (scene->objects) free(scene->objects);
    scene->tris = NULL;
    scene->objects = NULL;
}

void AddEntity(Scene* scene, Mesh mesh, Vector3 pos, int matID, int objID) {
    AddMeshToScene(scene, mesh, pos, matID, objID, false);
}

void AddModelObj(Scene* scene, const char* fileName, Vector3 pos, float scale, int matID, int objID) {
    if (!FileExists(fileName)) {
        printf("[SCENE] Error: Model file not found: %s\n", fileName);
        return;
    }

    Model model = LoadModel(fileName);
    for (int i = 0; i < model.meshCount; i++) {
        AddMeshToScene(scene, model.meshes[i], pos, matID, objID, false);
    }
    UnloadModel(model);
}

void UploadSceneToGPU(Scene* scene, unsigned int* ssboTris, unsigned int* ssboObjs) {
    if (*ssboTris != 0) rlUnloadShaderBuffer(*ssboTris);
    if (*ssboObjs != 0) rlUnloadShaderBuffer(*ssboObjs);

    size_t sizeTris = scene->triCount * sizeof(GpuTriangle);
    size_t sizeObjs = scene->objCount * sizeof(GpuObject);

    if (sizeTris > 0) *ssboTris = rlLoadShaderBuffer(sizeTris, scene->tris, RL_DYNAMIC_COPY);
    if (sizeObjs > 0) *ssboObjs = rlLoadShaderBuffer(sizeObjs, scene->objects, RL_DYNAMIC_COPY);

    printf("[GPU] Uploaded: %d tris, %d objs\n", scene->triCount, scene->objCount);
}

// --- SCENES ---

void LoadScene_StressTest(Scene* scene) {
    Mesh floor = GenMeshCube(100.0f, 1.0f, 100.0f);
    AddEntity(scene, floor, (Vector3) { 0, -1.5f, 0 }, 0, 0);

    Mesh sun = GenMeshCube(20.0f, 1.0f, 20.0f);
    AddEntity(scene, sun, (Vector3) { 0, 15.0f, 0 }, 5, 9999);

	Mesh ceiling = GenMeshCube(100.0f, 1.0f, 100.0f);
	AddEntity(scene, ceiling, (Vector3) { 0, 19.0f, 0 }, 0, 9998);

    int gridSize = 20;
    float spacing = 2.5f;
    float offset = (gridSize * spacing) / 2.0f;

    int count = 1;
    for (int x = 0; x < gridSize; x++) {
        for (int z = 0; z < gridSize; z++) {
            float posX = (x * spacing) - offset;
            float posZ = (z * spacing) - offset;
            float posY = sinf(x * 0.5f) * cosf(z * 0.5f) * 2.0f + 1.0f;

            Mesh cube = GenMeshCube(1.0f, 1.0f, 1.0f);
            int matID = (count % 4) + 1;

            AddEntity(scene, cube, (Vector3) { posX, posY, posZ }, matID, count);
            count++;
        }
    }
    printf("[SCENE] Stress Test Loaded: %d objects.\n", scene->objCount);
}

void LoadScene_ComplexModel(Scene* scene) {
    Mesh floor = GenMeshCube(50.0f, 1.0f, 50.0f);
    AddEntity(scene, floor, (Vector3) { 0, -1.0f, 0 }, 0, 0);

    Mesh ceiling = GenMeshCube(50.0f, 1.0f, 50.0f);
    AddEntity(scene, ceiling, (Vector3) { 0, 19.0f, 0 }, 0, 998);

	Mesh wall = GenMeshCube(50.0f, 20.0f, 1.0f);
	AddEntity(scene, wall, (Vector3) { 0, 9.0f, -10.0f }, 0, 997);

    Mesh lightSphere = GenMeshSphere(3.0f, 16, 16);
    AddEntity(scene, lightSphere, (Vector3) { 5.0f, 10.0f, 5.0f }, 5, 999);

    if (FileExists("model.obj")) {
        AddModelObj(scene, "model.obj", (Vector3) { 0, 0.5f, 0 }, 1.0f, 2, 1);
    }
    else {
        printf("[SCENE] Teapot not found, loading sphere.\n");
        Mesh sphere = GenMeshSphere(2.0f, 32, 32);
        AddEntity(scene, sphere, (Vector3) { 0, 2.0f, 0 }, 2, 1);
    }
}

void LoadScene_Mirrors(Scene* scene) {
    Mesh floor = GenMeshCube(50.0f, 1.0f, 50.0f);
    AddEntity(scene, floor, (Vector3) { 0, -1.0f, 0 }, 0, 0);

    Mesh ceiling = GenMeshCube(50.0f, 1.0f, 50.0f);
    AddEntity(scene, ceiling, (Vector3) { 0, 19.0f, 0 }, 0, 1);

    Mesh wall = GenMeshCube(50.0f, 20.0f, 1.0f);
    AddEntity(scene, wall, (Vector3) { 0, 9.0f, -10.0f }, 0, 2);

	Mesh wall2 = GenMeshCube(50.0f, 20.0f, 1.0f);
	AddEntity(scene, wall2, (Vector3) { 0.0f, 9.0f, 10.0f }, 0, 3);

	Mesh wall3 = GenMeshCube(1.0f, 20.0f, 50.0f);
	AddEntity(scene, wall3, (Vector3) { -10.0f, 9.0f, 0.0f }, 0, 3);

	Mesh wall4 = GenMeshCube(1.0f, 20.0f, 50.0f);
	AddEntity(scene, wall4, (Vector3) { 10.0f, 9.0f, 10.0f }, 0, 3);

    Mesh lightSphere = GenMeshSphere(3.0f, 16, 16);
    AddEntity(scene, lightSphere, (Vector3) { 5.0f, 10.0f, 5.0f }, 5, 3);

    Mesh sphere = GenMeshSphere(2.0f, 32, 32);
    AddEntity(scene, sphere, (Vector3) { 0, 2.0f, 0 }, 4, 1);
}

// --- STATE MANAGEMENT ---

SceneState InitSceneState(void) {
    SceneState state = { 0 };

    state.camera.position = (Vector3){ 5.0f, 5.0f, 5.0f };
    state.camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    state.camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    state.camera.fovy = 45.0f;
    state.camera.projection = CAMERA_PERSPECTIVE;
    state.lastCamPos = state.camera.position;
    state.lastCamTarget = state.camera.target;
    state.showGizmo = false;
    state.lightPos = (Vector3){ 0.0f, 15.0f, 0.0f };

    Scene tempScene = InitScene(50000, 100);
    LoadScene_ComplexModel(&tempScene);
    UploadSceneToGPU(&tempScene, &state.ssboTris, &state.ssboObjs);
    state.triCount = tempScene.triCount;
    state.objCount = tempScene.objCount;
    FreeSceneRAM(&tempScene);

    return state;
}

bool HandleSceneInput( SceneState* state) {
    if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_THREE)) {
        Scene newScene = InitScene(100000, 2000);

        if (IsKeyPressed(KEY_ONE)) LoadScene_ComplexModel(&newScene);
        if (IsKeyPressed(KEY_TWO)) LoadScene_StressTest(&newScene);
		if (IsKeyPressed(KEY_THREE)) LoadScene_Mirrors(&newScene);

        UploadSceneToGPU(&newScene, &state->ssboTris, &state->ssboObjs);
        state->triCount = newScene.triCount;
        state->objCount = newScene.objCount;
        FreeSceneRAM(&newScene);
        return true;
    }
    return false;
}

bool UpdateSceneCamera(SceneState* state) {
    if (IsKeyPressed(KEY_TAB)) {
        if (IsCursorHidden()) EnableCursor(); else DisableCursor();
    }
    if (IsKeyPressed(KEY_G)) state->showGizmo = !state->showGizmo;

    if (IsCursorHidden()) {
        UpdateCamera(&state->camera, CAMERA_FREE);
    }

    if (Vector3Distance(state->camera.position, state->lastCamPos) > 0.001f ||
        Vector3Distance(state->camera.target, state->lastCamTarget) > 0.001f) {
        state->lastCamPos = state->camera.position;
        state->lastCamTarget = state->camera.target;
        return true;
    }
    return false;
}

void DrawSceneGizmo(SceneState* state) {
    if (!state->showGizmo) return;
    BeginMode3D(state->camera);
    DrawGrid(20, 1.0f);
    DrawLine3D((Vector3) { 0, 0, 0 }, (Vector3) { 1, 0, 0 }, RED);
    DrawLine3D((Vector3) { 0, 0, 0 }, (Vector3) { 0, 1, 0 }, GREEN);
    DrawLine3D((Vector3) { 0, 0, 0 }, (Vector3) { 0, 0, 1 }, BLUE);
    DrawSphereWires(state->lightPos, 0.5f, 8, 8, YELLOW);
    EndMode3D();
}

void UnloadSceneState(SceneState* state) {
    if (state->ssboTris != 0) rlUnloadShaderBuffer(state->ssboTris);
    if (state->ssboObjs != 0) rlUnloadShaderBuffer(state->ssboObjs);
}