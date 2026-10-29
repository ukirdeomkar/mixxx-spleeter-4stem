#include "widget/weffectparameternamebase.h"

#include <QDrag>
#include <QPair>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

#include "effects/effectknobparameterslot.h"
#include "effects/effectparameterslotbase.h"
#include "effects/effectslot.h"
#include "moc_weffectparameternamebase.cpp"
#include "util/dnd.h"
#include "util/math.h"

#include <cmath>

namespace {
const QString kMimeTextDelimiter = QStringLiteral("\n");

/// Musical name for a duration expressed in beats, e.g. 0.25 -> "1/4".
/// Beat-synced effects store their time as a plain number of beats, which is
/// unreadable as "0.25" when what you want to see is the beat division.
QString formatBeatDivision(double beats) {
    static const QList<QPair<double, QString>> kDivisions = {
            {1.0 / 32, QStringLiteral("1/32")},
            {1.0 / 16, QStringLiteral("1/16")},
            {1.0 / 8, QStringLiteral("1/8")},
            {3.0 / 16, QStringLiteral("3/16")},
            {1.0 / 4, QStringLiteral("1/4")},
            {3.0 / 8, QStringLiteral("3/8")},
            {1.0 / 2, QStringLiteral("1/2")},
            {3.0 / 4, QStringLiteral("3/4")},
            {1.0, QStringLiteral("1")},
            {1.5, QStringLiteral("1 1/2")},
            {2.0, QStringLiteral("2")},
    };
    for (const auto& division : kDivisions) {
        if (std::abs(division.first - beats) < 1e-4) {
            return division.second;
        }
    }
    return QString();
}
} // anonymous namespace

WEffectParameterNameBase::WEffectParameterNameBase(
        QWidget* pParent, EffectsManager* pEffectsManager)
        : WLabel(pParent),
          m_pEffectsManager(pEffectsManager),
          m_widthHint(0) {
    setAcceptDrops(true);
    setCursor(Qt::OpenHandCursor);
    parameterUpdated();
    // When the parameter value changed it is display briefly.
    // Set up the timer that restores the parameter name.
    m_displayNameResetTimer.setSingleShot(true);
    m_displayNameResetTimer.setInterval(800);
    m_displayNameResetTimer.callOnTimeout(this, [this]() { setText(m_text); });
}

void WEffectParameterNameBase::setEffectParameterSlot(
        EffectParameterSlotBasePointer pEffectKnobParameterSlot) {
    m_pParameterSlot = pEffectKnobParameterSlot;
    if (m_pParameterSlot) {
        connect(m_pParameterSlot.data(),
                &EffectParameterSlotBase::updated,
                this,
                &WEffectParameterNameBase::parameterUpdated);
        if (qobject_cast<EffectKnobParameterSlot*>(m_pParameterSlot.data())) {
            // Make connection to show parameter value instead of name briefly
            // after value has changed.
            connect(m_pParameterSlot.data(),
                    &EffectParameterSlotBase::valueChanged,
                    this,
                    &WEffectParameterNameBase::showNewValue);
        }
    }
    parameterUpdated();
}

namespace {
QString beatDivisionText(double beats) {
    return formatBeatDivision(beats);
}
} // anonymous namespace

void WEffectParameterNameBase::parameterUpdated() {
    int valueWidth = 0;
    QFontMetrics metrics(font());
    if (m_pParameterSlot) {
        if (m_showValueOnly) {
            auto* pKnobSlot = qobject_cast<EffectKnobParameterSlot*>(
                    m_pParameterSlot.data());
            const double value =
                    pKnobSlot ? pKnobSlot->getValueParameter() : 0.0;
            EffectManifestParameterPointer pManifest =
                    m_pParameterSlot->getManifest();
            const QString division =
                    (pManifest &&
                            pManifest->unitsHint() ==
                                    EffectManifestParameter::UnitsHint::Beats)
                    ? beatDivisionText(value)
                    : QString();
            // Always show a value: the musical division when the parameter is
            // a time in beats, otherwise the plain number.
            m_text = division.isEmpty()
                    ? QString::number(value, 'g', 3)
                    : division;
        } else if (!m_pParameterSlot->shortName().isEmpty()) {
            m_text = m_pParameterSlot->shortName();
        } else {
            m_text = m_pParameterSlot->name();
        }
        setBaseTooltip(QStringLiteral("%1\n%2").arg(
                m_pParameterSlot->name(),
                m_pParameterSlot->description()));
        EffectManifestParameterPointer pManifest = m_pParameterSlot->getManifest();
        if (!pManifest.isNull()) {
            m_unitString = m_pParameterSlot->getManifest()->unitString();
            if (!m_unitString.isEmpty()) {
                m_unitString.prepend(QChar(' '));
            }
            double maxValue = m_pParameterSlot->getManifest()->getMaximum();
            double minValue = m_pParameterSlot->getManifest()->getMaximum();
            QString maxValueString = QString::number(maxValue - 0.01) + m_unitString;
            QString minValueString = QString::number(minValue + 0.01) + m_unitString;
            valueWidth = math_max(
                    metrics.size(0, maxValueString).width(),
                    metrics.size(0, minValueString).width());
        } else {
            m_unitString = QString();
        }
    } else {
        m_unitString = QString();
        m_text = kNoEffectString;
        setBaseTooltip(tr("No effect loaded."));
    }
    // frameWidth() is the maximum of the sum of margin, border and padding
    // width of the left and the right side.
    m_widthHint = math_max(
                          valueWidth,
                          metrics.size(0, m_text).width()) +
            2 * frameWidth();
    setText(m_text);
    m_parameterUpdated = true;
}

