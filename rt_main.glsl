#version 430

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// --- BUFFERS ---
// Output: RGBA8 (Screen)
layout(rgba8, binding = 0) uniform image2D imgOutput;
// ID Buffer: RGBA32F (R=ObjID, G=TriID, B=DIST, A=Reserved)
layout(rgba32f, binding = 3) uniform image2D idBuffer;

// Geometry (Read-only)
struct Triangle { vec4 p0; vec4 p1; vec4 p2; vec4 normal; };
struct Object { vec4 aabbMin; vec4 aabbMax; int startIndex; int triangleCount; int materialID; int objectID; };

layout(std430, binding = 1) readonly buffer TriangleBuffer { Triangle allTriangles[]; };
layout(std430, binding = 2) readonly buffer ObjectBuffer { Object objects[]; };

// --- STRUCTS ---
struct HitInfo {
    bool didHit;
    float dist;
    vec3 position;
    vec3 normal;
    float matID;
    int objectID;
    
    // Debug Performance
    int cost;          // Liczba sprawdzonych trójk¹tów/boxów
    bool fromCache;    // Czy trafiliœmy dziêki pamiêci podrêcznej?
};

struct Material { vec3 albedo; float roughness; float emission; };

// --- UNIFORMS ---
uniform int triangleCount;
uniform int objectCount;
uniform vec3 camPos;
uniform vec3 camTarget;
uniform int frame;
uniform int accumFrame;
uniform vec3 lightPos;
uniform int renderMode; // 0=Preview, 1=PT, 2=Heatmap

// --- MATERIALS (Hardcoded) ---
const Material materials[6] = Material[](
    Material(vec3(0.6), 0.3, 0.0),       // 0: Floor
    Material(vec3(0.9), 1.0, 0.0),       // 1: Wall
    Material(vec3(0.8, 0.8, 0.8), 0.0, 0.0), // 2: Mirror
    Material(vec3(0.1, 1.0, 0.1), 0.3, 0.0), // 3: Green
    Material(vec3(0.1, 0.1, 1.0), 0.8, 0.0), // 4: Blue
    Material(vec3(1.0), 1.0, 10.0)       // 5: Emitter
);
Material GetMaterial(int id) { if (id < 0 || id >= 6) return materials[0]; return materials[id]; }

// --- RNG ---
uint rngState;
void InitRNG(vec2 pixel, int frame) { rngState = uint(pixel.x * 1973 + pixel.y * 9277 + frame * 26699) | 1u; }
float RandomFloat() {
    rngState = rngState * 747796405u + 2891336453u;
    uint state = ((rngState >> ((rngState >> 28u) + 4u)) ^ rngState) * 277803737u;
    uint word = (state >> 22u) ^ state;
    return float(word) / 4294967295.0;
}
vec3 RandomInUnitSphere() {
    float z = RandomFloat() * 2.0 - 1.0; float a = RandomFloat() * 6.28318530718; float r = sqrt(1.0 - z * z);
    return vec3(r * cos(a), r * sin(a), z);
}

// --- PHYSICS (INTERSECTIONS) ---
bool IntersectTriangle(vec3 rayOrigin, vec3 rayDir, Triangle t, out float tOut) {
    if (dot(rayDir, t.normal.xyz) > 0.0) return false; 

    const float EPSILON = 0.0000001;
    vec3 v0 = t.p0.xyz; vec3 v1 = t.p1.xyz; vec3 v2 = t.p2.xyz;
    vec3 edge1 = v1 - v0; vec3 edge2 = v2 - v0;
    vec3 h = cross(rayDir, edge2); float a = dot(edge1, h);
    if (a > -EPSILON && a < EPSILON) return false; 

    float f = 1.0 / a; vec3 s = rayOrigin - v0; float u = f * dot(s, h);
    if (u < 0.0 || u > 1.0) return false;

    vec3 q = cross(s, edge1); float v = f * dot(rayDir, q);
    if (v < 0.0 || u + v > 1.0) return false;

    float dist = f * dot(edge2, q);
    if (dist > EPSILON) { tOut = dist; 
    return true;}
    return false;
}

