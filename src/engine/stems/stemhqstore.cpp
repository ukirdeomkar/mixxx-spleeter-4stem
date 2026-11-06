#include "engine/stems/stemhqstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QDebug>

#include "engine/stems/stemcache.h"
#include "preferences/configobject.h"
#include "util/cmdlineargs.h"

namespace mixxx {

namespace {

constexpr char kIndexFileName[] = "hq_stems_index.json";
constexpr char kTracksKey[] = "tracks";

// The offline job thread writes the index while the GUI thread reads it on
// track load.
QMutex s_indexMutex;

QString sanitizeFileName(const QString& name) {
    QString result;
    result.reserve(name.size());
    const QString illegal = QStringLiteral("\\/:*?\"<>|");
    for (const QChar ch : name) {
        if (ch.unicode() < 0x20 || illegal.contains(ch)) {
            result.append(QLatin1Char('_'));
        } else {
            result.append(ch);
        }
    }
    result = result.trimmed();
    if (result.isEmpty()) {
        result = QStringLiteral("track");
    }
    if (result.size() > 120) {
        result.truncate(120);
    }
    return result;
}

QString indexFilePath() {
    QString settingsPath = CmdlineArgs::instance().getSettingsPath();
    if (settingsPath.isEmpty()) {
        settingsPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }
    if (settingsPath.isEmpty()) {
        return QString();
    }
    return QDir(settingsPath).filePath(QLatin1String(kIndexFileName));
}

QJsonObject readIndexUnlocked() {
    const QString path = indexFilePath();
    if (path.isEmpty()) {
        return QJsonObject();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qDebug() << "StemHqStore: ignoring unreadable index" << path << error.errorString();
        return QJsonObject();
    }
    return document.object().value(QString::fromLatin1(kTracksKey)).toObject();
}

bool writeIndexUnlocked(const QJsonObject& tracks) {
    const QString path = indexFilePath();
    if (path.isEmpty()) {
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject root;
    root.insert(QString::fromLatin1(kTracksKey), tracks);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "StemHqStore: cannot write index" << path;
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

void registerTrack(const TrackPointer& pTrack, const QString& stemsPath) {
    QMutexLocker locker(&s_indexMutex);
    QJsonObject tracks = readIndexUnlocked();
    // Entries are not pruned here: a stem file may simply live on a USB stick
    // that is unplugged right now. findStemsForTrack() checks existence.
    tracks.insert(pTrack->getLocation(), stemsPath);
    if (!writeIndexUnlocked(tracks)) {
        qDebug() << "StemHqStore: failed to register" << stemsPath;
    }
}

} // namespace

QString StemHqStore::fileNameForTrack(const TrackPointer& pTrack) {
    const QByteArray hash = StemCache::stemsKeyForTrack(pTrack);
    if (hash.isEmpty()) {
        return QString();
    }
    // Short hash: 8 hex chars (32 bits) is plenty to separate two files with
    // the same base name, and keeps the name readable for a human.
    const QString suffix = QString::fromLatin1(hash.toHex().left(8));
    const QString base = sanitizeFileName(
            QFileInfo(pTrack->getLocation()).completeBaseName());
    return QStringLiteral("%1-%2.stems").arg(base, suffix);
}

QString StemHqStore::getLastOutputDir(UserSettingsPointer pConfig) {
    const ConfigKey key(QStringLiteral("[Stems]"), QStringLiteral("high_quality_dir"));
    QString dir = pConfig ? pConfig->getValue<QString>(key, QString()) : QString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    }
    return dir;
}

void StemHqStore::setLastOutputDir(UserSettingsPointer pConfig, const QString& dir) {
    if (!pConfig || dir.isEmpty()) {
        return;
    }
    pConfig->setValue(
            ConfigKey(QStringLiteral("[Stems]"), QStringLiteral("high_quality_dir")),
            dir);
}

QString StemHqStore::findStemsForTrack(const TrackPointer& pTrack,
        const QString& fallbackDir) {
    if (!pTrack) {
        return QString();
    }

    const QString location = pTrack->getLocation();
    {
        QMutexLocker locker(&s_indexMutex);
        const QJsonObject tracks = readIndexUnlocked();
        const QString path = tracks.value(location).toString();
        if (StemCache::hasStemsFile(path)) {
            return path;
        }
    }

    if (!fallbackDir.isEmpty()) {
        const QString candidate =
                QDir(fallbackDir).absoluteFilePath(fileNameForTrack(pTrack));
        if (StemCache::hasStemsFile(candidate)) {
            return candidate;
        }
    }

    return QString();
}

bool StemHqStore::hasRoomForFrames(const QString& dir, uint32_t totalFrames) {
    if (dir.isEmpty() || totalFrames == 0) {
        return false;
    }
    const QStorageInfo storage(dir);
    if (!storage.isValid() || !storage.isReady()) {
        // Unknown (network share, some removable media): let the write fail
        // later rather than blocking a legitimate destination.
        return true;
    }
    const qint64 needed = stemFileSizeForFrames(totalFrames);
    // Leave a little headroom so a full drive does not end up at zero bytes.
    return storage.bytesAvailable() > needed + (16LL * 1024 * 1024);
}

QString StemHqStore::saveStems(const TrackPointer& pTrack,
        const StemTrackBuffer* pBuffer,
        const QString& outputDir,
        QString* pError) {
    const auto fail = [pError](const QString& message) {
        qDebug() << "StemHqStore:" << message;
        if (pError) {
            *pError = message;
        }
        return QString();
    };

    if (!pTrack || !pBuffer || pBuffer->totalFrames() == 0) {
        return fail(QObject::tr("Nothing to save."));
    }
    if (outputDir.isEmpty()) {
        return fail(QObject::tr("No output folder selected."));
    }
    if (!QDir().mkpath(outputDir)) {
        return fail(QObject::tr("Cannot create folder %1.").arg(outputDir));
    }
    if (!hasRoomForFrames(outputDir, pBuffer->totalFrames())) {
        return fail(QObject::tr("Not enough free space in %1 (need about %2 MB).")
                            .arg(outputDir)
                            .arg(stemFileSizeForFrames(pBuffer->totalFrames()) /
                                    (1024 * 1024)));
    }

    const QString path =
            QDir(outputDir).absoluteFilePath(fileNameForTrack(pTrack));
    if (!StemCache::saveToPath(path, pBuffer)) {
        return fail(QObject::tr("Writing %1 failed.").arg(path));
    }

    registerTrack(pTrack, path);
    return path;
}

} // namespace mixxx
