# OpenScreen

Aplicación de código abierto para compartir y extender el escritorio entre equipos.

## Estado actual

**Versión:** 0.3.0 — conexión inicial del flujo de captura con PipeWire.

Incluye:
- Interfaz de escritorio con Qt 6 Widgets.
- Creación y autorización de sesión mediante XDG Desktop Portal.
- Selección de un monitor desde el diálogo de KDE.
- Apertura del descriptor remoto con `OpenPipeWireRemote()`.
- Conexión de un consumidor PipeWire al flujo autorizado.
- Detección del primer buffer de vídeo recibido.

La versión actual confirma la recepción del primer buffer; todavía no presenta una vista previa, codifica ni transmite el vídeo.

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

Al pulsar **Iniciar captura de pantalla**, KDE muestra el selector de monitor. Después de autorizar, OpenScreen abre el descriptor PipeWire y espera el primer buffer de vídeo. Cuando lo recibe, muestra el mensaje correspondiente en el estado del sistema.

## Próximo objetivo técnico

Mostrar una vista previa de los fotogramas capturados y medir resolución/FPS. Después se integrará la codificación de vídeo.
