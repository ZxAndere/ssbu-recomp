# Progreso PS4 — entrada real en nnMain

Continuación de `ac2ee45`, rama `ps4/platform-diagnostic`, conservando los objetos compilados y los avances anteriores.
**En PC se completaron los 306 constructores y se entró en nnMain. Smash no se ha ejecutado en una PS4 física.**

## Arranque real comprobado

| Etapa | Resultado observado en PC |
|---|---|
| ExeFS propio | main/sdk extraídos localmente; los seis hashes de segmentos NSO son correctos |
| Main recompilado | 112.876 funciones reales, 1.162 fuentes C, ejecutable Windows completo enlazado |
| SDK adicional | 67 funciones reales: allocator, mutex/recursive_mutex, std::string y __next_prime |
| Enlazado guest | Imágenes separadas main/sdk, datos y RTTI reales, relocaciones sin entradas pendientes |
| nninitStartup | Ejecutado y retornado |
| Constructores | 306 entrados y 306 retornados, registrados individualmente después de cada llamada |
| nnMain | Entrado; inicializado el recurso nativo de ciclo de vida de aplicación |
| Parada actual | nn::oe::SetFocusHandlingMode, LR 0x7103741fb0, modo solicitado 2 |

La secuencia comprobada avanzó de 62 a 65, 105, 168, 245, 292 y finalmente 306 constructores completados.
El informe reproducible está en [private-boot-summary.json](docs/ps4/private-boot-summary.json).
Los logs completos, imágenes, fuentes generadas y ejecutables permanecen exclusivamente en build_ps4/.
Cero relocaciones pendientes **no significa que todos los imports tengan implementación**: los imports fuertes
sin puente reciben thunks que detienen la ejecución cuando se usan. Los símbolos débiles sin definición
real en un módulo quedan a cero, incluidos los hooks opcionales de nu y glslc.

## Cambios técnicos de esta continuación

- std::recursive_mutex y std::mutex usan sus implementaciones SDK recompiladas, enlazadas con mutex nativos.
  Se respetan recursión, try-lock, desbloqueo, destrucción y representación estática del SDK de 32 bytes.
  El ejecutor sigue limitado a un contexto guest activo: no hay prueba de contención entre hilos guest.
- Compatibilidad pthread para atributos y mutex normales/recursivos, inicializadores estáticos,
  EBUSY/EPERM/EINVAL/EAGAIN guest y sincronización del contador/propietario en memoria guest.
- Cadenas C con comprobación de límites; conversiones enteras LP64 con endptr, overflow y errno en TLS.
  wmemcpy y otras operaciones anchas usan elementos guest de 32 bits, independientemente del wchar_t Windows.
- Relojes realtime/monotónico, gettimeofday/time, TLS del hilo principal y ThreadType de 0x1c0 bytes;
  identidad lógica, nombre, prioridad, núcleo lógico cero e información de pila verificadas en PC.
- Reservas virtuales de hasta 64 GiB sin comprometerlas físicamente. El juego reservó 62 GiB y continuó.
  Las direcciones lógicas permanecen bajo 39 bits y no se solapan con los módulos. Se conserva el límite
  agregado de 512 MiB de páginas físicas y los permisos guest de 4 KiB sobre páginas Orbis de 16 KiB.
- Cruces entre exports SDK y puentes host, fingerprint del SDK y metadatos main/sdk con nombres de objeto
  distintos. La colisión local_game_program.o del enlace Orbis quedó corregida y comprobada mediante enlace.
- El lifter ya no incorpora funciones SDK contiguas por un fallthrough imposible después de una instrucción
  que siempre atrapa. Los main generados anteriores y sus objetos se conservaron sin regenerarlos.
- nn::oe::Initialize crea y posee un evento nativo y libera sus recursos al finalizar/parar. Los perfiles
  Nintendo solicitados se registran como hints; no se aplican frecuencias Tegra ni se cambian relojes nativos.
  Notificaciones, foco y política de energía siguen pendientes de adaptación completa.

