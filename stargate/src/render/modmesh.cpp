#include "modmesh.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector2D>
#include <QVector3D>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <limits>

#include "src/core/assets.h"

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

// First material of the model: a length-prefixed name in the trailing
// block that has a .mesh_material file; fills the texture paths
void findTextures(const char *data, qsizetype from, qsizetype size, ModMesh::Model &model)
{
    for (qsizetype i = from; i + 4 < size; ++i) {
        const quint32 length = qFromLittleEndian<quint32>(data + i);
        if (length < 3 || length > 96 || i + 4 + qsizetype(length) > size)
            continue;
        const QByteArray name(data + i + 4, qsizetype(length));
        if (!std::all_of(name.cbegin(), name.cend(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            }))
            continue;
        const QString material = Assets::modFile(QStringLiteral("mesh_materials/%1.mesh_material")
                                                     .arg(QString::fromLatin1(name)));
        if (material.isEmpty())
            continue;
        QFile file(material);
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        auto texture = [&json](const char *key) {
            const QString name = json.value(QLatin1String(key)).toString();
            return name.isEmpty() ? QString() : Assets::modFile(QStringLiteral("textures/%1.dds").arg(name));
        };
        model.texturePath = texture("base_color_texture");
        model.maskPath = texture("mask_texture");
        model.ormPath = texture("occlusion_roughness_metallic_texture");
        model.normalPath = texture("normal_texture");
        return;
    }
}

} // namespace

ModMesh::Model ModMesh::load(const QString &path)
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
    const qsizetype tailOffset = indexOffset + 4 + qsizetype(indexCount) * 4;
    if (indexCount == 0 || indexCount % 3 != 0 || tailOffset > size)
        return {};

    // Vertices: position, normal and uv, in view axes
    std::vector<QVector3D> positions(vertexCount), normals(vertexCount), tangents(vertexCount);
    std::vector<QVector2D> uvs(vertexCount);
    std::vector<float> signs(vertexCount);
    QVector3D low(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max());
    QVector3D high = -low;
    for (quint32 i = 0; i < vertexCount; ++i) {
        const char *v = data + kVertexOffset + qsizetype(i) * kVertexSize;
        positions[i] = toView({readFloat(v), readFloat(v + 4), readFloat(v + 8)});
        normals[i] = toView({readFloat(v + 12), readFloat(v + 16), readFloat(v + 20)}).normalized();
        tangents[i] = toView({readFloat(v + 24), readFloat(v + 28), readFloat(v + 32)}).normalized();
        signs[i] = readFloat(v + 36) < 0.0f ? -1.0f : 1.0f;
        uvs[i] = QVector2D(readFloat(v + 40), readFloat(v + 44));
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
    Model model;
    model.vertices.reserve(size_t(indexCount) * kFloatsPerVertex);
    const char *indices = data + indexOffset + 4;
    for (quint32 i = 0; i < indexCount; ++i) {
        const quint32 index = qFromLittleEndian<quint32>(indices + qsizetype(i) * 4);
        if (index >= vertexCount)
            return {};
        const QVector3D p = (positions[index] - center) / length;
        const QVector3D &n = normals[index];
        const QVector2D &t = uvs[index];
        const QVector3D &g = tangents[index];
        model.vertices.insert(model.vertices.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z(), t.x(), t.y(),
                                                     g.x(), g.y(), g.z(), signs[index]});
    }
    findTextures(data, tailOffset, size, model);
    return model;
}
