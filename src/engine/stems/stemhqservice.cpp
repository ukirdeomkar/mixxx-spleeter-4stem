#include "engine/stems/stemhqservice.h"

#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressDialog>
#include <QThread>
#include <QWidget>

#include <algorithm>
#include <memory>
#include <vector>

#include "engine/stems/htdemucsengine.h"
#include "engine/stems/stemdecode.h"
#include "engine/stems/stemhqstore.h"
#include "engine/stems/stemtrackbuffer.h"

namespace mixxx {

StemHqWorker::StemHqWorker(QObject* parent)
        : QObject(parent) {
}

void StemHqWorker::process(const QList<TrackPointer>& tracks, const QString& outputDir) {
    m_cancelRequested.store(false, std::memory_order_relaxed);

    if (!HTDemucsEngine::instance().isAvailable()) {
        emit failed(tr("The HT-Demucs model (htdemucs.onnx) is not installed."));
        emit allFinished();
        return;
    }
    // Model load takes a few seconds and a few hundred MB; do it once, before
    // the first track so the progress dialog is already on screen.
    if (!HTDemucsEngine::instance().initialize()) {
        emit failed(tr("The HT-Demucs model could not be loaded. It may be corrupt."));
        emit allFinished();
        return;
    }

    int index = 0;
    for (const TrackPointer& pTrack : tracks) {
        const int currentIndex = index;
        ++index;

        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            break;
        }
        if (!pTrack) {
            continue;
        }

        emit trackStarted(currentIndex, static_cast<int>(tracks.size()), pTrack->getTitle());
        emit trackProgress(0.0);

        {
            std::vector<float> left;
            std::vector<float> right;
            if (!decodeTrackToStereoFloats(pTrack, &left, &right, &m_cancelRequested)) {
                emit trackFinished(pTrack->getLocation(),
                        false,
                        tr("Could not decode this track."));
                continue;
            }
            const auto totalFrames = static_cast<uint32_t>(left.size());

            if (!StemHqStore::hasRoomForFrames(outputDir, totalFrames)) {
                emit trackFinished(pTrack->getLocation(),
                        false,
                        tr("Not enough free space in %1 (the stems need about %2 MB).")
                                .arg(outputDir)
                                .arg(StemHqStore::stemFileSizeForFrames(totalFrames) /
                                        (1024 * 1024)));
                continue;
            }

            StemTrackBuffer buffer;
            buffer.allocate(totalFrames);

            const bool separated = HTDemucsEngine::instance().separateTrack(totalFrames,
                    left.data(),
                    right.data(),
                    [&buffer](uint32_t startFrame,
                            uint32_t numFrames,
                            const float* voc_l,
                            const float* voc_r,
                            const float* drum_l,
                            const float* drum_r,
                            const float* bass_l,
                            const float* bass_r,
                            const float* other_l,
                            const float* other_r) {
                        buffer.writeChunk(startFrame,
                                numFrames,
                                voc_l, voc_r,
                                drum_l, drum_r,
                                bass_l, bass_r,
                                other_l, other_r);
                    },
                    [this](double progress) {
                        emit trackProgress(progress);
                    },
                    &m_cancelRequested);

            // Release the decoded audio before saving; the buffer alone is
            // several hundred MB for a long track.
            left.clear();
            left.shrink_to_fit();
            right.clear();
            right.shrink_to_fit();

            if (!separated) {
                emit trackFinished(pTrack->getLocation(),
                        false,
                        m_cancelRequested.load(std::memory_order_relaxed)
                                ? tr("Cancelled.")
                                : tr("Separation failed."));
                if (m_cancelRequested.load(std::memory_order_relaxed)) {
                    break;
                }
                continue;
            }

            QString error;
            const QString path = StemHqStore::saveStems(pTrack, &buffer, outputDir, &error);
            if (path.isEmpty()) {
                emit trackFinished(pTrack->getLocation(), false, error);
            } else {
                emit trackFinished(pTrack->getLocation(), true, path);
            }
        }
    }

    emit allFinished();
}

StemHqService& StemHqService::instance() {
    static StemHqService* s_instance = new StemHqService();
    return *s_instance;
}

StemHqService::StemHqService()
        : m_pThread(new QThread()),
          m_pWorker(new StemHqWorker()) {
    m_pWorker->moveToThread(m_pThread);
    connect(m_pThread, &QThread::finished, m_pWorker, &QObject::deleteLater);
    connect(m_pWorker, &StemHqWorker::trackStarted, this, &StemHqService::slotTrackStarted);
    connect(m_pWorker, &StemHqWorker::trackProgress, this, &StemHqService::slotTrackProgress);
    connect(m_pWorker, &StemHqWorker::trackFinished, this, &StemHqService::slotTrackFinished);
    connect(m_pWorker, &StemHqWorker::failed, this, &StemHqService::slotFailed);
    connect(m_pWorker, &StemHqWorker::allFinished, this, &StemHqService::slotAllFinished);
    m_pThread->setObjectName(QStringLiteral("StemHqWorker"));
    m_pThread->start();
}

