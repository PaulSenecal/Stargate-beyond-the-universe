#include "meshbuilder.h"

#include <QtMath>
#include <utility>

namespace MeshBuilder {

namespace {

void pushVertex(VertexData &d, const QVector3D &p, const QVector3D &n)
{
    d.insert(d.end(), {p.x(), p.y(), p.z(), n.x(), n.y(), n.z()});
}

// Flat-shaded triangle; the winding is swapped if needed so the face
// normal points toward `outward`.
void flatTri(VertexData &d, QVector3D a, QVector3D b, QVector3D c, const QVector3D &outward)
{
    QVector3D n = QVector3D::crossProduct(b - a, c - a).normalized();
    if (QVector3D::dotProduct(n, outward) < 0.0f) {
        std::swap(b, c);
        n = -n;
    }
    pushVertex(d, a, n);
    pushVertex(d, b, n);
    pushVertex(d, c, n);
}

void flatQuad(VertexData &d, const QVector3D &a, const QVector3D &b,
              const QVector3D &c, const QVector3D &e, const QVector3D &outward)
{
    flatTri(d, a, b, c, outward);
    flatTri(d, a, c, e, outward);
}

} // namespace

VertexData box(const QVector3D &size)
{
    const float x = size.x() * 0.5f, y = size.y() * 0.5f, z = size.z() * 0.5f;
    const QVector3D p[8] = {
        {-x, -y, -z}, {x, -y, -z}, {x, y, -z}, {-x, y, -z},
        {-x, -y,  z}, {x, -y,  z}, {x, y,  z}, {-x, y,  z},
    };

    VertexData d;
    flatQuad(d, p[4], p[5], p[6], p[7], { 0,  0,  1});
    flatQuad(d, p[0], p[1], p[2], p[3], { 0,  0, -1});
    flatQuad(d, p[1], p[5], p[6], p[2], { 1,  0,  0});
    flatQuad(d, p[0], p[4], p[7], p[3], {-1,  0,  0});
    flatQuad(d, p[3], p[2], p[6], p[7], { 0,  1,  0});
    flatQuad(d, p[0], p[1], p[5], p[4], { 0, -1,  0});
    return d;
}

VertexData prism(const std::vector<QVector2D> &outline, float thickness)
{
    VertexData d;
    const int n = int(outline.size());
    if (n < 3)
        return d;

    QVector2D centroid;
    float signedArea = 0.0f;
    for (int i = 0; i < n; ++i) {
        const QVector2D &a = outline[i];
        const QVector2D &b = outline[(i + 1) % n];
        centroid += a;
        signedArea += a.x() * b.y() - b.x() * a.y();
    }
    centroid /= float(n);
    const float winding = signedArea >= 0.0f ? 1.0f : -1.0f;

    const float top = thickness * 0.5f;
    const float bottom = -top;
    const QVector3D cTop(centroid.x(), top, centroid.y());
    const QVector3D cBottom(centroid.x(), bottom, centroid.y());

    for (int i = 0; i < n; ++i) {
        const QVector2D &a = outline[i];
        const QVector2D &b = outline[(i + 1) % n];
        const QVector3D aTop(a.x(), top, a.y()), bTop(b.x(), top, b.y());
        const QVector3D aBottom(a.x(), bottom, a.y()), bBottom(b.x(), bottom, b.y());

        flatTri(d, cTop, aTop, bTop, {0, 1, 0});
        flatTri(d, cBottom, aBottom, bBottom, {0, -1, 0});

        // Outward edge normal in the XZ plane depends on the winding
        const QVector2D e = b - a;
        const QVector3D outward(e.y() * winding, 0.0f, -e.x() * winding);
        flatQuad(d, aBottom, bBottom, bTop, aTop, outward);
    }
    return d;
}

VertexData frustum(int segments, float bottomRadius, float topRadius, float height)
{
    VertexData d;
    const QVector3D baseCenter(0, 0, 0);
    const QVector3D topCenter(0, height, 0);

    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * float(M_PI) * float(i) / float(segments);
        const float a1 = 2.0f * float(M_PI) * float(i + 1) / float(segments);
        const float am = (a0 + a1) * 0.5f;

        const QVector3D b0(bottomRadius * qCos(a0), 0, bottomRadius * qSin(a0));
        const QVector3D b1(bottomRadius * qCos(a1), 0, bottomRadius * qSin(a1));
        const QVector3D t0(topRadius * qCos(a0), height, topRadius * qSin(a0));
        const QVector3D t1(topRadius * qCos(a1), height, topRadius * qSin(a1));
        const QVector3D outward(qCos(am), 0.0f, qSin(am));

        if (topRadius > 1e-4f)
            flatQuad(d, b0, b1, t1, t0, outward);
        else
            flatTri(d, b0, b1, topCenter, outward + QVector3D(0, 0.2f, 0));

        flatTri(d, baseCenter, b0, b1, {0, -1, 0});
        if (topRadius > 1e-4f)
            flatTri(d, topCenter, t0, t1, {0, 1, 0});
    }
    return d;
}

VertexData sphere(int rings, int sectors)
{
    VertexData d;
    auto point = [&](int r, int s) {
        const float theta = float(M_PI) * float(r) / float(rings);       // 0..pi
        const float phi = 2.0f * float(M_PI) * float(s) / float(sectors); // 0..2pi
        return QVector3D(qSin(theta) * qCos(phi), qCos(theta), qSin(theta) * qSin(phi));
    };

    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            const QVector3D p00 = point(r, s), p01 = point(r, s + 1);
            const QVector3D p10 = point(r + 1, s), p11 = point(r + 1, s + 1);
            pushVertex(d, p00, p00); pushVertex(d, p10, p10); pushVertex(d, p11, p11);
            pushVertex(d, p00, p00); pushVertex(d, p11, p11); pushVertex(d, p01, p01);
        }
    }
    return d;
}

std::vector<QVector2D> sector(const QVector2D &center, float radius,
                              float startDeg, float endDeg, int steps)
{
    std::vector<QVector2D> outline{center};
    for (int i = 0; i <= steps; ++i) {
        const float a = qDegreesToRadians(startDeg + (endDeg - startDeg) * float(i) / float(steps));
        outline.emplace_back(center.x() + radius * qCos(a), center.y() + radius * qSin(a));
    }
    return outline;
}

} // namespace MeshBuilder
