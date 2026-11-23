#ifndef MESHBUILDER_H
#define MESHBUILDER_H

#include <QVector2D>
#include <QVector3D>
#include <vector>

// ================================================================
//  MeshBuilder
//  Procedural geometry for the battle view. Every function returns
//  a flat list of interleaved vertices for GL_TRIANGLES:
//      position.xyz, normal.xyz   (6 floats per vertex)
//  Hard-surface parts use flat normals (faceted spaceship look),
//  the sphere uses smooth normals.
// ================================================================

namespace MeshBuilder {

using VertexData = std::vector<float>;
constexpr int kFloatsPerVertex = 6;

// Axis-aligned box centered on the origin
VertexData box(const QVector3D &size);

// 2D outline drawn in the XZ plane (QVector2D(x, z)), extruded along
// Y and centered on y = 0. The outline must be star-shaped around its
// centroid (any convex shape, arrows, fans…).
VertexData prism(const std::vector<QVector2D> &outline, float thickness);

// Cone / truncated cone along +Y, base at y = 0
VertexData frustum(int segments, float bottomRadius, float topRadius, float height);

// Unit sphere with smooth normals
VertexData sphere(int rings, int sectors);

// Outline of a disc sector (center first, then the arc), for prism()
std::vector<QVector2D> sector(const QVector2D &center, float radius,
                              float startDeg, float endDeg, int steps);

} // namespace MeshBuilder

#endif // MESHBUILDER_H
