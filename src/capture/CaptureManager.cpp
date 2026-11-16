#include "CaptureManager.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QMetaObject>
#include <QUuid>

#include <cerrno>
#include <cstring>
#include <unistd.h>

#include <pipewire/pipewire.h>
#include <pipewire/thread-loop.h>
#include <spa/param/format.h>
#include <spa/param/video/format.h>
#include <spa/pod/builder.h>

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

    pw_init(nullptr, nullptr);
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
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), createToken());
    options.insert(QStringLiteral("session_handle_token"), createToken());

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
        openPipeWireRemote(results);
        break;

    case PortalStage::Idle:
        break;
    }
}

void CaptureManager::openPipeWireRemote(const QVariantMap &results)
{
    const QVariant streamsValue = results.value(QStringLiteral("streams"));
    if (!streamsValue.isValid()) {
        fail(QStringLiteral("KDE no devolvió la lista de flujos de pantalla."));
        return;
    }

    // La respuesta del portal es a(ua{sv}): array de estructuras.
    // QDBusArgument debe ser const para seleccionar sus operadores de lectura;
    // una instancia no-const intenta usar la API de escritura y provoca SIGABRT.
    const QDBusArgument streams = streamsValue.value<QDBusArgument>();
    streams.beginArray();

    if (streams.atEnd()) {
        streams.endArray();
        fail(QStringLiteral("La sesión no contiene ningún flujo de pantalla."));
        return;
    }

    QVariantMap streamProperties;
    streams.beginStructure();
    streams >> m_streamNodeId;
    streams >> streamProperties;
    streams.endStructure();
    streams.endArray();

    m_streamSerial = streamProperties.value(
        QStringLiteral("pipewire-serial")
    ).toString();

    if (m_streamNodeId == 0 && m_streamSerial.isEmpty()) {
        fail(QStringLiteral("El portal no proporcionó un identificador de flujo válido."));
        return;
    }

    emit statusChanged(
        QStringLiteral("Pantalla autorizada. Abriendo conexión privada de PipeWire...")
    );

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kPortalService),
        QString::fromLatin1(kPortalPath),
        QString::fromLatin1(kScreenCastInterface),
        QStringLiteral("OpenPipeWireRemote")
    );
    message.setArguments({
        QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
        QVariant::fromValue(QVariantMap{})
    });

    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher]() {
        if (watcher->isError()) {
            const QString error = watcher->error().message();
            watcher->deleteLater();
            fail(QStringLiteral("No se pudo abrir el acceso remoto de PipeWire: %1")
                     .arg(error));
            return;
        }

        const QDBusPendingReply<QDBusUnixFileDescriptor> reply(*watcher);
        if (!reply.isValid() || !reply.value().isValid()) {
            watcher->deleteLater();
            fail(QStringLiteral("El portal no devolvió un descriptor PipeWire válido."));
            return;
        }

        const QDBusUnixFileDescriptor remoteFd = reply.value();
        const int ownedFd = ::dup(remoteFd.fileDescriptor());
        watcher->deleteLater();

        if (ownedFd < 0) {
            fail(QStringLiteral("No se pudo duplicar el descriptor PipeWire: %1")
                     .arg(QString::fromLocal8Bit(std::strerror(errno))));
            return;
        }

        if (!startPipeWire(ownedFd)) {
            return;
        }

        m_stage = PortalStage::Idle;
        emit statusChanged(
            QStringLiteral("PipeWire conectado. Esperando el primer fotograma...")
        );
    });
}

