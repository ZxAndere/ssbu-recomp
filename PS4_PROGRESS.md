# Progreso PS4 — bootstrap v0.2.0

Continuación de `ac2ee45` en `ps4/platform-diagnostic`. El objetivo de ejecutar Smash en PS4 sigue abierto.
Esta entrega incorpora servicios, dispositivos, carga ELF y ejecución de C producido por el lifter original.
**No hay partidas de Smash ejecutándose ni pruebas realizadas en una PS4 física.**

## Código implementado

| Componente | Implementación | Evidencia |
|---|---|---|
| Plataforma | Memoria y permisos, archivos con offset de 64 bits, hilos, mutex, condiciones, eventos, semáforos, TLS y módulos | Windows probado; Orbis compilado |
| Fibras | Resume/yield, pilas protegidas en Orbis, 16 slots locales por fibra y afinidad al hilo creador | 17 checks Windows; instrucciones SysV reales ejecutadas en Unicorn x86-64, comprobando GPR/RSP/MXCSR/x87 |
| DualShock 4 | sceUserService/scePad, cuatro usuarios, reconexión, botones Nintendo, zona muerta radial y vibración | Decodificador sintético probado; backend Orbis compilado |
| Audio | Cola PCM estéreo acotada, worker AudioOut a 48 kHz, bloques de 256 frames, silencio y contadores | FIFO/wrap/overflow/silencio probados; salida física pendiente |
| Pantalla | Dos framebuffers CPU lineales mediante sceVideoOut, texto propio y estado del arranque | Canarios de memoria en PC; backend compilado |
| Gráficos upstream | runtime/tex_decode.c integrado sin modificar | BC1 sintético comprobado; no equivale a renderizado 3D del juego |
| Memoria guest | RECOMP_ACCESS opcional en runtime.h/neon.h; imagen y pila traducidas a asignaciones host | Accesos, permisos y fallos comprobados; Windows mantiene acceso directo por defecto |
| Cargador | ELF64 AArch64, segmentos/BSS, límites/solapamientos, dynamic y relocaciones RELATIVE/ABS64/GLOB_DAT/JUMP_SLOT | Entradas válidas y malformadas probadas en PC |
| Recompilación | recomp.py original traduce cinco funciones ARM64 propias a C x86-64 | 1.000 comparaciones contra Unicorn ARM64 y enlace ELF/FSELF PS4 |
| Ejecución parcial | Tabla estática, entrada configurable, fingerprint, errores recuperables y presupuesto de instrucciones | Incluye bucle infinito, SVC, acceso inválido y tabla incorrecta |
| Pipeline privado | lift.py, manifiesto SHA-256 de imagen/fuentes, staging y response files | Fuentes/imagen cambiadas rechazadas; fixture enlazada en ambos destinos; entrada PC devolvió 1234 |

El ejecutor guest admite un contexto activo a la vez. Faltan el unwinder C++, migración de fibras,
handlers guest del SDK y la conexión completa de los dispositivos al juego. El adaptador implementa
la interfaz principal nnpad; motion y remapeo del menú original quedan pendientes.

## Pruebas ejecutadas

`scripts/ps4/verify.py` completó:

- **378 checks C**: plataforma 52, servicios 49, medios 47, fibras 17, recompilación 213.
- **14 pruebas Python**, sin omisiones, incluyendo **1.000 casos ARM64** comparados con ejecución nativa.
- Verificación del cambio de contexto SysV real mediante Unicorn x86-64.
- Prueba upstream rb_test: todos los casos pasaron; es una simulación de juguete, no online de Smash.
- Compilación Orbis de rb_test.c, rb_input.c, rb_wire.c y rb_timesync.c.
- ELF/FSELF y PKG público generados, hashes/firmas validados y payload extraído/comparado.
- Variante estática local compilada para Windows/PS4, usando exclusivamente código de prueba propio.

Evidencia: [verification.json](docs/ps4/verification.json), [salidas](docs/ps4/host-results.txt),
[manifiesto](docs/ps4/package-manifest.json), [validación PKG](docs/ps4/package-validation.txt).
Los GUEST STOP de SVC, dirección inválida y presupuesto agotado son pruebas negativas esperadas.
No se ejecutaron las pruebas de batalla/vídeo ni el diferencial sobre Smash por faltar el dump descifrado.

## Comportamiento del PKG

**SSBU Runtime Bootstrap**, Title ID `SSBD00001`, APP_VER `1.01`.
Ejecuta las pruebas y el código ARM64 propio recompilado. Intenta cargar `/data/ssbu/main.elf`;
si falta, lo registra. Si no se enlazó su tabla nativa, informa la dependencia. Nunca ejecuta ARM64 directamente.

Inicializa vídeo, controles y audio y muestra el estado. Cross solicita un tono corto; Options libera
los dispositivos y deja el proceso en espera. Todo ello está escrito/compilado y pendiente de verificación física.
El log se anexa a `/data/ssbu-ps4-diagnostic.log`. También puede cerrarse desde el menú PS.

El paquete público contiene código y fixtures propios, licencias y materiales homebrew OpenOrbis.
Las compilaciones con código local se separan en `build_ps4/*-private/` y no se publican automáticamente.

## Bloqueos concretos del juego

1. **Entrada ausente:** no se encontró main.elf, ExeFS main/sdk ni prod.keys en las rutas examinadas de
   C:\SmashUltimatePS4 y ubicaciones habituales. Los NSZ no se modificaron. Hace falta el ejecutable
   descifrado para generar/comprobar funciones, tablas e imports reales; esto permite seguir en PC sin probar consola.
2. **Runtime parcial:** se adaptan accesos del lifter y cinco imports libc. Faltan handlers guest del SDK,
   excepciones/landing pads, heaps, contextos múltiples, NRO y puentes nn::fs/hid/audio/NVN. Los servicios
   de plataforma implementados son la base de esos puentes, que todavía no están todos conectados.
3. **GPU:** hay framebuffer CPU. El renderer upstream exige Vulkan 1.3, colas gráficas/compute y shaders.
   No hay RADV Orbis instalado/validado. Piglet requiere módulos y shaders con otra interfaz y no reemplaza NVN automáticamente.
4. **Hardware:** CRT, ABI, permisos, flips, sonido, mandos y cierre 13.52/GoldHEN siguen sin verificar.

Commits publicados: f86036c (servicios), dbdb855 (dispositivos), 127d003 (bootstrap/fibras),
f9a8a30 (manifiestos/validación). v0.1.0 sigue conservada en su tag/release.
El checkout original permanece limpio en main. No se publicaron claves, respaldos, saves ni código derivado del juego.
No existen mediciones de FPS del juego ni una declaración de port terminado.
