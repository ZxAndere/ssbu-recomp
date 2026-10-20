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
python -m pip install --target build_ps4/python-deps unicorn==2.1.4 capstone==5.0.9 lz4==4.4.5 zstandard==0.25.0 pycryptodome==4.1.0
$env:PYTHONPATH = (Resolve-Path build_ps4/python-deps).Path
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

## Extracción, lift y enlace privados

La actualización propia de esta sesión produjo main/sdk con los seis SHA-256 de segmentos correctos.
El main coincide con el layout 13.0.5: texto 0x39c8440. Las claves solo se leen desde su ruta original.
El lector selectivo atraviesa el NSZ sólido sin escribir RomFS ni un NSP expandido; admite NCZBLOCK.
Cada salida debe ser una carpeta nueva dentro de build_ps4/, ignorada por Git.

```powershell
$env:SSBU_PROD_KEYS = 'D:\claves-propias\prod.keys'
$env:SSBU_TITLE_KEYS = 'D:\claves-propias\title.keys'
python scripts/ps4/extract.py --update 'D:\respaldo-propio\update.nsz' --out build_ps4/my-exefs
python scripts/ps4/lift.py --image build_ps4/my-exefs/main.elf --out build_ps4/my-full --jobs 2

# Cierre SDK del arranque verificado: mutex, strings, allocator y next_prime.
$bootRoots = Get-Content docs/ps4/sdk-boot-roots.json -Raw | ConvertFrom-Json
$sdkLift = @('scripts/ps4/bootstrap_lift.py', '--image', 'build_ps4/my-exefs/sdk.elf', '--out', 'build_ps4/my-sdk', '--entry-symbol', $bootRoots.entry_symbol, '--base', '0x7200000000', '--cut-imports', 'build_ps4/my-full/generated/imports.c', '--max-functions', '2000')
foreach ($sdkSymbol in $bootRoots.root_symbols) { $sdkLift += @('--root-symbol', $sdkSymbol) }
python @sdkLift

python scripts/ps4/build.py host --llvm-bin $ps4Compiler --game-generated build_ps4/my-full/generated --game-image build_ps4/my-exefs/main.elf --sdk-generated build_ps4/my-sdk/generated --game-sdk-image build_ps4/my-exefs/sdk.elf --game-opt 0 --jobs 2 --build-label full
python scripts/ps4/build.py ps4 --llvm-bin $ps4Compiler --sdk $ps4Sdk --game-generated build_ps4/my-full/generated --game-image build_ps4/my-exefs/main.elf --sdk-generated build_ps4/my-sdk/generated --game-sdk-image build_ps4/my-exefs/sdk.elf --game-opt 0 --jobs 2 --build-label full --package --private-game-package
```

Para la variante acotada: bootstrap_lift.py --initializers --max-functions 4096 y --root para destinos
indirectos observados. No sustituye un lift completo. El manifiesto SHA-256 vincula imagen y todas las
fuentes; cambios/archivos adicionales se rechazan. Un fingerprint adicional comprueba las imágenes cargadas.
--root-symbol resuelve cada nombre contra el ELF de entrada, sin fijar direcciones de otra versión.
Para reanudar esta sesión, reutiliza game-full/generated, game-exefs2 y sdk-core7/generated ya existentes;
no repitas extracción ni lift del main. El mismo --build-label full conserva la caché de los objetos.

Sin --game-entry se elige el export nninitStartup, seguido de DT_INIT_ARRAY y nnMain. El offset ELF cero
es metadata MOD0 del NSO, no una entrada del proceso. main.npdm pide una pila guest de 1 MiB; el bootstrap
usa ese tamaño y una fibra con pila nativa independiente de 16 MiB. main está en 0x7100000000 y SDK en
0x7200000000 mediante traducción de direcciones, sin exigir mapas host a esas direcciones.

Las funciones grandes se dividen en bloques de 256 instrucciones, con transiciones explícitas de PC.
Los helpers conservan registros en Ctx; 1.000 casos comparan estado completo contra C sin dividir.
El presupuesto de un millón cuenta checkpoints: entradas, saltos y puntos periódicos. No es una medición
ni un límite exacto de instrucciones ARM. Imports desconocidos y accesos inválidos paran y se registran.

El build usa objetos con caché de fuentes/headers/flags, concurrencia limitada, response files y cierre
del árbol del compilador si vence el timeout. --game-opt 0 es una prueba de arranque, sin debug generado;
no representa una compilación de rendimiento. El runtime común sigue con -O1 y sin fast-math.
El modo eh necesita integrar los hooks de unwinding y landing pads, todavía pendientes.

