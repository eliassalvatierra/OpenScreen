# OpenScreen

Aplicación de código abierto para compartir y extender el escritorio entre equipos.

## Estado actual

**Versión:** 0.2.0 — integración inicial con XDG Desktop Portal.

Incluye:
- Interfaz de escritorio con Qt 6 Widgets.
- Solicitud de sesión de captura mediante el portal ScreenCast.
- Selección de un monitor y solicitud de autorización al usuario mediante KDE.
- Cierre de la sesión del portal al cerrar la aplicación.

**Todavía no consume los fotogramas de PipeWire.** El portal autoriza y crea el flujo de pantalla; la conexión al descriptor remoto de PipeWire y la lectura de buffers son el siguiente objetivo.

## Entorno objetivo inicial

- CachyOS / Arch Linux
- KDE Plasma sobre Wayland
- Qt 6 (Widgets y DBus)
- PipeWire
- CMake y Ninja
- C++20

## Dependencias

En Arch Linux y derivados:

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base pipewire
```

## Compilar

Desde la raíz del repositorio:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Ejecutar

```bash
./build/openscreen
```

Al pulsar **Iniciar captura de pantalla**, KDE debe mostrar el diálogo de selección/autorización. Si se concede, OpenScreen informa que el portal creó el flujo de pantalla.

## Próximo objetivo técnico

Abrir el descriptor remoto con `OpenPipeWireRemote()`, conectar un consumidor PipeWire al flujo seleccionado y recibir los primeros buffers de vídeo.
