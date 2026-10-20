# Compilar el diagnóstico PS4

Este procedimiento no necesita juego, firmware, claves ni DLC. No compila Smash Ultimate.
Python 3.10+ y un clang con `ld.lld` son suficientes para el código C. Para empaquetar hace falta
la distribución binaria de OpenOrbis y un runtime .NET compatible con `PkgTool.Core`.

## Entorno usado

| Componente | Versión / origen | SHA-256 del archivo descargado |
|---|---|---|
| OpenOrbis | [v0.5.4, toolchain-llvm-18.tar.gz](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/tag/v0.5.4) | `3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526` |
| LLVM-MinGW portátil | [20261006, ucrt-x86_64.zip](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006), clang 23.1.3 | `317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a` |
| Python | 3.14.3, ya instalado | — |
| .NET | runtime 6.0.36 y 8.0.28 detectados; ejecución con roll-forward al major compatible | — |

Descargar de esas páginas oficiales, comprobar `Get-FileHash <archivo> -Algorithm SHA256` y extraer
en una carpeta propia. La raíz del SDK debe contener `link.x`, `include/`, `lib/crt1.o` y `bin/windows/`.
Una clonación del repo OpenOrbis sin sus bibliotecas **no es un SDK instalado**.
No se requiere instalar WSL. No se requieren SDL3, Vulkan, MSVC ni LLVM 9 para este diagnóstico;
el juego Windows upstream conserva sus requisitos originales.

## Comandos reproducibles en PowerShell

En esta máquina se creó la rama `ps4/platform-diagnostic` en la copia
`C:\SmashUltimatePS4\ssbu-recomp-ps4`; el directorio original sigue en `main`.

```powershell
Set-Location 'C:\SmashUltimatePS4\ssbu-recomp-ps4'
$ps4Compiler = 'C:\SmashUltimatePS4\ps4-deps\llvm-mingw-20261006-ucrt-x86_64\bin'
$ps4Sdk = 'C:\SmashUltimatePS4\ps4-deps\OpenOrbis\PS4Toolchain'

python -m unittest discover -s tests/ps4 -v
python scripts/ps4/build.py host --llvm-bin $ps4Compiler
python scripts/ps4/build.py ps4 --llvm-bin $ps4Compiler --sdk $ps4Sdk --check
python scripts/ps4/build.py ps4 --llvm-bin $ps4Compiler --sdk $ps4Sdk --package
python scripts/ps4/audit.py
```

Para otra máquina cambiar las rutas o usar `PS4_LLVM_BIN` y `OO_PS4_TOOLCHAIN`.
`--check` comprueba archivos/herramientas sin compilar; no sustituye el build real.
El empaquetador recibe `DOTNET_ROLL_FORWARD=Major` y el conversor recibe `OO_PS4_TOOLCHAIN`
solo en sus procesos hijos. Si no hay .NET, instalar un runtime desde
[Microsoft](https://dotnet.microsoft.com/download/dotnet); no se instala automáticamente.

Salidas ignoradas por Git:

```text
build_ps4/host/diagnostic.exe
build_ps4/ps4/diagnostic.elf
build_ps4/ps4/diagnostic.oelf
build_ps4/ps4/eboot.bin
build_ps4/ps4/IV0000-SSBD00001_00-SSBUPLATFORMDIAG.pkg
build_ps4/ps4/package-manifest.json
build_ps4/ps4/package-validation.log
```

La compilación utiliza C17, `-O1 -Wall -Wextra -Werror`, sin fast-math. El destino es
`x86_64-pc-freebsd12-elf`, PIE enlazado con `link.x`, `crt1.o`, libc y libkernel.
Se comprueba ELF64 little-endian, máquina x86-64 y tipo ET_DYN antes de convertir.
OpenOrbis crea el FSELF; LibOrbisPkg crea y valida el PKG. Se rechazan mensajes ERROR/WARN aunque
la utilidad termine con código cero. Después se extrae el PKG a un temporal propio y se comparan
sus archivos internos con los materiales originales. Las entradas externas SFO/icono las valida PkgTool.

El package usa una lista fija de archivos: ejecutable del diagnóstico, fixture propia, avisos/licencias,
SFO y materiales genéricos del ejemplo OpenOrbis. Nunca recorre un dump para empaquetarlo.
Se usa el icono genérico de OpenOrbis; no contiene arte de Smash. El manifiesto registra los hashes
de cada archivo y del PKG. Las rutas/órdenes son reproducibles; no se promete un PKG idéntico byte a byte
entre compilaciones, por metadatos, compilador y elementos del empaquetado.

## Prueba en consola, todavía pendiente

Title ID: `SSBD00001`. Nombre: `SSBU Platform Diagnostic`.
Instalar el PKG por el mecanismo homebrew que ya utilice el usuario. Esta sesión no ha enviado nada
a la consola ni ha modificado su configuración.

La aplicación **no inicializa vídeo ni controles**: puede mantener la pantalla negra/de carga.
Intenta anexar el log a `/data/ssbu-ps4-diagnostic.log`; si falla, sigue con stdout. Ejecuta las
comprobaciones de plataforma y dos sondeos de direcciones guest, cierra el log y queda en espera
con `sceKernelUsleep`. Cerrar desde el menú PS. Este estado de espera evita afirmar que la ruta de
retorno del CRT se haya validado. La salida limpia y la compatibilidad 13.52/GoldHEN requieren prueba real.

Log esperado si los tests básicos pasan:

```text
SSBU PS4 platform diagnostic; PS4 OpenOrbis x86-64 (experimental); no game content
RESULT: 52 checks, 0 failures
GUEST ADDRESS PROBE 0x7100000000: status ... (0 is exact)
GUEST ADDRESS PROBE 0x7300000000: status ... (0 is exact)
Diagnostic finished, result 0. Close the title from the PS menu.
```

Un valor distinto de cero en un sondeo no es una ejecución exitosa del mapa guest: indica que
habrá que investigar permisos/rangos y probablemente implementar traducción de direcciones.
No se debe ejecutar el instalador Windows upstream para compilar este diagnóstico; ese instalador
prepara el juego de PC y puede actualizar el checkout desde GitHub.