--private-game-package incluye main.elf/sdk.elf propios y marca contains_game: true. Esos PKG, fuentes,
ELF y objetos permanecen en build_ps4/*-private*/. No se publican. El PKG público contiene pruebas propias.
boot_probe.py captura la parada controlada y distingue constructores entrados/completados y nnMain;
no convierte una salida controlada en una afirmación de gameplay.

## Ejecución Orbis pendiente

El PKG ejecuta checks, busca /data/ssbu/main.elf y muestra el estado con vídeo/mandos/audio.
Cross solicita un tono; Options libera dispositivos y deja el proceso en espera. Log:
/data/ssbu-ps4-diagnostic.log y stdout. Un PKG privado busca sus ELF en /app0/main.elf y /app0/sdk.elf.

CRT, ABI, pantalla, sonido, mandos y cierre no se han verificado en hardware. La UI indica que
Smash no está ejecutándose. El siguiente trabajo con datos propios se hará primero en PC;
no se solicita probar cada commit. No hay FPS ni compatibilidad 13.52/GoldHEN confirmados.

## Reanudación gráfica incremental

El estado actual está en PS4_PROGRESS.md. No repetir extracción ni lift del main. La ampliación del SDK
usa `sdk-nvn-state2/generated` y conserva `sdk-core7`, `sdk-core8` y sus objetos anteriores.

```powershell
python scripts/ps4/boot_probe.py --executable build_ps4/host-private-full/diagnostic.exe --image build_ps4/game-exefs2/main.elf --out build_ps4/boot-current.json --checkpoints 30000000
```

Para relink rápido del runtime Windows actual, usar clang con
`@build_ps4/host-sdk-nvn-state2/relink.rsp`; reutiliza los objetos originales del main y los nuevos SDK.
Para regenerar solamente el SDK desde su tabla NVN local:

```powershell
python scripts/ps4/nvn_state_roots.py --image build_ps4/game-exefs2/sdk.elf --out build_ps4/new-sdk-roots.json
python scripts/ps4/bootstrap_lift.py --image build_ps4/game-exefs2/sdk.elf --out build_ps4/new-sdk-closure --base 0x7200000000 --roots-file build_ps4/new-sdk-roots.json --cut-imports build_ps4/game-full/generated/imports.c
```

`relink_sdk.py --generated ... --image ... --response ... --out ... --target host|ps4 --llvm-bin ...`
reemplaza solo los objetos SDK de un response file existente, sin borrar los anteriores ni recompilar
el main. Para PS4 añadir `--sdk ...`. Los outputs deben permanecer en build_ps4/.

Los flags de gráficos se añaden solo al runtime: `--host-vulkan-headers` para Windows y
`--ps4-gnm-root/--ps4-gnm-library` para una biblioteca OpenGNM propia compilada con Orbis.
No usar el backend de Windows como evidencia de compatibilidad de GPU PS4.

No cambiar inadvertidamente `--private-game-package` al reutilizar la caché PS4 completa:
`SP_GAME_APP0` forma parte de sus firmas. Los relinks incrementales conservan los flags existentes.

## Shaders reales y caché privada

Para continuar automáticamente las capturas de shaders del arranque en Windows:

```powershell
python scripts/ps4/advance_shader_boot.py --executable build_ps4/host-private-full/diagnostic.exe --image build_ps4/game-exefs2/main.elf --captures build_ps4/game-shader-capture --romfs D:/SmashUltimatePS4-local/romfs-13.0.5 --llvm-bin <LLVM-bin> --glslang <glslang.exe> --cycles 16
```

SSBU_SHADER_CAPTURE y SSBU_SHADER_CACHE se limitan al proceso hijo. Las capturas existentes se reutilizan
solo cuando son idénticas. La conversión conserva cachés por fuente y toolchain; cualquier nuevo error
ajeno a shaders detiene el ciclo. Los archivos derivados no salen de build_ps4/.

`real_gcn_probe.py --translated build_ps4/game-shader-capture/translated --stack <OpenGNM-stack> --llvm-bin <LLVM-bin>`
comprueba offline los contenedores GFX7 del juego. Usa el compilador ya construido por shader_probe.py y
shader_host_probe.py. Rechaza warnings; no verifica ejecución GPU, recursos ni semántica de los shaders.
