#ifndef TYPES_H
#define TYPES_H

#include "raylib.h"
#include <stdbool.h> 

#define MAX_BOUNCES 8

// TRIS
typedef struct {
    // xyz position + padding w
    Vector4 p0;
    Vector4 p1;
    Vector4 p2;
    // xyz normal + w=is_dynamic
    Vector4 normal;
} Triangle;
// GPU OBJECT (AABB + GEOMETRY)
typedef struct {
    Vector4 aabbMin;
    Vector4 aabbMax;

    int startIndex;    // First object triangle index in a global buffer
    int triangleCount;
    int materialID;
    int objectID;
} GpuObject;

// RAY CONFIG
typedef struct {
    Vector3 origin;
    Vector3 direction;
    bool active;
} BeamConfig;

// LIGHT PATH
typedef struct LightPath {
    Vector3 vertices[MAX_BOUNCES + 1];

    int obj_indices[MAX_BOUNCES];
    bool hit_dynamic[MAX_BOUNCES];
    bool from_cache[MAX_BOUNCES];

    int triCount;
} LightPath;

// BEAM SYSTEM
typedef struct {
    BeamConfig* emitters;
    LightPath* beams;
    int beam_count;
} BeamSystem;

#endif