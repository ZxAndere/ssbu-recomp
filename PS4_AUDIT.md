# Auditoría del port a PS4

Fecha: 19 de octubre de 2026. Base revisada: `80c9012` de `Denkishi/ssbu-recomp`.
El resultado actual es un bootstrap nativo parcial. **Smash Ultimate no se ha ejecutado en PS4.**

## Avances sobre la auditoría inicial

Después de `ac2ee45` se implementaron sync/TLS/módulos/archivos, fibras, DualShock 4, AudioOut,
framebuffer CPU, carga ELF/relocaciones, traducción guest→host y enlace estático de C generado.
La evidencia actual está en [PS4_PROGRESS.md](PS4_PROGRESS.md). La tabla siguiente conserva el
análisis de la base Windows; sus pendientes iniciales deben leerse junto al estado actualizado.
Los dispositivos están compilados, pendientes de prueba física. Además de las cinco funciones propias, se extrajo el ExeFS local, se generó el main completo y se
completaron los 306 constructores y se entró en nnMain en PC con funciones SDK reales. Véase el progreso actualizado.

## Alcance y método

Se revisaron `BUILDING.md`, el pipeline del lifter, la carga del runtime, sus interfaces de plataforma,
los scripts de instalación y las pruebas. `graphics_sdk/` **no existe** en este checkout: los gráficos
están principalmente en `runtime/nvn_*.c`, `runtime/mx_*` y `runtime/display.c`.
`fighter_sdk/` sí existe y también se incluyó por sus DLL, TLS y módulos.

`python scripts/ps4/audit.py` recorre los archivos de texto seguidos por Git y produce
[el inventario](docs/ps4/inventory.md) y [la evidencia completa por archivo y línea](docs/ps4/dependencies.tsv).
Busca expresamente `windows.h`, `LoadLibrary`, `GetProcAddress`, `VirtualAlloc`, `CreateThread`,
`QueryPerformanceCounter`, `SDL_` y `vkCreateInstance`, además de familias de memoria, sincronización,
archivos, red, audio, COM y compilación. Los resultados incluyen comentarios y ramas condicionales;
son candidatos de revisión, no un grafo de llamadas ni una prueba de alcanzabilidad.
La auditoría textual inicial no examinó claves ni respaldos. Después se usaron las claves locales indicadas
por el usuario para extraer ExeFS del NSZ; no se copiaron ni publicaron claves o respaldos.

## Hallazgos principales

