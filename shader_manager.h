#ifndef SHADER_MANAGER_H
#define SHADER_MANAGER_H

#include "raylib.h"
#include "scene_manager.h" // Needs SceneState

// Renderer state
typedef struct {
    // Shader
    unsigned int id;
    char* filePath;
    // Locations
    int locTriangleCount, locObjectCount, locCamPos, locCamTarget;
    int locFrame, locAccumFrame, locLightPos, locRenderMode;

    // Buffers
    RenderTexture2D target;
    RenderTexture2D idBuffer;

    // State
    int screenWidth;
    int screenHeight;
    float renderScale;
    int frameCounter;
    int accumFrame;
    int renderMode; // 0=Preview, 1=PT, 2=Heatmap, 3=CacheDebug
} RendererState;

RendererState InitRenderer(int width, int height, const char* shaderPath);

// Call compute shader
void RenderPathTracer(RendererState* renderer, SceneState* scene);

void DrawRendererResult(RendererState* renderer);

// Renderer input (scale, reload, mode switch)
void HandleRendererInput(RendererState* renderer, SceneState* scene);

// Clear
void UnloadRenderer(RendererState* renderer);

// Helpers
void UploadStaticUniforms(RendererState* renderer, SceneState* scene);
RenderTexture2D LoadIdBuffer(int width, int height);
RenderTexture2D InitRenderBuffer(int width, int height, float scale);

#endif