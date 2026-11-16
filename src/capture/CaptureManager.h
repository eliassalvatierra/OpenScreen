#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <atomic>

#include <spa/utils/hook.h>

struct pw_context;
struct pw_core;
struct pw_stream;
struct pw_thread_loop;

class CaptureManager final : public QObject
{
    Q_OBJECT

public:
    explicit CaptureManager(QObject *parent = nullptr);
    ~CaptureManager() override;

    bool initialize();
    void startCapture();
    void shutdown();

    [[nodiscard]] bool isInitialized() const;

signals:
    void errorOccurred(const QString &message);
    void statusChanged(const QString &status);

private slots:
    void onPortalResponse(uint response, const QVariantMap &results);

private:
    enum class PortalStage {
        Idle,
        CreateSession,
        SelectSources,
        Start
    };

    bool sendPortalRequest(const QString &method,
                           const QVariantList &arguments,
                           const QString &handleToken);
    QString createToken() const;
    QString requestPathForToken(const QString &token) const;
    void requestSession();
    void selectSources();
    void startPortalSession();
    void openPipeWireRemote(const QVariantMap &results);
    bool startPipeWire(int remoteFd);
    void stopPipeWire();
    void fail(const QString &message);

    static void onPipeWireProcess(void *data);

    QDBusConnection m_bus;
    QString m_sessionHandle;
    QString m_requestPath;
    PortalStage m_stage = PortalStage::Idle;
    bool m_initialized = false;

    uint32_t m_streamNodeId = 0;
    QString m_streamSerial;
    pw_thread_loop *m_pwLoop = nullptr;
    pw_context *m_pwContext = nullptr;
    pw_core *m_pwCore = nullptr;
    pw_stream *m_pwStream = nullptr;
    spa_hook m_streamListener{};
    std::atomic_bool m_firstFrameReceived{false};
};
