// SPDX-License-Identifier: GPL-3.0-or-later
#include "demo_controller.hpp"

#include "globe_view.hpp"
#include "main_window.hpp"

#include <QFile>
#include <QMessageBox>

#include <cmath>

DemoController::DemoController(MainWindow& window, GlobeView& globe)
    : QObject(&window), m_window(window), m_globe(globe) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] { show(m_index + 1); });
    connect(&m_globe, &GlobeView::demoNextRequested, this, [this] { show(m_index + 1); });
    connect(&m_globe, &GlobeView::demoPreviousRequested, this,
            [this] { show(std::max(0, m_index - 1)); });
    connect(&m_globe, &GlobeView::demoEndRequested, this, &DemoController::stop);
}

bool DemoController::start(int first) {
    QFile file(QStringLiteral(":/demos/tour.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(&m_window, tr("Demo tour"), tr("The demo tour is missing."));
        return false;
    }
    const QByteArray text = file.readAll();
    try {
        m_demo = orbit::demoFromJson(
            std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
    } catch (const orbit::DemoError& e) {
        QMessageBox::warning(&m_window, tr("Demo tour"),
                             tr("The demo tour can't be read:\n%1").arg(e.what()));
        return false;
    }
    show(first);
    return true;
}

void DemoController::stop() {
    m_timer.stop();
    m_index = -1;
    m_globe.hideCaption();
}

void DemoController::show(int index) {
    if (m_demo.steps.empty()) return;
    // Past the last step, stay on it: the user ends the tour.
    m_index = std::clamp(index, 0, static_cast<int>(m_demo.steps.size()) - 1);
    const orbit::DemoStep& step = m_demo.steps[static_cast<std::size_t>(m_index)];

    QFile scenario(QStringLiteral(":/scenarios/") + QString::fromStdString(step.scenario));
    if (!scenario.open(QIODevice::ReadOnly) ||
        !m_window.openScenarioText(scenario.readAll(), QString())) {
        stop();
        return;
    }
    m_globe.applyDemoStepOnNextResult(step);
    m_globe.showCaption(QString::fromStdString(step.title), QString::fromStdString(step.caption),
                        m_index + 1, static_cast<int>(m_demo.steps.size()));

    m_timer.stop();
    if (m_index + 1 < static_cast<int>(m_demo.steps.size())) {
        m_timer.start(static_cast<int>(std::lround(step.seconds * 1000.0)));
    }
}
