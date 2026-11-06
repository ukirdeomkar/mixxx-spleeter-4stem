#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

#include "track/track.h"

class QProgressDialog;
class QThread;

namespace mixxx {

/// Runs on the separation thread: decodes each track, runs HT-Demucs and saves
/// the stems to the chosen folder. One track at a time (peak RSS is measured in
/// gigabytes; see docs/todo-hq-demucs.md).
class StemHqWorker : public QObject {
    Q_OBJECT
  public:
    explicit StemHqWorker(QObject* parent = nullptr);

    /// Thread-safe: the GUI thread flips this to stop after the current
    /// segment.
    void requestCancel() {
        m_cancelRequested.store(true, std::memory_order_relaxed);
    }

    void process(const QList<TrackPointer>& tracks, const QString& outputDir);

  signals:
    void trackStarted(int index, int count, const QString& title);
    void trackProgress(double progress);
    void trackFinished(const QString& trackLocation, bool ok, const QString& message);
    void failed(const QString& message);
    void allFinished();

  private:
    std::atomic<bool> m_cancelRequested{false};
};

/// GUI-thread coordinator for "Add high quality stems".
///
/// Owns the worker thread, the progress dialog and the notification that lets
/// a deck switch to freshly written stems without reloading the track.
class StemHqService : public QObject {
    Q_OBJECT
  public:
    static StemHqService& instance();

    bool isBusy() const {
        return m_bBusy;
    }

    /// Start separating `tracks` into `outputDir`. Ignored while busy.
    void enqueue(const QList<TrackPointer>& tracks, const QString& outputDir);

  signals:
    /// The stems for `trackLocation` were written and registered; a deck that
    /// has this track loaded should switch to them.
    void hqStemsReady(const QString& trackLocation);

  private slots:
    void slotTrackStarted(int index, int count, const QString& title);
    void slotTrackProgress(double progress);
    void slotTrackFinished(const QString& trackLocation, bool ok, const QString& message);
    void slotFailed(const QString& message);
    void slotAllFinished();
    void slotCancel();

  private:
    StemHqService();
    ~StemHqService() override;

    void closeProgressDialog();
    void showSummary();

    QThread* m_pThread;
    StemHqWorker* m_pWorker;
    QProgressDialog* m_pProgressDialog{nullptr};
    int m_trackCount{0};
    int m_currentIndex{0};
    int m_succeeded{0};
    QStringList m_failures;
    bool m_bBusy{false};
};

} // namespace mixxx
