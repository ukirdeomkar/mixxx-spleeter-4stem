#pragma once

#include <QString>

#include <cstdint>

#include "engine/stems/stemtrackbuffer.h"
#include "preferences/usersettings.h"
#include "track/track.h"

namespace mixxx {

/// Storage for the offline "high quality" (HT-Demucs) stems.
///
/// Unlike the fast engine's cache, these files live in a folder the user picked
/// (an external drive, a USB stick, ...) because they are big: four stereo
/// float stems are ~420 MB for a five minute track. The folder is not derivable
/// from the library, so a small JSON index in the settings dir remembers which
/// file belongs to which track. Once a track has an entry, a deck loads those
/// stems instead of running the Spleeter engine.
///
/// Files use the same .stems format as StemCache.
class StemHqStore {
  public:
    /// Four stems, stereo, float32 -> 32 bytes per frame.
    static constexpr qint64 kBytesPerFrame = 4 * 2 * sizeof(float);

    /// Deterministic file name inside an output folder, e.g.
    /// "Some Track-1a2b3c4d.stems". Derived from the track location and file
    /// size, so a re-imported track maps to the same name.
    static QString fileNameForTrack(const TrackPointer& pTrack);

    /// Folder used as the default in the folder picker: the last folder the
    /// user chose (remembered in the config), else the system music folder.
    static QString getLastOutputDir(UserSettingsPointer pConfig);
    static void setLastOutputDir(UserSettingsPointer pConfig, const QString& dir);

    /// Path of existing high quality stems for this track, or an empty string.
    /// Checks the index first, then `fallbackDir` (usually the last folder the
    /// user picked) with the deterministic file name.
    static QString findStemsForTrack(const TrackPointer& pTrack,
            const QString& fallbackDir = QString());

    /// Approximate size on disk of the stems for `totalFrames` frames.
    static qint64 stemFileSizeForFrames(uint32_t totalFrames) {
        return kBytesPerFrame * static_cast<qint64>(totalFrames);
    }

    /// Cheap pre-flight: does `dir` have room for the stems of `totalFrames`?
    static bool hasRoomForFrames(const QString& dir, uint32_t totalFrames);

    /// Write the stems into `dir` and register them in the index.
    /// Returns the written path, or an empty string with *pError set.
    static QString saveStems(const TrackPointer& pTrack,
            const StemTrackBuffer* pBuffer,
            const QString& outputDir,
            QString* pError = nullptr);
};

} // namespace mixxx