void WEffectParameterNameBase::showNewValue(double newValue) {
    // Don't show the value for a newly loaded parameter. 'valueChanged' is emitted
    // if this parameter is linked to the Meta knob
    if (m_parameterUpdated) {
        m_parameterUpdated = false;
        return;
    }
    if (m_showValueOnly) {
        EffectManifestParameterPointer pManifest =
                m_pParameterSlot ? m_pParameterSlot->getManifest()
                                 : EffectManifestParameterPointer();
        const bool isBeatParameter = pManifest &&
                pManifest->unitsHint() ==
                        EffectManifestParameter::UnitsHint::Beats;
        if (!isBeatParameter) {
            // Non-beat effects (EQ, filter, ...) still show their value,
            // persistently, just without a musical division.
            setText(QString::number(newValue, 'g', 3));
            return;
        }
    }

    int absVal = abs(static_cast<int>(round(newValue)));
    int tenPowDecimals = 1; // omit decimals
    if (absVal < 100) {     // 0-99: round to 2 decimals
        tenPowDecimals = 100;
    } else if (absVal < 1000) { // 100-999: round to 1 decimal
        tenPowDecimals = 10;
    }
    double dispVal = round(newValue * tenPowDecimals) / tenPowDecimals;

    if (m_pParameterSlot) {
        EffectManifestParameterPointer pManifest = m_pParameterSlot->getManifest();
        if (pManifest &&
                pManifest->unitsHint() ==
                        EffectManifestParameter::UnitsHint::Beats) {
            const QString beatDivision = beatDivisionText(newValue);
            if (!beatDivision.isEmpty()) {
                setText(beatDivision);
                if (!m_showValueOnly) {
                    m_displayNameResetTimer.start();
                }
                return;
            }
        }
    }

    setText(QString::number(dispVal) + m_unitString);
    m_displayNameResetTimer.start();
}

void WEffectParameterNameBase::mousePressEvent(QMouseEvent* pEvent) {
    VERIFY_OR_DEBUG_ASSERT(m_pParameterSlot && m_pParameterSlot->isLoaded()) {
        return;
    }
    VERIFY_OR_DEBUG_ASSERT(m_pEffectSlot && m_pEffectSlot->isLoaded()) {
        return;
    }
    DragAndDropHelper::mousePressed(pEvent);
}

void WEffectParameterNameBase::mouseMoveEvent(QMouseEvent* pEvent) {
    if (DragAndDropHelper::mouseMoveInitiatesDrag(pEvent)) {
        QDrag* drag = new QDrag(this);
        QMimeData* mimeData = new QMimeData;

        mimeData->setText(
                mimeTextIdentifier() + kMimeTextDelimiter +
                m_pEffectSlot->getManifest()->uniqueId() + kMimeTextDelimiter +
                QString::number(m_pParameterSlot->slotNumber()));
        drag->setMimeData(mimeData);
        drag->exec();
    }
}

void WEffectParameterNameBase::dragEnterEvent(QDragEnterEvent* pEvent) {
    VERIFY_OR_DEBUG_ASSERT(m_pParameterSlot && m_pParameterSlot->isLoaded()) {
        return;
    }
    VERIFY_OR_DEBUG_ASSERT(m_pEffectSlot && m_pEffectSlot->isLoaded()) {
        return;
    }
    const QString& mimeText = pEvent->mimeData()->text();
    QStringList mimeTextLines = mimeText.split(kMimeTextDelimiter);
    if (mimeTextLines.at(0) == mimeTextIdentifier() &&
            mimeTextLines.at(1) == m_pEffectSlot->getManifest()->uniqueId()) {
        pEvent->acceptProposedAction();
    }
}

void WEffectParameterNameBase::dropEvent(QDropEvent* pEvent) {
    VERIFY_OR_DEBUG_ASSERT(m_pParameterSlot && m_pParameterSlot->isLoaded()) {
        return;
    }
    VERIFY_OR_DEBUG_ASSERT(m_pEffectSlot && m_pEffectSlot->isLoaded()) {
        return;
    }
    const QString& mimeText = pEvent->mimeData()->text();
    QStringList mimeTextLines = mimeText.split(kMimeTextDelimiter);
    m_pEffectSlot->swapParameters(m_pParameterSlot->parameterType(),
            m_pParameterSlot->slotNumber(),
            mimeTextLines.at(2).toInt());
}

const QString WEffectParameterNameBase::mimeTextIdentifier() const {
    return QStringLiteral("Mixxx effect parameter ") +
            QString::number(
                    static_cast<int>(m_pParameterSlot->parameterType()));
}

QSize WEffectParameterNameBase::sizeHint() const {
    // make sure the sizeHint is not changing because of the label or value string
    QSize size = WLabel::sizeHint();
    size.setWidth(m_widthHint);
    return size;
}