La capa de plataforma conserva archivos de 64 bits, módulos, sync/TLS/eventos/semáforos, hilos nativos y
fibras con pila protegida. Hay adaptadores DualShock 4, PCM AudioOut y dos framebuffers CPU con texto propio.
Están compilados para Orbis y comprobados con pruebas sintéticas; no equivalen a HID/audio/NVN del juego integrados.
El bootstrap usa la pila guest de 1 MiB solicitada por este NPDM y una pila nativa independiente de 16 MiB.
Las funciones C muy grandes se dividen en helpers de 256 instrucciones; el presupuesto cuenta checkpoints,
no instrucciones ni FPS. El código completo se compiló a -O0 para esta prueba de arranque, no de rendimiento.

## Pruebas disponibles completadas

- **476 comprobaciones C**: plataforma 52, servicios 49, medios 47, fibras 21 y recompiler/runtime 307.
- **31 pruebas Python**, sin omisiones. Incluyen 1.000 casos contra Unicorn ARM64 y 1.000 comparaciones
  de estado nativo entre C dividido y sin dividir, además del cambio de contexto SysV en Unicorn x86-64.
- Modelo Windows de páginas Orbis: permisos parciales, canarios, cero al recomprometer y reserva de 62 GiB
  con solo una página host de 16 KiB comprometida al final del rango.
- rb_test upstream: 211 rollbacks, 512 frames resimulados, profundidad máxima 6; todos pasaron.
  Es una simulación de prueba, no rollback ni partidas de Smash verificadas.
- Compilación de los cuatro módulos de esa prueba para Orbis; enlace ELF/FSELF y empaquetado de fixtures
  propias; firmas/digests y payload extraído comparados. Variante estática propia enlazada en Windows/Orbis.

Evidencia: [verification.json](docs/ps4/verification.json), [salidas](docs/ps4/host-results.txt),
[manifiesto público](docs/ps4/package-manifest.json), [validación](docs/ps4/package-validation.txt).
Las paradas de SVC, dirección inválida, presupuesto y free inválido son pruebas negativas esperadas.
El arranque real retorna una parada controlada de import pendiente, no un resultado de gameplay correcto.

## Paquete experimental y pendientes

El paquete privado de arranque se generó en build_ps4/ps4-private-full/ con el main completo y las 67 funciones
SDK. Tamaño: 1.025.048.576 bytes; SHA-256: 27b0b29ca14321e8a1837fd3a63a5deb3b413b5744c06ce17f2ddbce415caee8.
ELF/FSELF, firmas/digests y payload extraído fueron validados. Se reutilizaron todos los objetos del main;
solo se recompilaron cinco fuentes SDK. Contiene material derivado del juego y **no se publica**.
El paquete público SSBU Runtime Bootstrap, SSBD00001, APP_VER 1.01 contiene exclusivamente pruebas propias,
licencias y materiales homebrew OpenOrbis. El release/tag v0.1.0 se conserva.

El siguiente punto de software es adaptar foco/notificaciones de nn::oe, después continuar los imports
observados en nnMain. No se presenta este import pendiente como un bloqueo que obligue a probar hardware.
Quedan unwinder/excepciones C++, múltiples contextos guest, módulos NRO, RomFS y puentes completos nn::fs,
HID/audio y NVN. El renderer upstream requiere Vulkan 1.3; el framebuffer CPU no dibuja escenas de Smash.

CRT/ABI, reservas virtuales grandes, permisos, flips, audio, mandos y comportamiento 13.52/GoldHEN requieren
validación física. La prueba concreta está preparada en [hardware-probe.md](docs/ps4/hardware-probe.md).
No hay mediciones de FPS ni compatibilidad física confirmada. Las claves, ExeFS, respaldos y código generado
no se suben a GitHub; el checkout original permanece intacto.

Referencia pública para tamaño ThreadType: [nnsdk-rs](https://github.com/ultimate-research/nnsdk-rs/blob/master/src/nn/os.rs).
Los offsets usados se contrastaron con accesores del SDK local; no se publicó su código.
