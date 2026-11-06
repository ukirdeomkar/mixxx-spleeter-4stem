#include "engine/stems/stemcache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDebug>
#include <QSaveFile>

#include <cstring>

namespace mixxx {

namespace {
// v2: four stems (vocals, drums, bass, other). v1 caches are ignored.
constexpr char kMagic[8] = {'M', 'I', 'X', 'S', 'T', 'E', 'M', '2'};

constexpr qint64 kMinStemsFileSize =
        static_cast<qint64>(sizeof(kMagic) + sizeof(uint32_t));
} // namespace

QString StemCache::getCacheDir() {
    QString dir = QDir::homePath() + "/Library/Application Support/Mixxx/stems_cache";
    QDir().mkpath(dir);
    return dir;
}

QByteArray StemCache::stemsKeyForTrack(TrackPointer pTrack) {
    if (!pTrack) {
        return QByteArray();
    }
    const QString key = pTrack->getLocation() + ":" +
            QString::number(pTrack->getFileInfo().sizeInBytes());
    return QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1);
}

QString StemCache::getCachePath(TrackPointer pTrack) {
    const QByteArray hash = stemsKeyForTrack(pTrack);
    if (hash.isEmpty()) {
        return QString();
    }
    return getCacheDir() + "/" + hash.toHex() + ".stems";
}

bool StemCache::hasStemsFile(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo fi(path);
    return fi.exists() && fi.isFile() && fi.size() > kMinStemsFileSize;
}

bool StemCache::hasCachedStems(TrackPointer pTrack) {
    return hasStemsFile(getCachePath(pTrack));
}

bool StemCache::loadFromCache(TrackPointer pTrack, StemTrackBuffer* pBuffer) {
    return loadFromPath(getCachePath(pTrack), pBuffer);
}

bool StemCache::saveToCache(TrackPointer pTrack, const StemTrackBuffer* pBuffer) {
    return saveToPath(getCachePath(pTrack), pBuffer);
}

bool StemCache::loadFromPath(const QString& path, StemTrackBuffer* pBuffer) {
    if (path.isEmpty() || !pBuffer) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    char magic[8];
    if (file.read(magic, sizeof(magic)) != sizeof(magic) ||
        std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        qDebug() << "StemCache: Invalid magic header in" << path;
        return false;
    }

    uint32_t totalFrames = 0;
    if (file.read(reinterpret_cast<char*>(&totalFrames), sizeof(uint32_t)) != sizeof(uint32_t) ||
        totalFrames == 0) {
        return false;
    }

    pBuffer->allocate(totalFrames);

    const qint64 channelBytes = static_cast<qint64>(totalFrames) * sizeof(float);
    std::vector<float> vocL(totalFrames);
    std::vector<float> vocR(totalFrames);
    std::vector<float> drumL(totalFrames);
    std::vector<float> drumR(totalFrames);
    std::vector<float> bassL(totalFrames);
    std::vector<float> bassR(totalFrames);
    std::vector<float> otherL(totalFrames);
    std::vector<float> otherR(totalFrames);

    if (file.read(reinterpret_cast<char*>(vocL.data()), channelBytes) != channelBytes ||
        file.read(reinterpret_cast<char*>(vocR.data()), channelBytes) != channelBytes ||
        file.read(reinterpret_cast<char*>(drumL.data()), channelBytes) != channelBytes ||
        file.read(reinterpret_cast<char*>(drumR.data()), channelBytes) != channelBytes ||
        file.read(reinterpret_cast<char*>(bassL.data()), channelBytes) != channelBytes ||
        file.read(reinterpret_cast<char*>(bassR.data()), channelBytes) != channelBytes ||
        file.read(reinterpret_cast<char*>(otherL.data()), channelBytes) != channelBytes ||
        file.read(reinterpret_cast<char*>(otherR.data()), channelBytes) != channelBytes) {
        qDebug() << "StemCache: Incomplete stem data read from" << path;
        pBuffer->clear();
        return false;
    }

    pBuffer->writeChunk(0, totalFrames,
                        vocL.data(), vocR.data(),
                        drumL.data(), drumR.data(),
                        bassL.data(), bassR.data(),
                        otherL.data(), otherR.data());

    qDebug() << "StemCache: Successfully loaded stems from" << path
             << "(" << totalFrames << "frames)";
    return true;
}

bool StemCache::saveToPath(const QString& path, const StemTrackBuffer* pBuffer) {
    if (path.isEmpty() || !pBuffer || pBuffer->totalFrames() == 0) {
        return false;
    }

    const uint32_t totalFrames = pBuffer->totalFrames();
    if (pBuffer->framesReady() < totalFrames) {
        return false; // Incomplete separation, don't cache
    }

    QDir().mkpath(QFileInfo(path).absolutePath());

    // QSaveFile writes to a temporary file and renames it on commit(), so an
    // interrupted run can never leave a truncated .stems file behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "StemCache: Failed to create stem file" << path;
        return false;
    }

    if (file.write(kMagic, sizeof(kMagic)) != sizeof(kMagic) ||
        file.write(reinterpret_cast<const char*>(&totalFrames), sizeof(uint32_t)) != sizeof(uint32_t)) {
        file.cancelWriting();
        return false;
    }

    const qint64 channelBytes = static_cast<qint64>(totalFrames) * sizeof(float);
    std::vector<float> vocL(totalFrames);
    std::vector<float> vocR(totalFrames);
    std::vector<float> drumL(totalFrames);
    std::vector<float> drumR(totalFrames);
    std::vector<float> bassL(totalFrames);
    std::vector<float> bassR(totalFrames);
    std::vector<float> otherL(totalFrames);
    std::vector<float> otherR(totalFrames);

    pBuffer->readFrames(0, totalFrames,
                        vocL.data(), vocR.data(),
                        drumL.data(), drumR.data(),
                        bassL.data(), bassR.data(),
                        otherL.data(), otherR.data());

    if (file.write(reinterpret_cast<const char*>(vocL.data()), channelBytes) != channelBytes ||
        file.write(reinterpret_cast<const char*>(vocR.data()), channelBytes) != channelBytes ||
        file.write(reinterpret_cast<const char*>(drumL.data()), channelBytes) != channelBytes ||
        file.write(reinterpret_cast<const char*>(drumR.data()), channelBytes) != channelBytes ||
        file.write(reinterpret_cast<const char*>(bassL.data()), channelBytes) != channelBytes ||
        file.write(reinterpret_cast<const char*>(bassR.data()), channelBytes) != channelBytes ||
        file.write(reinterpret_cast<const char*>(otherL.data()), channelBytes) != channelBytes ||
        file.write(reinterpret_cast<const char*>(otherR.data()), channelBytes) != channelBytes) {
        qDebug() << "StemCache: Write error saving stems to" << path;
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        qDebug() << "StemCache: Failed to commit stem file" << path;
        return false;
    }

    qDebug() << "StemCache: Successfully saved stems to" << path;
    return true;
}

} // namespace mixxx