| Subsistema y evidencia en la base | Dependencia / dificultad | Alternativa y trabajo necesario |
|---|---|---|
| `tools/lifter/build.py:21`, `:40`, `:142`; `BUILDING.md` | Clang Windows, COFF, `lld-link`, import libraries, DLL; alta | Conservar ARM64 → C; compilar el C otra vez para ELF x86-64 SysV. Los objetos Windows no sirven para Orbis. El diagnóstico ya usa un enlace separado. |
| `runtime/game_shim.c:34`, `runtime/gamedll.c:28`, `runtime/nro_shim.c:43` | Exportaciones/importaciones `__declspec`, límite exe/DLL; alta | Macros de visibilidad y enlace estático inicial. Después estudiar PRX y sus restricciones de carga. No reutilizar PE como si fuera ELF. |
| `runtime/runtime.h:58`, `runtime/hostrt.h:9`, `runtime/hostrt.c:846`, `runtime/nnro.c:750` | Un puntero guest es un puntero host; bases `0x7100000000`, `0x7200000000`, `0x7300000000`, SDK `0x7500000000`; crítica | Primero comprobar direcciones en hardware. El diagnóstico sondea una página en las bases principal e imports sin `MAP_FIXED`. Si no se permiten, introducir traducción de direcciones en todos los accesos y escapes de punteros. No se ha probado el mapa completo. |
| `runtime/boot.c:244`, `runtime/hostrt.c:13` | Reserva/commit de memoria, guardas, `VirtualQuery`, imagen de `0x7500000` bytes; alta | Páginas Orbis de 16 KiB, mapa explícito, permisos y presupuesto de memoria. El prototipo solo implementa mapas RW pequeños; no reserva el juego ni memoria ejecutable. |
| `runtime/os_threads.c:33`, `:326`, `:414` | Threads, eventos, CV, SRWLOCK, afinidad, fibras y FLS/TLS; crítica | `scePthread*` y eventos Orbis, más adaptación explícita de fibras/contextos y TLS. Dos hilos y un mutex en el prototipo no sustituyen este subsistema entero. |
| `runtime/display.c:399`, `:512`, `runtime/nvn_vk.c:59`, `:245` | Ventanas HWND, WGL, Win32 Vulkan surface, carga de `vulkan-1.dll`; crítica | Separar presentación del traductor NVN. Primer gráfico viable: framebuffer CPU con `sceVideoOut`. Un renderer 3D necesita otra prueba independiente y shaders. |
| `runtime/nnpad_sdl.c:14`, `:15`, `runtime/nnhid.c:447` | SDL3, XInput y descubrimiento Windows; media/alta | `sceUserServiceGetInitialUser`, `scePadOpen` y `scePadReadState`; mapear a `nnpad` y probar desconexión, ejes, botones y varios jugadores. El SDK instalado trae SDL2, no SDL3. Pendiente. |
| `runtime/nnaudio.c:41`, `:42`, `:75` | `waveOut`, buffers WAVEHDR y libopus cargada como DLL; alta | `sceAudioOut`, colas y reloj de consumo, libopus compilada para Orbis. No dar por válido audio silencioso. Pendiente. |
| `runtime/nnmovie_mf.c:76`, `:127`, `:129` | Media Foundation/COM, decodificación VP9/Vorbis; alta | Interfaces de vídeo con otro decodificador y licencias/dependencias revisadas. Para pruebas tempranas se puede omitir explícitamente la cinemática, sin afirmar que el vídeo funciona. |
| `runtime/nnfs.c:91`, `:106`, `:470`, `:569`, `runtime/nnfsdir.c` | Rutas Windows, FindFirstFile, HANDLE, Unicode y transacciones de guardado; alta | `/app0` para datos del programa, `/data` para diagnóstico, backend separado de saves. Lectura binaria a offset de 64 bits disponible en el prototipo; guardados del juego pendientes. |
| `runtime/nnro.c:603`, `runtime/hostrt.c:697` | DLL de cada NRO, resolución de símbolos, BCrypt y minidumps; alta | Registro estático de funciones/tablas primero; `sceKernelLoadStartModule`/`sceKernelDlsym` solo para módulos Orbis compatibles. Log simple ya disponible. |
| `runtime/nnnet.c`, `runtime/net_relay.c`, `scripts/bootstrap.ps1`, `scripts/install.py` | Winsock/WinHTTP, PowerShell, MSVC/SDK, instaladores y descargas Windows; alta | Mantener herramientas de extracción/lifting en el PC. Compilación Orbis separada. Red, online y rollback quedan fuera del primer hito. |
| `tests/` | Pruebas de batalla y comparativas ligadas al exe, ventanas y datos generados | No confundir resultados publicados por upstream con pruebas de esta sesión. Se añadieron pruebas autónomas sin dump, ejecutadas en Windows. |

Los números de línea de esta tabla corresponden a la base; el TSV regenerado es la referencia completa.

## Compatibilidad del código recompilado

El lifter emite C y usa `Ctx`/`V128`, `runtime.h`, `fpexact.h` y `neon.h`. Esa capa permite cambiar
el compilador de destino sin reescribir las instrucciones ARM64 como ensamblador x86.
El diagnóstico compila y ejecuta helpers reales de `runtime.h` con valores sintéticos (aritmética,
lectura/escritura y operaciones exclusivas). **No es una ejecución del código del juego ni una
validación diferencial completa del lifter.**

