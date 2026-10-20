# Compilar el bootstrap PS4 v0.2.0

La variante pública no necesita juego, claves, firmware ni DLC. Incluye programas ARM64 propios
traducidos con el lifter original y componentes de plataforma/runtime, sin funciones de Smash.

## Herramientas verificadas

| Herramienta | Distribución | SHA-256 del archivo descargado |
|---|---|---|
| OpenOrbis v0.5.4 | [toolchain-llvm-18.tar.gz](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/tag/v0.5.4) | 3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526 |
| Clang 23.1.3 / LLVM-MinGW | [20261006 ucrt-x86_64.zip](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006) | 317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a |
| Python | 3.14.3 usado; scripts 3.10+ | — |
| .NET | 6/8 instalados, roll-forward solo para PkgTool hijo | — |
| Unicorn / Capstone | 2.1.4 / 5.0.9, instalados en build_ps4/python-deps | — |

La raíz SDK contiene link.x, include/, lib/crt1.o, libc y bin/windows. Una clonación de fuentes
no incluye esos binarios. Este bootstrap no requiere MSVC, SDL3, Vulkan, WSL ni LLVM 9.

## Pruebas y compilación públicas

```powershell
Set-Location 'C:\SmashUltimatePS4\ssbu-recomp-ps4'
$ps4Compiler = 'C:\SmashUltimatePS4\ps4-deps\llvm-mingw-20261006-ucrt-x86_64\bin'
$ps4Sdk = 'C:\SmashUltimatePS4\ps4-deps\OpenOrbis\PS4Toolchain'

# En otra máquina: dependencias de emulación, sin instalación global.
python -m pip install --target build_ps4/python-deps unicorn==2.1.4 capstone==5.0.9
python scripts/ps4/verify.py --llvm-bin $ps4Compiler --sdk $ps4Sdk
```

verify.py ejecuta Windows, genera/valida/extrae el PKG Orbis, ejecuta unittest/Unicorn, comprueba
el enlace estático con código propio y ejecuta/compila los tests upstream de rollback sin juego.
Logs completos: build_ps4/verification/. Resumen: docs/ps4/. Sin Unicorn se omite la emulación
y el informe lo indica; en esta sesión no hubo omisiones.

Por separado:

```powershell
python scripts/ps4/build.py host --llvm-bin $ps4Compiler
python scripts/ps4/build.py ps4 --llvm-bin $ps4Compiler --sdk $ps4Sdk --package
python -m unittest discover -s tests/ps4 -v
python scripts/ps4/audit.py
```

Salidas: build_ps4/host/diagnostic.exe y build_ps4/ps4/{diagnostic.elf,diagnostic.oelf,eboot.bin,
IV0000-SSBD00001_00-SSBUPLATFORMDIAG.pkg,package-manifest.json,package-validation.log}.
Nombre de aplicación: SSBU Runtime Bootstrap; Title ID SSBD00001; APP_VER 1.01.

C17, -O1 -Wall -Wextra -Werror, sin fast-math. Código generado: solo se desactiva unused-label.
Orbis: x86_64-pc-freebsd12-elf, link.x, crt1.o, libc y stubs kernel/Pad/UserService/AudioOut/VideoOut/SystemService.
El cambio de contexto está en ensamblador SysV. Response files permiten enlazar más de mil objetos sin
superar el límite de argumentos Windows. El empaquetado usa una lista fija; no recorre dumps.

## Lift y enlace privados

Hace falta un main.elf AArch64 propio ya descifrado/convertido. NSZ cifrado no sirve como sustituto.
El input original no se modifica; se copia y genera dentro de build_ps4/, ignorado por Git.

```powershell
python scripts/ps4/lift.py --image 'D:\dump-propio\main.elf' --out build_ps4/local-lift --mode full --jobs 2
python scripts/ps4/build.py host --llvm-bin $ps4Compiler --game-generated build_ps4/local-lift/generated --game-image build_ps4/local-lift/inputs/main.elf --game-entry 0xDIRECCION_GUEST
python scripts/ps4/build.py ps4 --llvm-bin $ps4Compiler --sdk $ps4Sdk --game-generated build_ps4/local-lift/generated --game-image build_ps4/local-lift/inputs/main.elf --game-entry 0xDIRECCION_GUEST
```

0xDIRECCION_GUEST es un marcador: sustituir por una entrada identificada en el dump y presente
entre las funciones recompiladas. No se inventa una entrada de Smash. Sin esa opción se usa la entrada ELF.
El modo eh usa recomp_eh.py y necesita además integrar los hooks de excepciones/landing pads, pendientes.

El manifiesto SHA-256 vincula imagen y fuentes al terminar el lift. El build rechaza inputs, fuentes o
conjuntos de archivos cambiados. La comprobación FNV en ejecución evita cargar después otra imagen;
no es una firma criptográfica. Las fuentes se compilan de nuevo para Orbis, sin reutilizar DLL/COFF Windows.

Cada entrada guest tiene un límite de un millón de instrucciones traducidas. Checkpoints en labels
cortan los bucles infinitos. Imports desconocidos, direcciones inválidas, SVC o funciones ausentes
producen una parada registrada. Todavía faltan handlers y excepciones del runtime completo.

La variante con fuentes locales va a build_ps4/host-private o ps4-private. Crear su PKG exige ambas
opciones --package --private-game-package: incluye el ELF propio y marca contains_game: true.
Esos resultados permanecen locales. Esta entrega publica únicamente la variante sin código del juego.

## Ejecución Orbis pendiente

El PKG ejecuta checks, busca /data/ssbu/main.elf y muestra el estado con vídeo/mandos/audio.
Cross solicita un tono; Options libera dispositivos y deja el proceso en espera. Log:
/data/ssbu-ps4-diagnostic.log y stdout. Un PKG privado busca su ELF en /app0/main.elf.

CRT, ABI, pantalla, sonido, mandos y cierre no se han verificado en hardware. La UI indica que
Smash no está ejecutándose. El siguiente trabajo con datos propios se hará primero en PC;
no se solicita probar cada commit. No hay FPS ni compatibilidad 13.52/GoldHEN confirmados.
