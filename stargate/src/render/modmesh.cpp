#include "modmesh.h"

#include <QFile>
#include <QVector3D>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <limits>

namespace {

constexpr qsizetype kCountOffset = 0x35;
constexpr qsizetype kVertexOffset = 0x3d;
constexpr qsizetype kVertexSize = 49;
constexpr quint32 kMaxVertices = 2'000'000;

float readFloat(const char *p)
{
    const quint32 bits = qFromLittleEndian<quint32>(p);
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// Mod axes: forward Z, up Y -> battle view: forward X, up Y
QVector3D toView(const QVector3D &v)
{
    return QVector3D(v.z(), v.y(), -v.x());
}

} // namespace

MeshBuilder::VertexData ModMesh::load(const QString &path)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    const char *data = bytes.constData();
    const qsizetype size = bytes.size();
    if (size < kVertexOffset || data[0] != 1)
        return {};

    const quint32 vertexCount = qFromLittleEndian<quint32>(data + kCountOffset);
    const qsizetype indexOffset = kVertexOffset + qsizetype(vertexCount) * kVertexSize;
    if (vertexCount == 0 || vertexCount > kMaxVertices || indexOffset + 4 > size)
        return {};
    const quint32 indexCount = qFromLittleEndian<quint32>(data + indexOffset);
    if (indexCount == 0 || indexCount % 3 != 0 || indexOffset + 4 + qsizetype(indexCount) * 4 > size)
        return {};

    // Vertices: position and normal, in view axes
    std::vector<QVector3D> positions(vertexCount), normals(vertexCount);
    QVector3D low(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max());
    QVector3D high = -low;
    for (quint32 i = 0; i < vertexCount; ++i) {
        const char *v = data + kVertexOffset + qsizetype(i) * kVertexSize;
        positions[i] = toView({readFloat(v), readFloat(v + 4), readFloat(v + 8)});
        normals[i] = toView({readFloat(v + 12), readFloat(v + 16), readFloat(v + 20)}).normalized();
        for (int a = 0; a < 3; ++a) {
            low[a] = std::min(low[a], positions[i][a]);
            high[a] = std::max(high[a], positions[i][a]);
        }
    }
    const float length = high.x() - low.x();
    if (!(length > 0.0f))
        return {};
    const QVector3D center = (low + high) / 2.0f;

    // Triangles unrolled (the battle view draws non-indexed vertices)
    MeshBuilder::VertexData out;
    out.reserve(size_t(indexCount) * MeshBuilder::kFloatsPerVertex);
    const char *indices = data + indexOffset + 4;
    for (quint32 i = 0; i < indexCount; ++i) {
        const quint32 index = qFromLittleEndian<quint32>(indices + qsizetype(i) * 4);
        if (index >= vertexCount)
            return {};
        const QVector3D p = (positions[index] - center) / length;
        const QVector3D &n = normals[index];
        out.insert(out.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z()});
    }
    return out;
}
