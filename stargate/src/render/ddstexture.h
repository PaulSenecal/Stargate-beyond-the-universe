#ifndef DDSTEXTURE_H
#define DDSTEXTURE_H

#include <QImage>
#include <QString>

// ================================================================
//  DdsTexture
//  Reads a color texture of the Stargate Reborn mod (.dds, DX10
//  header, BC7) and decodes it on the CPU with bcdec (macOS OpenGL
//  4.1 cannot use BC7). The file holds every mip level: the largest
//  one not bigger than maxSize is decoded (1024, 2048 or 4096, see
//  the texture quality setting). Returns a null image when the file
//  is missing or not in a supported format.
// ================================================================

namespace DdsTexture {

QImage load(const QString &path, int maxSize);

// Texture quality setting (display/textureQuality): 0 normal (1024),
// 1 high (2048, default), 2 ultra (4096)
int qualitySetting();
void setQualitySetting(int quality);       // saved; models reload their textures
int maxSizeFor(int quality);

} // namespace DdsTexture

#endif // DDSTEXTURE_H
