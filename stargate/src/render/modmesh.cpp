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

// Attach points at the start of the trailing block: [u32 length][name]
// [position 3f][rotation 9f][2 bytes], until the material list
std::vector<std::pair<QString, QVector3D>> readPoints(const char *data, qsizetype from, qsizetype size)
{
    std::vector<std::pair<QString, QVector3D>> points;
    for (qsizetype i = from; i + 4 + 50 < size && i < from + 200000;) {
        const quint32 length = qFromLittleEndian<quint32>(data + i);
        if (length < 2 || length > 80 || i + 4 + qsizetype(length) + 50 > size) {
            ++i;
            continue;
        }
        const QByteArray name(data + i + 4, qsizetype(length));
        if (!std::all_of(name.cbegin(), name.cend(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
            })) {
            ++i;
            continue;
        }
        if (!name.contains('.') && name != "above" && name != "aura" && name != "center" && name != "bomb")
            break;                              // the material list starts
        const char *p = data + i + 4 + length;
        points.emplace_back(QString::fromLatin1(name), toView({readFloat(p), readFloat(p + 4), readFloat(p + 8)}));
        i += 4 + length + 50;
    }
    return points;
}

// Glowing spots at the rear of a hull without exhaust points: the
// vertices of the last few percent of its length, in bins across it
std::vector<QVector3D> rearSpots(const std::vector<QVector3D> &positions, const QVector3D &low, const QVector3D &high)
{
    constexpr int kBins = 5;
    const float limit = low.x() + (high.x() - low.x()) * 0.03f;
    QVector3D sum[kBins];
    int count[kBins] = {};
    const float width = std::max(1e-3f, high.z() - low.z());
    for (const QVector3D &p : positions)
        if (p.x() <= limit) {
            const int bin = std::clamp(int((p.z() - low.z()) / width * kBins), 0, kBins - 1);
            sum[bin] += p;
            ++count[bin];
        }
    std::vector<QVector3D> spots;
    for (int b = 0; b < kBins; ++b)
        if (count[b] > 0)
            spots.push_back(sum[b] / float(count[b]));
    return spots;
}

} // namespace

ModMesh::Model ModMesh::load(const QString &path, const Model *frame)
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
    if (vertexCount == 0 || vertexCount > kMaxVertices || indexOffset + 8 > size)
        return {};
    // Index count, a u32 (always 0), then the indices
    const quint32 indexCount = qFromLittleEndian<quint32>(data + indexOffset);
    const qsizetype tailOffset = indexOffset + 8 + qsizetype(indexCount) * 4;
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
    const float length = frame && frame->length > 0.0f ? frame->length : high.x() - low.x();
    if (!(length > 0.0f))
        return {};
    const QVector3D center = frame && frame->length > 0.0f ? frame->center : (low + high) / 2.0f;

    // Triangles unrolled (the battle view draws non-indexed vertices)
    Model model;
    model.center = center;
    model.length = length;
    model.halfExtents = (high - low) / (2.0f * length);
    model.vertices.reserve(size_t(indexCount) * kFloatsPerVertex);
    const char *indices = data + indexOffset + 8;
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

    // Silhouette from above: cells touched by the vertices, the edge
    // middles and the centers of the triangles
    constexpr int kWidth = 160;
    const float halfZ = std::max(model.halfExtents.z(), 0.02f);
    const int height = std::clamp(int(kWidth * 2.0f * halfZ), 8, 160);
    model.silhouetteRect = QRectF(-0.5, -halfZ, 1.0, 2.0 * halfZ);
    model.silhouette = QImage(kWidth, height, QImage::Format_Alpha8);
    model.silhouette.fill(0);
    auto mark = [&](float x, float z) {
        const int cx = std::clamp(int((x + 0.5f) * kWidth), 0, kWidth - 1);
        const int cz = std::clamp(int((z + halfZ) / (2.0f * halfZ) * height), 0, height - 1);
        model.silhouette.scanLine(cz)[cx] = 255;
    };
    for (size_t v = 0; v + 2 * kFloatsPerVertex < model.vertices.size(); v += 3 * kFloatsPerVertex) {
        const float *a = &model.vertices[v], *b = a + kFloatsPerVertex, *c = b + kFloatsPerVertex;
        for (float u = 0.0f; u <= 1.0f; u += 0.25f)
            for (float w = 0.0f; u + w <= 1.0f; w += 0.25f)
                mark(a[0] + (b[0] - a[0]) * u + (c[0] - a[0]) * w, a[2] + (b[2] - a[2]) * u + (c[2] - a[2]) * w);
    }

    for (const auto &[name, pos] : readPoints(data, tailOffset, size)) {
        const QVector3D p = (pos - center) / length;
        model.points.push_back({name, p});
        if (name.startsWith(QLatin1String("exhaust")))
            model.engines.push_back(p);
    }
    if (model.engines.empty())
        for (const QVector3D &spot : rearSpots(positions, low, high))
            model.engines.push_back((spot - center) / length);
    return model;
}
