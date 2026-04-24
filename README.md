## TTRT (Temporal Tales Ray Transform)
#Overview
A real-time, brute-force software path tracer written in pure C (C23). It utilizes OpenGL Compute Shaders to simulate realistic light transport without relying on hardware-accelerated DXR.

#Technical Details
Core: Brute-force path tracing via GLSL Compute Shaders. Dynamic lighting, soft shadows, and global illumination emerge naturally from the simulation.

#Intersections: 
Custom math for Ray-Sphere and Ray-AABB. Includes a basic caching mechanism for fast AABB checks.

#Materials:
Support for diffuse, metallic, and refractive (dielectric) surfaces.

#Stack:
C23, OpenGL 4.3+, Raylib / rlgl.

#Performance & Limitations
The engine performs well (approx. 40 FPS on an RTX 4060 Ti) on scenes with basic primitives and low triangle counts thanks to fast AABB checks.

#However, because it lacks a spatial hashing system like Bounding Volume Hierarchy (BVH), performance drops significantly (to ~1 FPS) when rendering complex geometry like the Utah Teapot. Recognizing the limits of software path tracing led me to pivot to my current project: a DirectX 12 engine utilizing hardware DXR.

#How to Build
Simply compile the C source files using any IDE or compiler configured with Raylib. Ensure the shader files are located in the correct relative directory for runtime compilation.