Persisten el ABI Windows frente a SysV (también `long` y convenciones de llamada), el contexto de
excepciones y `setjmp` (`runtime/nocrt/setjmp.h` declara el formato UCRT), TLS/fibras, símbolos
del SDK y cualquier puntero guest que llegue a `memcpy`, archivos, gráficos o vtables.
El código y las tablas derivados del dump deben generarse localmente y permanecer ignorados.
El pipeline upstream reconoce 13.0.4/13.0.5; **1.0.0 no es una entrada admitida**.
El respaldo 13.0.4 indicado por el usuario sería la entrada para una etapa posterior.

## Qué gráficos están realmente disponibles

El [ejemplo oficial graphics de OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/tree/master/samples/graphics)
usa memoria directa y `sceVideoOut` para presentar un framebuffer escrito por CPU. Esto permite
probar salida de imagen, pero no demuestra un backend NVN ni aceleración 3D.
El SDK v0.5.4 instalado ofrece headers EGL/GLES2/SDL2 y stubs Orbis; eso no prueba por sí mismo
que exista un driver compatible con el renderer Vulkan 1.3 del juego.

Existe una [rama Mesa/RADV para Orbis](https://github.com/orbis-ports/mesa-ps4), con cambios de winsys
y WSI. El [orbis-porting-kit](https://github.com/orbis-ports/orbis-porting-kit) contiene ejemplos y un
loader específico, pero está archivado y remite a un sucesor que no estuvo accesible públicamente
durante esta revisión. Su documentación describe pruebas de terceros; no las hemos reproducido.
No se instaló Mesa ni se comprobó `vkCreateInstance` en la PS4 del usuario. Hace falta fijar una
revisión disponible, construir el stack y probar dispositivo, extensiones, shaders y presentación
antes de seleccionar esta vía. RADV de Linux no se puede suponer utilizable directamente en Orbis.

## Obstáculos encontrados y resueltos en el host

1. El checkout local de OpenOrbis no contiene `crt1.o`, bibliotecas ni ejecutables. Se descargó la
   distribución oficial v0.5.4 en una carpeta independiente y se verificó su SHA-256.
2. No había compilador C/C++ encontrado en PATH ni instancia detectada por `vswhere`. Se usó
   LLVM-MinGW portátil; no se cambió PATH ni se instaló Visual Studio.
3. `fseeko` estaba oculto sin `_POSIX_C_SOURCE`; se habilitaron sus declaraciones POSIX.
4. `create-fself` necesita `OO_PS4_TOOLCHAIN` aunque se use una ruta absoluta; el build lo pasa solo al hijo.
5. `PkgTool.Core` pide .NET 3.0; se ejecutó con el .NET disponible mediante `DOTNET_ROLL_FORWARD=Major`,
   limitado al proceso hijo. No se instaló un runtime antiguo.
6. El primer Content ID tenía 37 caracteres y el empaquetador lo rechazó. Corregido a 36 y comprobado antes de empaquetar.
7. Se comprobó en el [código de PkgTool](https://github.com/OpenOrbis/LibOrbisPkg/blob/master/PkgTool/Program.cs)
   que `pkg_validate` imprime errores sin devolver necesariamente fallo. El script ahora examina
   errores, advertencias y confirmaciones de hashes, además del código de salida, y relee el payload.
8. Los headers musl del SDK definen `MAP_ANON=0x20` (Linux). La llamada directa `sceKernelMmap`
   usa `0x1000` de la [ABI FreeBSD](https://github.com/freebsd/freebsd-src/blob/releng/9.0/sys/sys/mman.h).
   El adaptador usa constantes Orbis explícitas y nunca usa `MAP_FIXED`.

El bootstrap posterior incorpora traducción guest→host y fibras/TLS; sus pruebas sintéticas ya no
dependen de reservar las bases originales en consola. Falta comprobar los escapes de punteros de los
handlers reales con el ExeFS propio y conectar el SDK, excepciones y renderer 3D.
La compatibilidad con firmware 13.52/GoldHEN es
información aportada por el usuario, no una combinación comprobada por este proyecto.
