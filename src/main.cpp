#include <QApplication>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include "capture/CaptureManager.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName(QStringLiteral("OpenScreen"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    CaptureManager captureManager;
    captureManager.initialize();

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("OpenScreen"));
    window.resize(960, 640);
    window.setMinimumSize(720, 480);

    auto *central = new QWidget(&window);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(18);

    auto *title = new QLabel(QStringLiteral("OpenScreen"), central);
    QFont titleFont = title->font();
    titleFont.setPointSize(26);
    titleFont.setBold(true);
    title->setFont(titleFont);

    auto *subtitle = new QLabel(
        QStringLiteral("Compartí y extendé tu escritorio entre dispositivos."),
        central
    );

    auto *systemCard = new QFrame(central);
    systemCard->setFrameShape(QFrame::StyledPanel);
    auto *systemLayout = new QVBoxLayout(systemCard);

    auto *systemTitle = new QLabel(
        QStringLiteral("Estado del sistema"), systemCard
    );
    QFont sectionFont = systemTitle->font();
    sectionFont.setPointSize(12);
    sectionFont.setBold(true);
    systemTitle->setFont(sectionFont);

    auto *systemStatus = new QLabel(
        QStringLiteral("Inicializando componentes..."), systemCard
    );

    systemLayout->addWidget(systemTitle);
    systemLayout->addWidget(systemStatus);

    auto *devicesTitle = new QLabel(
        QStringLiteral("Dispositivos"), central
    );
    devicesTitle->setFont(sectionFont);

    auto *devicesStatus = new QLabel(
        QStringLiteral("Todavía no hay dispositivos conectados."),
        central
    );

    auto *nextTitle = new QLabel(
        QStringLiteral("Próximas etapas"), central
    );
    nextTitle->setFont(sectionFont);

    auto *nextSteps = new QLabel(
        QStringLiteral(
            "1. Conectar con el portal de captura de Wayland\n"
            "2. Recibir fotogramas desde PipeWire\n"
            "3. Codificar vídeo con aceleración por hardware\n"
            "4. Transmitir a los dispositivos cliente"
        ),
        central
    );

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(8);
    layout->addWidget(systemCard);
    layout->addWidget(devicesTitle);
    layout->addWidget(devicesStatus);
    layout->addWidget(nextTitle);
    layout->addWidget(nextSteps);
    layout->addStretch();

    QObject::connect(
        &captureManager,
        &CaptureManager::statusChanged,
        systemStatus,
        &QLabel::setText
    );

    QObject::connect(
        &captureManager,
        &CaptureManager::errorOccurred,
        systemStatus,
        &QLabel::setText
    );

    window.setCentralWidget(central);
    window.statusBar()->showMessage(
        QStringLiteral("OpenScreen 0.1.0 | CachyOS | KDE Plasma | Wayland")
    );

    window.show();
    return app.exec();
}
