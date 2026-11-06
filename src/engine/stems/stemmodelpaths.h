#pragma once

#include <QString>

namespace mixxx {

/// Locate an ONNX model that ships next to the application (or lives in the
/// user's model directory). Shared by the real-time Spleeter engine and the
/// offline HT-Demucs engine so both look in exactly the same places.
///
/// Search order: <app>/models, <app>/../Resources/models (macOS bundle),
/// ./models, ., the platform app-data dir, ~/.mixxx/models,
/// ~/Library/Application Support/Mixxx/models, /usr/share/mixxx/models and
/// /usr/local/share/mixxx/models.
///
/// Returns an empty string when the model is not installed.
QString findStemModelFile(const QString& fileName);

} // namespace mixxx
