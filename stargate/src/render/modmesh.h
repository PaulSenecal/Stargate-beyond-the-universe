#ifndef MODMESH_H
#define MODMESH_H

#include <QImage>
#include <QRectF>
#include <QString>
#include <QVector3D>

#include <vector>

// ================================================================
//  ModMesh
//  Reads a ship model of the Stargate Reborn mod (Sins of a Solar
//  Empire II .mesh, local assets): header (61 bytes, vertex count at
//  0x35), vertices of 49 bytes (position, normal, tangent, sign, uv,
//  flag), the triangle indices (count, a zero uint32, then uint32 each), then a block with
//  attach points and the material names. The model is turned to the
//  battle view axes (forward +X, up +Y), centered and scaled to a
//  length of 1. Its textures come from its first material:
//  mesh_materials/<material>.mesh_material (color, mask: lights in
//  blue, occlusion-roughness-metallic, normal map) -> textures/*.dds. No data when the file is missing or not
//  in that format: the caller keeps its built-in shape.
// ================================================================

namespace ModMesh {

constexpr int kFloatsPerVertex = 12;   // position, normal, uv, tangent + sign

// A named attach point of the model (weapon.main-0, exhaust.2,
// child.<turret>...), in the normalized model space
struct AttachPoint
{
    QString name;
    QVector3D pos;
};

struct Model
{
    std::vector<float> vertices;        // triangles, kFloatsPerVertex floats per vertex
    QString texturePath;                // color texture (.dds), empty if none
    QString maskPath;                   // lights (blue channel)
    QString ormPath;                    // occlusion, roughness, metallic
    QString normalPath;                 // normal map (BC5)
    std::vector<AttachPoint> points;    // weapons, exhausts, hangars...
    std::vector<QVector3D> engines;     // exhausts, or glowing spots found at the rear
    QVector3D halfExtents;              // half size of the hull (normalized space)
    QVector3D center;                   // normalization of the file: center and length
    float length = 0;
    QImage silhouette;                  // seen from above (x right, z down), for the status schematic
    QRectF silhouetteRect;              // area it covers, in normalized (x, z)
};

// frame: normalize with the center and length of another model (the
// shield model of a ship must fit the ship itself)
Model load(const QString &path, const Model *frame = nullptr);

} // namespace ModMesh

#endif // MODMESH_H