float IntersectBox(vec3 rayOrigin, vec3 invDir, vec3 boxMin, vec3 boxMax) {
    vec3 tMin = (boxMin - rayOrigin) * invDir; vec3 tMax = (boxMax - rayOrigin) * invDir;
    vec3 t1 = min(tMin, tMax); vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z); float tFar = min(min(t2.x, t2.y), t2.z);
    if (tNear > tFar || tFar < 0.0) return -1.0;
    return max(tNear, 0.0);
}

// --- TRACE SCENE (Cache + Fallback) ---
HitInfo TraceScene(vec3 rayOrigin, vec3 rayDir, ivec2 pixel) {
    HitInfo bestHit;
    bestHit.didHit = false;
    bestHit.dist = 1e30;
    bestHit.objectID = -1;
    bestHit.fromCache = false;
    bestHit.cost = 0; // Cost reset

    vec3 invDir = 1.0 / (rayDir + vec3(1e-6));

    // Prev frame cache read
    vec4 cacheData = imageLoad(idBuffer, pixel);
    int cachedObjID = int(cacheData.r);
    int cachedTriID = int(cacheData.g);

    int hitObjIndex = -1; 
    int hitTriIndex = -1;

    // --- 0: FAST PATH (Cache) ---
    if (cachedObjID >= 0 && cachedObjID < objectCount) {
        Object obj = objects[cachedObjID];
        bestHit.cost++;
        bestHit.fromCache = true;
        if (IntersectBox(rayOrigin, invDir, obj.aabbMin.xyz, obj.aabbMax.xyz) >= 0.0) {
            if (cachedTriID >= 0 && cachedTriID < triangleCount) {
                bestHit.cost++;//Cache hit cost
                float dist;
                if (IntersectTriangle(rayOrigin, rayDir, allTriangles[cachedTriID], dist)) {
                    // Cache hit
                    bestHit.didHit = true;
                    bestHit.dist = dist;
                    bestHit.normal = allTriangles[cachedTriID].normal.xyz;
                    bestHit.position = rayOrigin + rayDir * dist;
                    bestHit.matID = float(obj.materialID);
                    bestHit.objectID = obj.objectID;

                    bestHit.fromCache = true;
                    
                    hitObjIndex = cachedObjID;
                    hitTriIndex = cachedTriID;
                }
            }
        }
    }

    // --- 1: SLOW PATH (Full Scan) ---
    float maxDistSearch = bestHit.didHit ? bestHit.dist : 1e30;

    //In case of occlusion scan with maxdist limit
    for(int i = 0; i < objectCount; i++) {
        bestHit.cost++;
        float boxDist = IntersectBox(rayOrigin, invDir, objects[i].aabbMin.xyz, objects[i].aabbMax.xyz);
        
        // Skip if AABB is further than current hit
        if (boxDist < 0.0 || boxDist > maxDistSearch) continue;

        int start = objects[i].startIndex;
        int end = start + objects[i].triangleCount;

        for(int t = start; t < end; t++) {
            //High tier optimization
            if (bestHit.fromCache && t == cachedTriID) continue;

            bestHit.cost++;
            float dist;
            if (IntersectTriangle(rayOrigin, rayDir, allTriangles[t], dist)) {
                if (dist < bestHit.dist) {
                    bestHit.didHit = true;
                    bestHit.dist = dist;
                    bestHit.normal = allTriangles[t].normal.xyz;
                    bestHit.position = rayOrigin + rayDir * dist;
                    bestHit.matID = float(objects[i].materialID);
                    bestHit.objectID = objects[i].objectID;
                    bestHit.fromCache = false;
                    
                    maxDistSearch = dist;
                    hitObjIndex = i;
                    hitTriIndex = t;
                }
            }
        }
    }
    
    // Cache save
    imageStore(idBuffer, pixel, vec4(float(hitObjIndex), float(hitTriIndex), bestHit.dist, 0.0));

    return bestHit;
}

// --- RENDERERS ---

