#pragma once

#include <QString>
#include <QThread>
#include <QAtomicInt>
#include <QMutex>
#include <QWaitCondition>
#include <memory>

#include "track/track.h"
#include "control/controlobject.h"
#include "engine/stems/stemtrackbuffer.h"

namespace mixxx {

class StemWorkerThread : public QThread {
    Q_OBJECT
  public:
    StemWorkerThread(
            std::shared_ptr<StemTrackBuffer> pBuffer,
            ControlObject* pProgressControl,
            ControlObject* pStatusControl);
    ~StemWorkerThread() override;

    // Request analyzing a track in the background. When `hqStemsPath` points at
    // a high quality (HT-Demucs) stem file, it is loaded instead of running the
    // Spleeter engine.
    void loadTrack(TrackPointer pTrack, const QString& hqStemsPath = QString());

    // Stop current analysis
    void stop();

  protected:
    void run() override;

  private:
    std::shared_ptr<StemTrackBuffer> m_pBuffer;
    ControlObject* m_pProgressControl;
    ControlObject* m_pStatusControl;

    QMutex m_mutex;
    QWaitCondition m_condition;
    TrackPointer m_pNextTrack;
    TrackPointer m_pCurrentTrack;
    QString m_nextHqStemsPath;
    QString m_currentHqStemsPath;
    QAtomicInt m_stopRequested{0};
    QAtomicInt m_abortCurrentTrack{0};
    QAtomicInt m_hasWork{0};
};

} // namespace mixxx
