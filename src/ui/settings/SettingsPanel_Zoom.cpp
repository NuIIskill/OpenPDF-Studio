#include "ui/settings/SettingsPanel.hpp"
#include "app/AppSettings.hpp"
#include "ui/settings/ToggleSwitch.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

QWidget *SettingsPanel::buildZoomPage()
{
    QWidget *page = nullptr;
    QVBoxLayout *vl = buildScrollPage(page, tr("Zoom"), tr("Configure how zoom works with the mouse wheel."));

    const auto makeToggleCard = [&](const QString &label, bool checked,
                                     QAbstractButton **out) -> QFrame * {
        auto *card = new QFrame(vl->parentWidget());
        card->setObjectName(QStringLiteral("ZoomCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *hl = new QHBoxLayout(card);
        hl->setContentsMargins(16, 14, 16, 14);
        hl->setSpacing(12);
        auto *lbl = new QLabel(label, card);
        lbl->setObjectName(QStringLiteral("ZoomRowLabel"));
        hl->addWidget(lbl, 1);
        auto *toggle = new ToggleSwitch(card);
        toggle->setChecked(checked);
        hl->addWidget(toggle);
        if (out) *out = toggle;
        return card;
    };

    {
        auto *card = new QFrame(vl->parentWidget());
        card->setObjectName(QStringLiteral("ZoomCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(16, 16, 16, 16);
        cl->setSpacing(10);

        auto *cardTitle = new QLabel(tr("Mouse Wheel Zoom Step"), card);
        cardTitle->setObjectName(QStringLiteral("ZoomCardTitle"));
        cl->addWidget(cardTitle);

        auto *stepRow = new QHBoxLayout;
        stepRow->setSpacing(12);
        auto *stepLbl = new QLabel(tr("Zoom step with Ctrl + Mouse Wheel"), card);
        stepLbl->setObjectName(QStringLiteral("ZoomRowLabel"));
        stepRow->addWidget(stepLbl, 1);
        m_zoomStepSpin = new QSpinBox(card);
        m_zoomStepSpin->setObjectName(QStringLiteral("ZoomStepSpin"));
        m_zoomStepSpin->setRange(1, 50);
        m_zoomStepSpin->setValue(m_settings->zoomStep());
        m_zoomStepSpin->setSuffix(QStringLiteral(" %"));
        m_zoomStepSpin->setFixedWidth(88);
        stepRow->addWidget(m_zoomStepSpin);
        cl->addLayout(stepRow);

        auto *stepDesc = new QLabel(tr("Sets how much is zoomed per mouse wheel movement."), card);
        stepDesc->setObjectName(QStringLiteral("ZoomRowDesc"));
        cl->addWidget(stepDesc);

        auto *exBox = new QFrame(card);
        exBox->setObjectName(QStringLiteral("ZoomExampleBox"));
        exBox->setAttribute(Qt::WA_StyledBackground, true);
        auto *exRow = new QHBoxLayout(exBox);
        exRow->setContentsMargins(10, 8, 10, 8);
        exRow->setSpacing(6);
        auto *exIcon = new QLabel(QStringLiteral("↗"), exBox);
        exIcon->setObjectName(QStringLiteral("ZoomExampleIcon"));
        exRow->addWidget(exIcon);
        m_zoomExampleLabel = new QLabel(exBox);
        m_zoomExampleLabel->setObjectName(QStringLiteral("ZoomExampleText"));
        exRow->addWidget(m_zoomExampleLabel, 1);
        cl->addWidget(exBox);

        vl->addWidget(card);
    }

    vl->addWidget(makeToggleCard(
        tr("Zoom with Ctrl + Mouse Wheel"),
        m_settings->ctrlWheelZoom(),
        &m_ctrlWheelToggle));

    vl->addWidget(makeToggleCard(
        tr("Zoom to Mouse Pointer"),
        m_settings->zoomToPointer(),
        &m_zoomPtrToggle));

    {
        auto *card = new QFrame(vl->parentWidget());
        card->setObjectName(QStringLiteral("ZoomCard"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *hl = new QHBoxLayout(card);
        hl->setContentsMargins(16, 14, 16, 14);
        hl->setSpacing(12);
        auto *lbl = new QLabel(tr("Action without Ctrl"), card);
        lbl->setObjectName(QStringLiteral("ZoomRowLabel"));
        hl->addWidget(lbl, 1);
        m_wheelActionCombo = new QComboBox(card);
        m_wheelActionCombo->setObjectName(QStringLiteral("ZoomActionCombo"));
        m_wheelActionCombo->addItem(tr("Scroll"), QStringLiteral("scroll"));
        m_wheelActionCombo->addItem(tr("Zoom"),   QStringLiteral("zoom"));
        const QString act = m_settings->wheelAction();
        for (int i = 0; i < m_wheelActionCombo->count(); ++i) {
            if (m_wheelActionCombo->itemData(i).toString() == act) {
                m_wheelActionCombo->setCurrentIndex(i);
                break;
            }
        }
        hl->addWidget(m_wheelActionCombo);
        vl->addWidget(card);
    }

    finishScrollPage(vl);

    const auto updateExample = [this]() {
        m_zoomExampleLabel->setText(
            tr("Example: 100 % → %1 %").arg(100 + m_zoomStepSpin->value()));
    };
    updateExample();
    connect(m_zoomStepSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, updateExample);

    return page;
}
