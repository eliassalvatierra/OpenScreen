#pragma once

#include <QObject>
#include <QString>

class CaptureManager final : public QObject
{
    Q_OBJECT

public:
    explicit CaptureManager(QObject *parent = nullptr);
    ~CaptureManager() override;

    bool initialize();
    void shutdown();

    [[nodiscard]] bool isInitialized() const;

signals:
    void errorOccurred(const QString &message);
    void statusChanged(const QString &status);

private:
    bool m_initialized = false;
};
