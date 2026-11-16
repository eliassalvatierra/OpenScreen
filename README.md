# OpenScreen

Aplicación de código abierto para compartir y extender el escritorio entre equipos.

## Estado actual

**Versión:** 0.1.0 — estructura inicial.

Incluye:
- Interfaz de escritorio con Qt 6 Widgets.
- Integración inicial con la biblioteca PipeWire.
- Estructura C++ y configuración CMake.

Todavía no incluye captura real de pantalla, codificación de vídeo, transmisión de red ni monitor virtual.

## Entorno objetivo inicial

- CachyOS / Arch Linux
- KDE Plasma sobre Wayland
- Qt 6
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

## Próximo objetivo técnico

Validar una sesión de captura mediante XDG Desktop Portal (ScreenCast) y recibir los fotogramas desde PipeWire. La inicialización actual de la biblioteca PipeWire no representa todavía una captura activa.
