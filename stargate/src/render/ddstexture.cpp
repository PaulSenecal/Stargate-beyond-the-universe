#include "ddstexture.h"

#include <QFile>
#include <QSettings>
#include <QtEndian>

#include <algorithm>
#include <atomic>
#include <cstring>

// bcdec is a third-party single-file library (MIT / Unlicense):
// compiled here only, with its own warnings silenced
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#define BCDEC_IMPLEMENTATION
#include "src/thirdparty/bcdec.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

constexpr quint32 kDxgiBc7Unorm = 98;
constexpr quint32 kDxgiBc7Srgb = 99;
constexpr qsizetype kHeaderSize = 4 + 124;     // magic + DDS_HEADER
constexpr qsizetype kDx10Size = 20;            // DDS_HEADER_DXT10

qsizetype levelBytes(int width, int height)
{
    return qsizetype(std::max(1, (width + 3) / 4)) * std::max(1, (height + 3) / 4) * BCDEC_BC7_BLOCK_SIZE;
}

} // namespace

QImage DdsTexture::load(const QString &path, int maxSize)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    const char *data = bytes.constData();
    if (bytes.size() < kHeaderSize + kDx10Size || std::memcmp(data, "DDS ", 4) != 0
        || std::memcmp(data + 84, "DX10", 4) != 0)
        return {};
    const quint32 dxgi = qFromLittleEndian<quint32>(data + kHeaderSize);
    if (dxgi != kDxgiBc7Unorm && dxgi != kDxgiBc7Srgb)
        return {};
    int width = int(qFromLittleEndian<quint32>(data + 16));
    int height = int(qFromLittleEndian<quint32>(data + 12));
    const int levels = std::max(1, int(qFromLittleEndian<quint32>(data + 28)));
    if (width < 4 || height < 4 || width > 16384 || height > 16384)
        return {};

    // Skip the mip levels bigger than wanted (they come first)
    qsizetype offset = kHeaderSize + kDx10Size;
    for (int level = 0; level < levels - 1 && std::max(width, height) > maxSize && std::min(width, height) > 4; ++level) {
        offset += levelBytes(width, height);
        width = std::max(4, width / 2);
        height = std::max(4, height / 2);
    }
    if (offset + levelBytes(width, height) > bytes.size())
        return {};

    QImage image(width, height, QImage::Format_RGBA8888);
    if (image.isNull())
        return {};
    const int pitch = int(image.bytesPerLine());
    const char *block = data + offset;
    for (int y = 0; y < height; y += 4)
        for (int x = 0; x < width; x += 4) {
            bcdec_bc7(block, image.scanLine(y) + x * 4, pitch);
            block += BCDEC_BC7_BLOCK_SIZE;
        }
    return image;
}

namespace {
std::atomic<int> g_quality{-1};             // read once from the settings
} // namespace

int DdsTexture::qualitySetting()
{
    if (g_quality < 0)
        g_quality = std::clamp(QSettings("Stargate", "stargate").value("display/textureQuality", 1).toInt(), 0, 2);
    return g_quality;
}

void DdsTexture::setQualitySetting(int quality)
{
    g_quality = std::clamp(quality, 0, 2);
    QSettings("Stargate", "stargate").setValue("display/textureQuality", g_quality.load());
}

int DdsTexture::maxSizeFor(int quality)
{
    return quality <= 0 ? 1024 : quality == 1 ? 2048 : 4096;
}