vec3 RenderPreview(vec3 rayOrigin, vec3 rayDir, ivec2 pixel) {
    HitInfo hit = TraceScene(rayOrigin, rayDir, pixel);
    if (!hit.didHit) return vec3(0.15); // T³o

    vec3 lightDir = normalize(vec3(0.5, 1.0, -0.5));
    float diff = max(dot(hit.normal, lightDir), 0.1);
    Material mat = GetMaterial(int(hit.matID));
    return mat.albedo * (diff + mat.emission);
}

// HEATMAP
vec3 RenderHeatmap(vec3 rayOrigin, vec3 rayDir, ivec2 pixel) {
    HitInfo hit = TraceScene(rayOrigin, rayDir, pixel);
    if (!hit.didHit) return vec3(0.0, 0.0, 0.2); // Dark background

    float stress = float(hit.cost) / 800.0;
    stress = clamp(stress, 0.0, 1.0);

    if(hit.fromCache)return vec3(0.0,0.0,1.0);

    // Gradient green->red
    return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), stress);
}

// STANDARD PATH TRACER
vec3 RenderPathTrace(vec3 rayOrigin, vec3 rayDir, ivec2 pixel) {
    vec3 accColor = vec3(0.0);
    vec3 throughput = vec3(1.0);
    
    vec3 ambientBase = vec3(0.02); 

    for (int bounce = 0; bounce < 6; bounce++) {
        HitInfo hit = TraceScene(rayOrigin, rayDir, pixel);

        // Miss (Skybox)
        if (!hit.didHit) {
            accColor += vec3(0.05) * throughput;
            ambientBase = vec3(0.0); 
            break;
        }

        Material mat = GetMaterial(int(hit.matID));

        // Emission
        if (mat.emission > 0.0) {
            accColor += mat.albedo * mat.emission * throughput;
            ambientBase = vec3(0.0);
            break;
        }

        throughput *= mat.albedo;

        // Next Ray Calculation (Standard Monte Carlo)
        vec3 pureReflect = reflect(rayDir, hit.normal);
        vec3 toLight = normalize(lightPos - hit.position); // Light biias
        
        float bias = clamp(float(bounce) * 0.1, 0.0, 0.5); //Set multiplier to 0 for no bias
        vec3 guideDir = normalize(mix(pureReflect, toLight, bias));

        vec3 nextRayDir;
        if (mat.roughness < 0.01) {
            nextRayDir = guideDir; // Lustro
        } else {
            // RNG
            vec3 rand = RandomInUnitSphere();
            nextRayDir = normalize(guideDir + rand * mat.roughness);
        }
        
        if (dot(nextRayDir, hit.normal) < 0.0) nextRayDir = -nextRayDir;

        rayDir = nextRayDir;
        rayOrigin = hit.position + hit.normal * 0.001;
    }

    return accColor + (ambientBase * throughput);
}

// --- MAIN ---
void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(imgOutput);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    InitRNG(pixel, frame);
    // Jitter (Anty aliasing)
    vec2 jitter = (renderMode == 1) ? vec2(RandomFloat(), RandomFloat()) : vec2(0.5);

    vec2 uv = ((vec2(pixel) + jitter) / vec2(size)) * 2.0 - 1.0;
    float aspect = float(size.x) / float(size.y);
    uv.x *= aspect;

    vec3 fwd = normalize(camTarget - camPos);
    vec3 right = normalize(cross(fwd, vec3(0,1,0)));
    vec3 up = cross(right, fwd);
    vec3 rayDir = normalize(fwd * 1.5 + right * uv.x + up * uv.y);

    vec3 finalColor;
    switch (renderMode) {
        case 0: finalColor = RenderPreview(camPos, rayDir, pixel); break;
        case 1: finalColor = RenderPathTrace(camPos, rayDir, pixel); break;
        case 2: finalColor = RenderHeatmap(camPos, rayDir, pixel); break;
        case 3: finalColor = RenderHeatmap(camPos, rayDir, pixel); break;
        default: finalColor = vec3(1,0,1); break;
    }

    // Accumulation
    if (accumFrame > 1 && renderMode == 1) {
        vec3 old = imageLoad(imgOutput, pixel).rgb;
        finalColor = mix(old, finalColor, 1.0 / float(accumFrame));
    }

    imageStore(imgOutput, pixel, vec4(finalColor, 1.0));
}