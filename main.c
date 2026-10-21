#define GRAPHICS_API_OPENGL_43

#include "raylib.h"
#include "scene_manager.h"
#include "shader_manager.h"

int main() {
    const int screenWidth = 1500;
    const int screenHeight = 860;

    InitWindow(screenWidth, screenHeight, "GPU Path Tracer");
    SetTargetFPS(75);
    DisableCursor();

    // --- INIT ---

    // 1. Scene
    SceneState scene = InitSceneState();

    // 2. Renderer
    RendererState renderer = InitRenderer(screenWidth, screenHeight, "rt_main.glsl");

    rlEnableShader(renderer.id);
    UploadStaticUniforms(&renderer, &scene);
    rlDisableShader();

    // --- MAIN LOOP ---
    while (!WindowShouldClose()) {

        // 1. INPUT & UPDATE
        // SprawdŸ czy scena zosta³a zmieniona (Klawisze 1, 2)
        if (HandleSceneInput(&scene)) {
            // Jeœli tak, zaktualizuj shader o nowe liczniki trójk¹tów
            rlEnableShader(renderer.id);
            UploadStaticUniforms(&renderer, &scene);
            rlDisableShader();

            renderer.accumFrame = 1; // Reset akumulacji
        }

        // SprawdŸ czy kamera siê ruszy³a
        if (UpdateSceneCamera(&scene)) {
            renderer.accumFrame = 1;
        }

        // Obs³uga shadera (Skala, Reload, Mode)
        HandleRendererInput(&renderer, &scene);


        // 2. GPU COMPUTE PASS (Raytracing)
        if (renderer.accumFrame > 1000) renderer.accumFrame = 1000; // Cap
        else renderer.accumFrame++; // W innym wypadku main.c decydowa³, tu robi to renderer na podstawie inputu

        RenderPathTracer(&renderer, &scene);


        // 3. DRAW PASS (Wyœwietlanie)
        BeginDrawing();
        ClearBackground(BLACK);

        // Rysuj wynik Raytracera
        DrawRendererResult(&renderer);

        // Rysuj Gizmo (na wierzchu)
        DrawSceneGizmo(&scene);

        DrawFPS(screenWidth - 100, 10);
        EndDrawing();
    }

    // --- CLEANUP ---
    UnloadSceneState(&scene);
    UnloadRenderer(&renderer);
    CloseWindow();

    return 0;
}