#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantMap>

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
    void fail(const QString &message);

    QDBusConnection m_bus;
    QString m_sessionHandle;
    QString m_requestPath;
    PortalStage m_stage = PortalStage::Idle;
    bool m_initialized = false;
};
