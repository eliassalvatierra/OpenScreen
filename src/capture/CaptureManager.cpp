#include "CaptureManager.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUuid>

namespace {
constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kScreenCastInterface = "org.freedesktop.portal.ScreenCast";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";
}

CaptureManager::CaptureManager(QObject *parent)
    : QObject(parent),
      m_bus(QDBusConnection::sessionBus())
{
}

CaptureManager::~CaptureManager()
{
    shutdown();
}

bool CaptureManager::initialize()
{
    if (m_initialized)
        return true;

    if (!m_bus.isConnected()) {
        emit errorOccurred(
            QStringLiteral("No se pudo conectar al bus de sesión D-Bus.")
        );
        return false;
    }

    m_initialized = true;
    emit statusChanged(
        QStringLiteral("Listo. La captura se inicia cuando lo solicites.")
    );
    return true;
}

void CaptureManager::startCapture()
{
    if (!m_initialized && !initialize())
        return;

    if (m_stage != PortalStage::Idle || !m_sessionHandle.isEmpty()) {
        emit statusChanged(
            QStringLiteral("Ya hay una solicitud o sesión de captura activa.")
        );
        return;
    }

    emit statusChanged(
        QStringLiteral("Solicitando permiso de captura a KDE...")
    );
    requestSession();
}

void CaptureManager::requestSession()
{
    const QString sessionToken = createToken();
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), createToken());
    options.insert(QStringLiteral("session_handle_token"), sessionToken);

    m_stage = PortalStage::CreateSession;
    sendPortalRequest(
        QStringLiteral("CreateSession"),
        {QVariant::fromValue(options)},
        options.value(QStringLiteral("handle_token")).toString()
    );
}

void CaptureManager::selectSources()
{
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), createToken());
    options.insert(QStringLiteral("types"), uint(1)); // MONITOR
    options.insert(QStringLiteral("multiple"), false);
    options.insert(QStringLiteral("cursor_mode"), uint(2)); // Embedded

    m_stage = PortalStage::SelectSources;
    sendPortalRequest(
        QStringLiteral("SelectSources"),
        {
            QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
            QVariant::fromValue(options)
        },
        options.value(QStringLiteral("handle_token")).toString()
    );
}

void CaptureManager::startPortalSession()
{
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), createToken());

    m_stage = PortalStage::Start;
    sendPortalRequest(
        QStringLiteral("Start"),
        {
            QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
            QString(),
            QVariant::fromValue(options)
        },
        options.value(QStringLiteral("handle_token")).toString()
    );
}

bool CaptureManager::sendPortalRequest(
    const QString &method,
    const QVariantList &arguments,
    const QString &handleToken)
{
    const QString requestPath = requestPathForToken(handleToken);

    if (!m_bus.connect(
            QString::fromLatin1(kPortalService),
            requestPath,
            QString::fromLatin1(kRequestInterface),
            QStringLiteral("Response"),
            this,
            SLOT(onPortalResponse(uint,QVariantMap)))) {
        fail(QStringLiteral("No se pudo escuchar la respuesta del portal de KDE."));
        return false;
    }

    m_requestPath = requestPath;

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kPortalService),
        QString::fromLatin1(kPortalPath),
        QString::fromLatin1(kScreenCastInterface),
        method
    );
    message.setArguments(arguments);

    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, requestPath]() {
        if (watcher->isError()) {
            const QString error = watcher->error().message();
            m_bus.disconnect(
                QString::fromLatin1(kPortalService),
                requestPath,
                QString::fromLatin1(kRequestInterface),
                QStringLiteral("Response"),
                this,
                SLOT(onPortalResponse(uint,QVariantMap))
            );
            if (m_requestPath == requestPath)
                m_requestPath.clear();
            fail(QStringLiteral("Error al comunicarse con el portal: %1").arg(error));
        } else {
            const QDBusPendingReply<QDBusObjectPath> reply(*watcher);
            if (reply.isValid() && reply.value().path() != requestPath) {
                m_bus.disconnect(
                    QString::fromLatin1(kPortalService),
                    requestPath,
                    QString::fromLatin1(kRequestInterface),
                    QStringLiteral("Response"),
                    this,
                    SLOT(onPortalResponse(uint,QVariantMap))
                );
                const QString actualPath = reply.value().path();
                if (!m_bus.connect(
                        QString::fromLatin1(kPortalService),
                        actualPath,
                        QString::fromLatin1(kRequestInterface),
                        QStringLiteral("Response"),
                        this,
                        SLOT(onPortalResponse(uint,QVariantMap)))) {
                    fail(QStringLiteral("No se pudo conectar con la respuesta del portal."));
                } else {
                    m_requestPath = actualPath;
                }
            }
        }
        watcher->deleteLater();
    });

    return true;
}

