#include "CaptureManager.h"

#include <pipewire/pipewire.h>

CaptureManager::CaptureManager(QObject *parent)
    : QObject(parent)
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

    pw_init(nullptr, nullptr);
    m_initialized = true;

    emit statusChanged(
        QStringLiteral("Biblioteca PipeWire inicializada; captura aún no conectada")
    );

    return true;
}

void CaptureManager::shutdown()
{
    if (!m_initialized)
        return;

    pw_deinit();
    m_initialized = false;

    emit statusChanged(QStringLiteral("PipeWire detenido"));
}

bool CaptureManager::isInitialized() const
{
    return m_initialized;
}