bool CaptureManager::startPipeWire(int remoteFd)
{
    m_firstFrameReceived.store(false);

    m_pwLoop = pw_thread_loop_new("openscreen-capture", nullptr);
    if (!m_pwLoop) {
        ::close(remoteFd);
        fail(QStringLiteral("No se pudo crear el bucle de PipeWire."));
        return false;
    }

    m_pwContext = pw_context_new(
        pw_thread_loop_get_loop(m_pwLoop), nullptr, 0
    );
    if (!m_pwContext) {
        ::close(remoteFd);
        stopPipeWire();
        fail(QStringLiteral("No se pudo crear el contexto de PipeWire."));
        return false;
    }

    const int loopResult = pw_thread_loop_start(m_pwLoop);
    if (loopResult < 0) {
        ::close(remoteFd);
        stopPipeWire();
        fail(QStringLiteral("No se pudo iniciar el hilo de PipeWire."));
        return false;
    }
    m_pwLoopStarted = true;

    pw_thread_loop_lock(m_pwLoop);

    m_pwCore = pw_context_connect_fd(m_pwContext, remoteFd, nullptr, 0);
    if (!m_pwCore) {
        const QString error = QString::fromLocal8Bit(std::strerror(errno));
        pw_thread_loop_unlock(m_pwLoop);
        stopPipeWire();
        fail(QStringLiteral("No se pudo conectar al servidor PipeWire del portal: %1")
                 .arg(error));
        return false;
    }

    QString target = m_streamSerial;
    if (target.isEmpty())
        target = QString::number(m_streamNodeId);

    pw_properties *properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Screen",
        PW_KEY_TARGET_OBJECT, target.toUtf8().constData(),
        nullptr
    );

    m_pwStream = pw_stream_new(
        m_pwCore,
        "OpenScreen Screen Capture",
        properties
    );

    if (!m_pwStream) {
        pw_thread_loop_unlock(m_pwLoop);
        stopPipeWire();
        fail(QStringLiteral("No se pudo crear el flujo de vídeo PipeWire."));
        return false;
    }

    static const pw_stream_events streamEvents = [] {
        pw_stream_events events{};
        events.version = PW_VERSION_STREAM_EVENTS;
        events.process = &CaptureManager::onPipeWireProcess;
        return events;
    }();

    pw_stream_add_listener(
        m_pwStream,
        &m_streamListener,
        &streamEvents,
        this
    );

    uint8_t podBuffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(podBuffer, sizeof(podBuffer));
    const spa_pod *params[1];

    // SPA_POD_CHOICE_RANGE_* necesita punteros a estructuras con vida útil estable.
    const spa_rectangle preferredSize{1280, 720};
    const spa_rectangle minimumSize{1, 1};
    const spa_rectangle maximumSize{8192, 8192};
    const spa_fraction preferredFramerate{30, 1};
    const spa_fraction minimumFramerate{1, 1};
    const spa_fraction maximumFramerate{60, 1};

    params[0] = static_cast<const spa_pod *>(spa_pod_builder_add_object(
        &builder,
        SPA_TYPE_OBJECT_Format,
        SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(
            3,
            SPA_VIDEO_FORMAT_BGRx,
            SPA_VIDEO_FORMAT_BGRx,
            SPA_VIDEO_FORMAT_RGBx
        ),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(
            &preferredSize,
            &minimumSize,
            &maximumSize
        ),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
            &preferredFramerate,
            &minimumFramerate,
            &maximumFramerate
        )
    ));

    const int connectResult = pw_stream_connect(
        m_pwStream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
        ),
        params,
        1
    );

    pw_thread_loop_unlock(m_pwLoop);

    if (connectResult < 0) {
        stopPipeWire();
        fail(QStringLiteral("No se pudo conectar el flujo de vídeo: %1")
                 .arg(QString::fromLocal8Bit(std::strerror(-connectResult))));
        return false;
    }

    return true;
}

void CaptureManager::onPipeWireProcess(void *data)
{
    auto *self = static_cast<CaptureManager *>(data);
    if (!self || !self->m_pwStream)
        return;

    pw_buffer *buffer = pw_stream_dequeue_buffer(self->m_pwStream);
    if (!buffer)
        return;

    spa_buffer *spaBuffer = buffer->buffer;
    bool hasFrame = false;

    if (spaBuffer && spaBuffer->n_datas > 0) {
        const spa_data &plane = spaBuffer->datas[0];
        hasFrame = plane.data != nullptr &&
                   plane.chunk != nullptr &&
                   plane.chunk->size > 0;
    }

    pw_stream_queue_buffer(self->m_pwStream, buffer);

    if (hasFrame && !self->m_firstFrameReceived.exchange(true)) {
        QMetaObject::invokeMethod(
            self,
            [self]() {
                emit self->statusChanged(
                    QStringLiteral("¡Primer fotograma recibido desde PipeWire!")
                );
            },
            Qt::QueuedConnection
        );
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

void CaptureManager::stopPipeWire()
{
    if (!m_pwLoop)
        return;

    if (m_pwLoopStarted)
        pw_thread_loop_lock(m_pwLoop);

    if (m_pwStream) {
        spa_hook_remove(&m_streamListener);
        pw_stream_disconnect(m_pwStream);
        pw_stream_destroy(m_pwStream);
        m_pwStream = nullptr;
    }

    if (m_pwCore) {
        pw_core_disconnect(m_pwCore);
        m_pwCore = nullptr;
    }

    if (m_pwContext) {
        pw_context_destroy(m_pwContext);
        m_pwContext = nullptr;
    }

    if (m_pwLoopStarted) {
        pw_thread_loop_unlock(m_pwLoop);
        pw_thread_loop_stop(m_pwLoop);
        m_pwLoopStarted = false;
    }

    pw_thread_loop_destroy(m_pwLoop);
    m_pwLoop = nullptr;
}

void CaptureManager::shutdown()
{
    stopPipeWire();

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

    if (m_initialized)
        pw_deinit();

    m_stage = PortalStage::Idle;
    m_initialized = false;
    m_streamNodeId = 0;
    m_streamSerial.clear();
}

bool CaptureManager::isInitialized() const
{
    return m_initialized;
}
