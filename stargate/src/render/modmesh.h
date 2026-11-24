#ifndef MODMESH_H
#define MODMESH_H

#include <QString>

#include <vector>

// ================================================================
//  ModMesh
//  Reads a ship model of the Stargate Reborn mod (Sins of a Solar
//  Empire II .mesh, local assets): header (61 bytes, vertex count at
//  0x35), vertices of 49 bytes (position, normal, tangent, sign, uv,
//  flag), the triangle indices (count + uint32), then a block with
//  attach points and the material names. The model is turned to the
//  battle view axes (forward +X, up +Y), centered and scaled to a
//  length of 1. Its textures come from its first material:
//  mesh_materials/<material>.mesh_material (color, mask: lights in
//  blue, occlusion-roughness-metallic, normal map) -> textures/*.dds. No data when the file is missing or not
//  in that format: the caller keeps its built-in shape.
// ================================================================

namespace ModMesh {

constexpr int kFloatsPerVertex = 12;   // position, normal, uv, tangent + sign

struct Model
{
    std::vector<float> vertices;        // triangles, kFloatsPerVertex floats per vertex
    QString texturePath;                // color texture (.dds), empty if none
    QString maskPath;                   // lights (blue channel)
    QString ormPath;                    // occlusion, roughness, metallic
    QString normalPath;                 // normal map (BC5)
};

Model load(const QString &path);

} // namespace ModMesh

#endif // MODMESH_H
