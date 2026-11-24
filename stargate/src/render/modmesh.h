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
//  length of 1. Its color texture comes from its first material:
//  mesh_materials/<material>.mesh_material -> base_color_texture ->
//  textures/<texture>.dds. No data when the file is missing or not
//  in that format: the caller keeps its built-in shape.
// ================================================================

namespace ModMesh {

constexpr int kFloatsPerVertex = 8;    // position, normal, uv

struct Model
{
    std::vector<float> vertices;        // triangles, kFloatsPerVertex floats per vertex
    QString texturePath;                // color texture (.dds), empty if none
};

Model load(const QString &path);

} // namespace ModMesh

#endif // MODMESH_H
