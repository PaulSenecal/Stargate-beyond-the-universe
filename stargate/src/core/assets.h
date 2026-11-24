#ifndef ASSETS_H
#define ASSETS_H

#include <QDir>
#include <QFileInfo>
#include <QString>

// ================================================================
//  Assets
//  Optional local content of the Stargate Reborn mod (pictures,
//  sounds…), kept out of the repository (stargate/src/autre is
//  git-ignored). The game must work without it: every lookup
//  returns an empty path when the file is missing.
//  Folder: $STARGATE_ASSETS, else the one set at build time.
// ================================================================

namespace Assets {

inline QString modDir()
{
    const QString env = qEnvironmentVariable("STARGATE_ASSETS");
    if (!env.isEmpty())
        return env;
#ifdef STARGATE_ASSETS_DIR
    return QStringLiteral(STARGATE_ASSETS_DIR);
#else
    return {};
#endif
}

// Absolute path of a file of the mod ("textures/x.png"), empty if missing
inline QString modFile(const QString &relative)
{
    const QString dir = modDir();
    if (dir.isEmpty() || relative.isEmpty())
        return {};
    const QString path = QDir(dir).filePath(relative);
    return QFileInfo::exists(path) ? path : QString();
}

// Large picture of a ship ("goauld_cruiser_hatak"), empty if missing
inline QString shipPicture(const QString &key)
{
    return key.isEmpty() ? QString() : modFile(QStringLiteral("textures/%1_tooltip_picture.png").arg(key));
}

} // namespace Assets

#endif // ASSETS_H
