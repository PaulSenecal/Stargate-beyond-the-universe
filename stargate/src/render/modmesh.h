#ifndef MODMESH_H
#define MODMESH_H

#include <QString>

#include "meshbuilder.h"

// ================================================================
//  ModMesh
//  Reads a ship model of the Stargate Reborn mod (Sins of a Solar
//  Empire II .mesh, local assets): header (61 bytes, vertex count at
//  0x35), vertices of 49 bytes (position, normal, tangent, sign, uv,
//  flag), then the triangle indices (count + uint32). The model is
//  turned to the battle view axes (forward +X, up +Y), centered and
//  scaled to a length of 1. Returns no data when the file is missing
//  or not in that format: the caller keeps its built-in shape.
// ================================================================

namespace ModMesh {

MeshBuilder::VertexData load(const QString &path);

} // namespace ModMesh

#endif // MODMESH_H
