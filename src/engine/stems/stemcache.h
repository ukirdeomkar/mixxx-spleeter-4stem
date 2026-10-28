#pragma once

#include <QString>
#include <memory>

#include "track/track.h"
#include "engine/stems/stemtrackbuffer.h"

namespace mixxx {

class StemCache {
  public:
    /// Writing raw float stems costs ~420 MB per five minute track (~25 GB for a
    /// small library), so v1 of the Spleeter engine does not write a cache.
    /// Loading stays implemented for a future compressed format.
    static constexpr bool isEnabled() {
        return false;
    }

    static QString getCacheDir();
    static QString getCachePath(TrackPointer pTrack);

    // Check if valid cached stems exist on disk for the given track
    static bool hasCachedStems(TrackPointer pTrack);

    // Load stems from disk into buffer. Returns true on success.
    static bool loadFromCache(TrackPointer pTrack, StemTrackBuffer* pBuffer);

    // Save stems from buffer to disk cache. Returns true on success.
    static bool saveToCache(TrackPointer pTrack, const StemTrackBuffer* pBuffer);
};

} // namespace mixxx
