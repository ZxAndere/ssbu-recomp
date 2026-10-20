# Prueba preparada para PS4 Fat / 13.52 GoldHEN

La compilación privada de `build_ps4/ps4-private-full/` intenta arrancar el código real del juego.
No se requiere una prueba de consola para implementar el import de foco que sigue pendiente en PC.
Esta prueba concentra las incertidumbres de hardware cuando se decida validar la compilación.

El paquete privado incluye sus main/sdk en `/app0/` y el main nativo completo. Es local y no se publica.
Usa el mismo Title ID SSBD00001 que el bootstrap público. El log se anexa a
`/data/ssbu-ps4-diagnostic.log`; los resultados anteriores permanecen en el archivo.

## Resultado esperado de esta revisión

1. Completan las pruebas propias: 52 plataforma, 49 servicios, 47 medios, 21 fibras y 307 runtime.
2. Se mapean main y sdk; el log identifica 67 funciones SDK enlazadas.
3. Aparecen `GAME PHASE: nninitStartup` y `static constructors (306)`.
4. Cada constructor termina con `GAME CONSTRUCTOR DONE`; después aparece `GAME PHASE: nnMain`.
5. Se inicializa el ciclo de vida propio y se registran los hints de rendimiento, sin cambiar frecuencias.
6. Parada controlada esperada: SetFocusHandlingMode, modo 2, LR 0x7103741fb0.
7. La UI de pruebas aparece; se comprueban mando, tono con Cross, flips y cierre con Options/menú PS.

Esto no verifica gráficos, audio ni partidas de Smash. Solo contrasta el arranque nativo y los backends.

## Cómo identificar una diferencia de hardware

| Última evidencia | Incertidumbre que resuelve |
|---|---|
| No aparece el primer log | Carga CRT/FSELF, librerías importadas o instalación/carga del paquete |
| Fallan fibras antes del juego | ABI SysV, protección de pila o restauración de registros/FP |
| Constructor 65: native virtual reservation failed, tamaño 0xf80000000 | Si Orbis admite la reserva PROT_NONE de 62 GiB usada por el juego |
| Fallo de commit/permisos | Semántica mmap/mprotect sobre páginas Orbis de 16 KiB |
| Menos constructores que en PC | Comparar la dirección y el primer GUEST STOP con el informe PC; conservar el log completo |
| Llega a nnMain y al mismo import pendiente | Equivalencia de este punto de arranque; no implica que el runtime completo funcione |
| Arranque equivalente y fallo de un dispositivo | Resultado y código de Pad/UserService, AudioOut o VideoOut registrado por separado |

El PC ya ejecutó reservas grandes y permisos con un modelo de páginas de 16 KiB. No valida la implementación
kernel de PS4 ni la disponibilidad física de servicios bajo esa versión de GoldHEN.

Para la siguiente adaptación de foco, OpenOrbis 0.5.4 solo declara GetStatus sin firma de parámetros.
La [interfaz de shadPS4](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/system/systemservice.h)
aporta campos observados, pero no es una prueba física. Antes de basar suspensión/notificaciones del juego
en ella, hay que contrastar ABI, tamaño y transiciones Home/volver; no se han inventado estados de foco.
