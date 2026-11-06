#include "engine/stems/stemmodelpaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace mixxx {

QString findStemModelFile(const QString& fileName) {
#if defined(__ANDROID__) || defined(ANDROID)
    // On Android the model may ship inside the APK; unpack it once.
    const QString assetPath = "assets:/models/" + fileName;
    if (QFile::exists(assetPath)) {
        const QString destDir =
                QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
        QDir().mkpath(destDir);
        const QString destPath = destDir + "/" + fileName;
        if (!QFileInfo::exists(destPath)) {
            QFile::copy(assetPath, destPath);
        }
        if (QFileInfo::exists(destPath)) {
            return destPath;
        }
    }
#endif

    const QStringList searchDirs = {
            QCoreApplication::applicationDirPath() + "/models",
            QCoreApplication::applicationDirPath() + "/../Resources/models",
            QDir::currentPath() + "/models",
            QDir::currentPath(),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models",
            QDir::homePath() + "/.mixxx/models",
            QDir::homePath() + "/Library/Application Support/Mixxx/models",
            "/usr/share/mixxx/models",
            "/usr/local/share/mixxx/models",
    };

    for (const auto& dir : searchDirs) {
        const QString candidate = dir + "/" + fileName;
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

} // namespace mixxx
