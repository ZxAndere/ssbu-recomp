# Progreso verificable PS4

Fecha de la sesión: 19 de octubre de 2026. Base upstream: `80c9012`.
Rama: `ps4/platform-diagnostic`. Copia: `C:\SmashUltimatePS4\ssbu-recomp-ps4`.
El checkout original `C:\SmashUltimatePS4\ssbu-recomp` permanece en `main` y sin cambios de archivos.

## Resultado

| Nivel de evidencia | Resultado de esta sesión |
|---|---|
| Código escrito | Abstracción C de plataforma con backends Windows y PS4; diagnóstico autónomo y scripts de auditoría/build |
| Compilación Windows | Correcta, clang 23.1.3, warnings tratados como errores |
| Pruebas ejecutadas en Windows | **52 comprobaciones, 0 fallos**, más **5 pruebas Python superadas** |
| Pruebas negativas | Fixture inexistente devuelve código 1; checkout OpenOrbis sin binarios rechazado con error explícito; ELF ARM64/COFF/truncado y validaciones incompletas o fallidas rechazados |
| Compilación PS4 | ELF64 x86-64 PIE enlazado y convertido a OELF/FSELF (`eboot.bin`) por OpenOrbis v0.5.4 |
| PKG | Construido, validaciones de hashes/firmas aprobadas y payload extraído/comparado con los archivos de origen |
| Pruebas en PS4 real | **Ninguna**; no hay conexión ni log de la consola en esta sesión |
| Smash en PS4 | **No ejecutado**; el diagnóstico no contiene ni carga el juego |
| Rendimiento del juego | Sin mediciones de FPS, CPU, GPU, memoria de juego ni latencia |

## Qué se probó

El diagnóstico usa la misma interfaz para reloj, memoria RW anónima, hilos, mutex, lectura binaria
con offset de 64 bits y registros. Comprueba límites y overflow de tamaño, cero inicial,
alineación de páginas, escritura en los extremos, rechazo de un mapa ocupado sin destruirlo,
liberación, dos hilos que incrementan un contador 20 000 veces con mutex, lectura parcial/EOF,
offset superior a 4 GiB, errores de argumentos y monotonía del reloj.
También incluye helpers reales de `runtime.h` con registros y memoria sintéticos.
Se han ejecutado esas comprobaciones sobre **Windows**, no sobre Orbis.

Las pruebas Python verifican que una carpeta de fuentes no se confunda con un SDK completo,
el formato y la arquitectura ELF, y que la salida de `pkg_validate` se compruebe aunque el
empaquetador devuelva código cero después de imprimir un error.

## Artefactos

Paquete: `build_ps4/ps4/IV0000-SSBD00001_00-SSBUPLATFORMDIAG.pkg`.
Tamaño del build verificado: **6 619 136 bytes**.
SHA-256: `39d44dd4b11fdcefeb8f62de5ba04883feb591ba934b9b642eedc8acf36a2056`.
Title ID: `SSBD00001`; no es el identificador de Smash.
El [manifiesto](docs/ps4/package-manifest.json) registra también cada archivo empaquetado.

El PKG está compuesto por el diagnóstico, una fixture de texto propia, licencias y los materiales
homebrew genéricos del ejemplo OpenOrbis. Se conservan los binarios fuera del árbol versionado;
la publicación de código no incluye dumps, claves, NSZ, datos generados del juego, firmware ni saves.

## Archivos de esta entrega

- `runtime/platform/{platform.h,common.c,windows.c,ps4.c}`: frontera de plataforma nueva y optativa.
- `platform/ps4/{diagnostic.c,main.c,fixture.txt,NOTICE.txt,licenses/musl.txt}`: aplicación de prueba.
- `scripts/ps4/{build.py,audit.py}`: compilación, empaquetado, comprobación y auditoría.
- `tests/ps4/test_tools.py`: comprobaciones de herramientas sin dependencias del juego.
- `PS4_AUDIT.md`, `PS4_ROADMAP.md`, `PS4_PROGRESS.md`, `PS4_BUILD.md`, `docs/ps4/`: evidencia y procedimiento.
- `.gitignore`: exclusiones adicionales para NSZ/XCZ, SDK y salidas Orbis.
- `README.md`: aviso visible del estado real de la rama PS4.

No se conectó esta API al arranque Windows completo ni se sustituyó el runtime por stubs.
El código nuevo usa `runtime.h` para comprobar una parte real del soporte del lifter, pero todavía
no traduce ni ejecuta funciones provenientes del respaldo del usuario.

## Fallos resueltos y pendientes

Se resolvieron la ausencia del SDK binario/compilador, la declaración POSIX de `fseeko`, la variable
necesaria para `create-fself`, la selección de .NET para PkgTool y el Content ID de longitud incorrecta.
Detalles y alternativas están en [la auditoría](PS4_AUDIT.md).

Permanecen pendientes la ejecución Orbis y su cierre desde el menú PS, controles, framebuffer,
audio/vídeo, fibras/TLS, módulos, mapa guest completo y renderer 3D. El programa termina las pruebas,
cierra el log y queda esperando con bajo consumo; no se ha validado un retorno por el CRT en consola.
El log de hardware debe registrar los resultados de las dos reservas guest de una página para
decidir si se conserva el mapa directo o se introduce traducción de direcciones.

**Siguiente paso concreto:** instalar este diagnóstico en la PS4, abrirlo, cerrarlo desde el menú PS y
recuperar `/data/ssbu-ps4-diagnostic.log`. Si no aparece un log, comprobar primero la carga del PKG,
el CRT y los permisos de `/data`; todavía no corresponde diagnosticar el código del juego.
