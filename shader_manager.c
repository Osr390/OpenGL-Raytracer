#define GRAPHICS_API_OPENGL_43
#include "shader_manager.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- OPENGL FUNCTIONS ---

// Creates render texture with given scale (for dynamic resizing)
RenderTexture2D InitRenderBuffer(int width, int height, float scale) {
    int w = (int)(width * scale);
    int h = (int)(height * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    RenderTexture2D rt = LoadRenderTexture(w, h);
    //Bilinear filter for less pixelated view
    SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
    return rt;
}

// Creates an IDBuffer for cache use (R32F)
RenderTexture2D LoadIdBuffer(int width, int height) {
    RenderTexture2D rt = { 0 };

    // 1. Create empty FBO
    rt.id = rlLoadFramebuffer();

    if (width > 0 && height > 0) {
        rt.texture.width = width;
        rt.texture.height = height;
        rt.texture.format = PIXELFORMAT_UNCOMPRESSED_R32;
        rt.texture.mipmaps = 1;

        // 2. Create texture
        rt.texture.id = rlLoadTexture(NULL, width, height, rt.texture.format, 1);

        // Point filter to avoid interpolation
        SetTextureFilter(rt.texture, TEXTURE_FILTER_POINT);

        // 3. Texture to FBO
        rlEnableFramebuffer(rt.id);
        rlFramebufferAttach(rt.id, rt.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);

        if (rlFramebufferComplete(rt.id)) {
            printf("[SHADER] ID Buffer Created (R32F, %dx%d)\n", width, height);
        }
        else {
            printf("[SHADER] CRITICAL: ID Buffer Failed!\n");
        }
        rlDisableFramebuffer();
    }
    return rt;
}


unsigned int CompileComputeShader(char* code) {
    unsigned int shader = rlCompileShader(code, RL_COMPUTE_SHADER);
    if (shader == 0) return 0;
    unsigned int program = rlLoadComputeShaderProgram(shader);
    if (program == 0) printf("[SHADER] Link Error.\n");
    return program;
}

void LoadShaderAndLocs(RendererState* r, const char* path) {
    char* code = LoadFileText(path);
    if (!code) {
        printf("[SHADER] Error: File not found %s\n", path);
        return;
    }

    unsigned int newProgram = CompileComputeShader(code);
    UnloadFileText(code);

    if (newProgram != 0) {
        if (r->id != 0) rlUnloadShaderProgram(r->id);
        r->id = newProgram;

        // Take locations
        r->locTriangleCount = rlGetLocationUniform(r->id, "triangleCount");
        r->locObjectCount = rlGetLocationUniform(r->id, "objectCount");
        r->locCamPos = rlGetLocationUniform(r->id, "camPos");
        r->locCamTarget = rlGetLocationUniform(r->id, "camTarget");
        r->locFrame = rlGetLocationUniform(r->id, "frame");
        r->locAccumFrame = rlGetLocationUniform(r->id, "accumFrame");
        r->locLightPos = rlGetLocationUniform(r->id, "lightPos");
        r->locRenderMode = rlGetLocationUniform(r->id, "renderMode");

        printf("[SHADER] Shader Loaded & Linked. ID: %d\n", r->id);
    }
}

// --- PUBLIC FUNCTIONS ---

RendererState InitRenderer(int width, int height, const char* shaderPath) {
    RendererState r = { 0 };
    r.screenWidth = width;
    r.screenHeight = height;
    r.renderScale = 1.0f;
    r.accumFrame = 1;
    r.renderMode = 0;

	// File path copy (for hot reload)
    int len = strlen(shaderPath);
    r.filePath = (char*)malloc(len + 1);
    strcpy(r.filePath, shaderPath);

    // Init
    LoadShaderAndLocs(&r, shaderPath);
    r.target = InitRenderBuffer(width, height, r.renderScale);
    r.idBuffer = LoadIdBuffer(width, height);

    return r;
}

void UploadStaticUniforms(RendererState* r, SceneState* s) {
    if (r->id == 0) return;
    rlSetUniform(r->locTriangleCount, &s->triCount, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(r->locObjectCount, &s->objCount, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(r->locLightPos, &s->lightPos, RL_SHADER_UNIFORM_VEC3, 1);
}

void RenderPathTracer(RendererState* r, SceneState* s) {
    if (r->id == 0) return;

    rlEnableShader(r->id);

    // Binding:
    // 0: Output Color (RenderTexture)
    rlBindImageTexture(r->target.texture.id, 0, r->target.texture.format, false);

    // 1: Triangles SSBO
    rlBindShaderBuffer(s->ssboTris, 1);

    // 2: Objects SSBO
    rlBindShaderBuffer(s->ssboObjs, 2);

    // 3: ID Cache (R32F)
    rlBindImageTexture(r->idBuffer.texture.id, 3, r->idBuffer.texture.format, false);

    // Uniforms
    rlSetUniform(r->locCamPos, &s->camera.position, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(r->locCamTarget, &s->camera.target, RL_SHADER_UNIFORM_VEC3, 1);
    rlSetUniform(r->locFrame, &r->frameCounter, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(r->locAccumFrame, &r->accumFrame, RL_SHADER_UNIFORM_INT, 1);
    rlSetUniform(r->locRenderMode, &r->renderMode, RL_SHADER_UNIFORM_INT, 1);

    // Dispatch
    unsigned int groupX = (r->target.texture.width + 7) / 8;
    unsigned int groupY = (r->target.texture.height + 7) / 8;
    rlComputeShaderDispatch(groupX, groupY, 1);

    rlDisableShader();

    r->frameCounter++;
}

void HandleRendererInput(RendererState* r, SceneState* s) {
	// 1. Scale Adjustment
    if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_RIGHT_BRACKET)) {
        if (IsKeyPressed(KEY_LEFT_BRACKET)) r->renderScale -= 0.1f;
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) r->renderScale += 0.1f;
        if (r->renderScale < 0.1f) r->renderScale = 0.1f;
        if (r->renderScale > 1.0f) r->renderScale = 1.0f;

        UnloadRenderTexture(r->target);
        r->target = InitRenderBuffer(r->screenWidth, r->screenHeight, r->renderScale);
        r->accumFrame = 1;
        printf("RENDER SCALE: %.2f\n", r->renderScale);
    }

    // 2. Hot Reload
    if (IsKeyPressed(KEY_F5) || IsKeyPressed(KEY_R)) {
        LoadShaderAndLocs(r, r->filePath);

        // Static uniforms reupload
        rlEnableShader(r->id);
        UploadStaticUniforms(r, s);
        rlDisableShader();
        r->accumFrame = 1;
    }

    // 3. Render Mode
    if (IsKeyPressed(KEY_L)) {
        r->renderMode = (r->renderMode + 1) % 4; // 0..3
        r->accumFrame = 1;
        printf("MODE: %d\n", r->renderMode);
    }
}

void DrawRendererResult(RendererState* r) {
    if (r->id != 0) {
        // Render texture (screen)
        DrawTexturePro(r->target.texture,
            (Rectangle) {
            0, 0, (float)r->target.texture.width, -(float)r->target.texture.height
        },
            (Rectangle) {
            0, 0, (float)r->screenWidth, (float)r->screenHeight
        },
            (Vector2) {
            0, 0
        }, 0.0f, WHITE);

        char buffer[64];
        sprintf(buffer, "Scale: %.2f | Res: %dx%d | SPP: %d",
            r->renderScale, r->target.texture.width, r->target.texture.height, r->accumFrame);
        DrawText(buffer, 10, 40, 20, GREEN);
    }
    else {
        DrawText("SHADER ERROR", 100, 300, 30, RED);
    }
}

void UnloadRenderer(RendererState* r) {
    UnloadRenderTexture(r->target);
    rlUnloadTexture(r->idBuffer.texture.id);
    rlUnloadFramebuffer(r->idBuffer.id);

    if (r->id != 0) rlUnloadShaderProgram(r->id);
    free(r->filePath);
}