StemHqService::~StemHqService() {
    m_pWorker->requestCancel();
    m_pThread->quit();
    m_pThread->wait();
    delete m_pThread;
}

void StemHqService::enqueue(const QList<TrackPointer>& tracks, const QString& outputDir) {
    if (m_bBusy || tracks.isEmpty() || outputDir.isEmpty()) {
        return;
    }

    m_bBusy = true;
    m_trackCount = static_cast<int>(tracks.size());
    m_currentIndex = 0;
    m_succeeded = 0;
    m_failures.clear();

    QWidget* pParent = QApplication::activeWindow();
    m_pProgressDialog = new QProgressDialog(
            tr("Preparing high quality stem separation..."),
            tr("Cancel"),
            0,
            100,
            pParent);
    m_pProgressDialog->setWindowTitle(tr("Add high quality stems (HT-Demucs)"));
    m_pProgressDialog->setWindowModality(Qt::NonModal);
    m_pProgressDialog->setAutoClose(false);
    m_pProgressDialog->setAutoReset(false);
    m_pProgressDialog->setMinimumDuration(0);
    m_pProgressDialog->setValue(0);
    connect(m_pProgressDialog, &QProgressDialog::canceled, this, &StemHqService::slotCancel);
    m_pProgressDialog->show();

    StemHqWorker* pWorker = m_pWorker;
    QMetaObject::invokeMethod(
            pWorker,
            [pWorker, tracks, outputDir] {
                pWorker->process(tracks, outputDir);
            },
            Qt::QueuedConnection);
}

void StemHqService::slotTrackStarted(int index, int count, const QString& title) {
    if (!m_pProgressDialog) {
        return;
    }
    m_currentIndex = index;
    m_trackCount = count;
    const QString prefix = count > 1
            ? tr("Track %1 of %2: %3").arg(index + 1).arg(count).arg(title)
            : title;
    m_pProgressDialog->setLabelText(
            tr("%1\nSeparating with HT-Demucs, this takes around twice the track length...")
                    .arg(prefix));
    m_pProgressDialog->setValue(0);
}

void StemHqService::slotTrackProgress(double progress) {
    if (!m_pProgressDialog || m_trackCount <= 0) {
        return;
    }
    const double overall =
            (m_currentIndex + std::max(0.0, std::min(1.0, progress))) / m_trackCount;
    m_pProgressDialog->setValue(static_cast<int>(overall * 100.0));
}

void StemHqService::slotTrackFinished(const QString& trackLocation,
        bool ok,
        const QString& message) {
    if (ok) {
        ++m_succeeded;
        qDebug() << "StemHqService: high quality stems ready at" << message;
        emit hqStemsReady(trackLocation);
    } else {
        m_failures.append(message);
        qDebug() << "StemHqService: high quality stems failed for"
                 << trackLocation << ":" << message;
    }
}

void StemHqService::slotFailed(const QString& message) {
    m_failures.append(message);
}

void StemHqService::slotCancel() {
    if (!m_pWorker) {
        return;
    }
    if (m_pProgressDialog) {
        m_pProgressDialog->setLabelText(tr("Cancelling after the current segment..."));
    }
    m_pWorker->requestCancel();
}

void StemHqService::closeProgressDialog() {
    if (!m_pProgressDialog) {
        return;
    }
    m_pProgressDialog->disconnect(this);
    m_pProgressDialog->close();
    m_pProgressDialog->deleteLater();
    m_pProgressDialog = nullptr;
}

void StemHqService::showSummary() {
    QString text;
    if (m_succeeded == 1) {
        text = tr("High quality stems are ready. This track now uses them by default.");
    } else if (m_succeeded > 1) {
        text = tr("High quality stems are ready for %1 tracks. They are used by "
                  "default from now on.")
                       .arg(m_succeeded);
    }

    if (!m_failures.isEmpty()) {
        if (!text.isEmpty()) {
            text.append(QStringLiteral("\n\n"));
        }
        text.append(tr("Not converted:"));
        for (const QString& failure : m_failures) {
            text.append(QStringLiteral("\n  - %1").arg(failure));
        }
    }

    if (text.isEmpty()) {
        text = tr("No high quality stems were created.");
    }

    QMessageBox* pBox = new QMessageBox(
            m_failures.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
            tr("Add high quality stems"),
            text,
            QMessageBox::Ok,
            QApplication::activeWindow());
    pBox->setAttribute(Qt::WA_DeleteOnClose);
    pBox->open();
}

void StemHqService::slotAllFinished() {
    m_bBusy = false;
    closeProgressDialog();
    showSummary();
}

} // namespace mixxx

#include "moc_stemhqservice.cpp"