void CaptureManager::onPortalResponse(
    uint response,
    const QVariantMap &results)
{
    if (!m_requestPath.isEmpty()) {
        m_bus.disconnect(
            QString::fromLatin1(kPortalService),
            m_requestPath,
            QString::fromLatin1(kRequestInterface),
            QStringLiteral("Response"),
            this,
            SLOT(onPortalResponse(uint,QVariantMap))
        );
        m_requestPath.clear();
    }

    if (response != 0) {
        const QString message = response == 1
            ? QStringLiteral("La solicitud de captura fue cancelada.")
            : QStringLiteral("KDE no pudo completar la solicitud de captura.");
        shutdown();
        emit statusChanged(message);
        return;
    }

    switch (m_stage) {
    case PortalStage::CreateSession:
        m_sessionHandle = results.value(QStringLiteral("session_handle")).toString();
        if (m_sessionHandle.isEmpty()) {
            fail(QStringLiteral("El portal no devolvió un identificador de sesión."));
            return;
        }
        emit statusChanged(
            QStringLiteral("Sesión creada. Preparando la selección de pantalla...")
        );
        selectSources();
        break;

    case PortalStage::SelectSources:
        emit statusChanged(
            QStringLiteral("Pantalla configurada. Esperando confirmación de KDE...")
        );
        startPortalSession();
        break;

    case PortalStage::Start:
        if (!results.contains(QStringLiteral("streams"))) {
            fail(QStringLiteral("KDE autorizó la solicitud, pero no devolvió un flujo de pantalla."));
            return;
        }
        m_stage = PortalStage::Idle;
        emit statusChanged(
            QStringLiteral("Permiso concedido: KDE creó el flujo de pantalla. "
                           "La conexión de vídeo con PipeWire es el siguiente paso.")
        );
        break;

    case PortalStage::Idle:
        break;
    }
}

QString CaptureManager::createToken() const
{
    return QStringLiteral("openscreen_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)
                 .remove(QLatin1Char('-')));
}

QString CaptureManager::requestPathForToken(const QString &token) const
{
    QString sender = m_bus.baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));

    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
        .arg(sender, token);
}

void CaptureManager::fail(const QString &message)
{
    shutdown();
    emit errorOccurred(message);
}

void CaptureManager::shutdown()
{
    if (!m_requestPath.isEmpty()) {
        m_bus.disconnect(
            QString::fromLatin1(kPortalService),
            m_requestPath,
            QString::fromLatin1(kRequestInterface),
            QStringLiteral("Response"),
            this,
            SLOT(onPortalResponse(uint,QVariantMap))
        );
        m_requestPath.clear();
    }

    if (!m_sessionHandle.isEmpty() && m_bus.isConnected()) {
        QDBusMessage closeMessage = QDBusMessage::createMethodCall(
            QString::fromLatin1(kPortalService),
            m_sessionHandle,
            QStringLiteral("org.freedesktop.portal.Session"),
            QStringLiteral("Close")
        );
        m_bus.asyncCall(closeMessage);
        m_sessionHandle.clear();
    }

    m_stage = PortalStage::Idle;
    m_initialized = false;
}

bool CaptureManager::isInitialized() const
{
    return m_initialized;
}
