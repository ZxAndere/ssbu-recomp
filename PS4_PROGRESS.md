# Progreso PS4 — inicialización NVN y compilador GCN

Continuación de `ac2ee45` en `ps4/platform-diagnostic`. Se conservan las 112.876 funciones del main,
los 306 constructores y los objetos anteriores. **Todavía no hay una imagen real de Smash, menú ni partida.**

## Arranque medido en PC

| Etapa | Evidencia |
|---|---|
| Main nativo | 112.876 funciones; 1.162 objetos reutilizados, cero recompilaciones del main |
| SDK | 198 funciones locales: las 79 de arranque más 119 funciones de estado NVN y SamplerBuilder |
| Arranque | nninitStartup retorna; completan los 306 constructores; se entra en nnMain |
| Servicios | Foco/notificaciones nativos, idioma es-419 del PC, workers cooperativos, mutex y eventos |
| RomFS | Se monta el directorio del respaldo propio; todavía cero aperturas/lecturas de recursos del juego |
| Gráficos PC | Se crea un dispositivo Vulkan real en una NVIDIA GeForce GTX 960, API 1.4 |
| NVN | Se enlazan nombres, se consultan límites del backend y políticas explícitas del traductor |
| Memoria gráfica | Pools nativos para Shader, Descriptors, Uncached y comandos; flush/invalidate con límites |
| Descriptores | Tablas de 8.192 texturas y 2.048 samplers; un sampler Vulkan real registrado en el índice 32 |
| Colas/sync | Dos colas lógicas sobre la cola Vulkan real; fences nativos y almacenamiento del recorder |
| Parada actual | `nvnTextureBuilderSetDefaults`, LR `0x71037e0cd4` |

La preparación privada de RomFS finalizó: 15 archivos, 17.600.938.296 bytes, con manifiesto SHA-256
por archivo. `data.arc` ocupa 17.600.880.952 bytes. Esto comprueba extracción e integridad local;
**no comprueba que nnMain ya cargue recursos**. Los respaldos originales no se modifican.

El presupuesto ampliable de 30.000.000 checkpoints permite superar la inicialización larga que agotaba
el millón original. El contador mide checkpoints, no instrucciones ni rendimiento. Todos los imports,
comandos y capacidades sin implementación detienen el arranque con un error explícito.

## Implementación gráfica real y límites

- Los pools NVN ordinarios crean VkBuffer y memoria nativa mapeada. No se devuelven direcciones CPU
  como supuestas direcciones físicas de GPU: el espacio virtual del guest necesita resolución del traductor.
- Las transferencias CPU esperan el backend antes de acceder a su memoria. Las tablas mantienen descriptores
  virtuales propios de 32 bytes; no se envían descriptores Tegra a GCN ni se anuncian como VkDescriptorSet.
- Las funciones puras de estados NVN y SamplerBuilder ejecutan el código recompilado del SDK propio.
  Se preservan sus formatos exactos; esto todavía no los traduce a pipelines GCN/Vulkan.
- El sampler Vulkan admite los filtros y wraps implementados, comparación y borde negro transparente.
  Modos de borde, anisotropía y reducción aún no implementados se rechazan antes de crear el objeto.
- La compresión fast-clear opcional se rechaza devolviendo falso. No se simula una inscripción exitosa.
- Se mantienen los adaptadores DualShock 4, AudioOut y VideoOut. Todavía no están conectados al HID/audio
  del juego ni constituyen un renderer de Smash.

La prueba propia `native_graphics_probe.py` comprueba fences, creación de sampler, rechazo de un modo
no admitido y una **copia real de GPU de 65.536 bytes**, con barrera de memoria y lectura exacta del patrón.
No dibuja imágenes del juego. No hay capa Khronos de validación instalada en este PC; la comprobación con
esa capa queda pendiente y no se declara satisfecha.

## SPIR-V a GCN

Se construyeron los 478 fuentes seleccionados del compilador externo tanto para Windows como para Orbis.
Se corrigieron en copias de compilación los generadores faltantes, ABI de pthreads/condición Windows,
BLAKE3 portable, capacidades SPIR-V y selección de subgrupos. El shim externo de `vk_pipeline.h` era
un no-op: provocaba wave32 en grupos compute pequeños, inválido para GFX7. Ahora respeta wave64.

Tres shaders originales propios pasan en Windows: vertex, fragment y compute con escritura SSBO.
Cada uno genera 44 bytes de código GCN dentro de un contenedor validado, es determinista y cambia sus
instrucciones al variar una constante. El compute conserva un slot de tabla de recursos y grupo 1x1x1.
Los tres se enlazan también en un ELF Orbis. **Ni estos shaders ni shaders del juego se ejecutaron en GCN.**
La prueba no demuestra todavía la ABI de recursos, cálculos ni salida visual en PS4.

## Shaders reales del juego

La captura privada del primer SetShaders permitió usar el traductor Maxwell portátil existente.
El ciclo automático de captura, conversión y nuevo arranque produjo **15 shaders reales en SPIR-V**
y creó sus 15 módulos Vulkan. La caché comprueba un fingerprint de control, código y constantes;
no identifica un shader solo por una dirección de memoria reutilizable.

Los 15 SPIR-V también se compilaron con el backend GFX7 y pasaron la validación estructural de sus
contenedores GCN, sin advertencias del compilador. Se habilitó la capacidad DemoteToHelperInvocation
que NIR/ACO ya implementan y que el wrapper omitía; en Vulkan se habilita solo si el dispositivo
la soporta. Esto **no demuestra la corrección semántica, ABI de recursos ni ejecución de esos shaders
en PS4**. Los binarios y textos generados quedan exclusivamente en build_ps4/.

El recorder conserva snapshots de estados y asociaciones de pools; las listas solo aplican esos
estados al contexto lógico de cola. Draw, dispatch, clears y copias de comandos sin executor siguen
parando explícitamente. El sort concreto de uint32 con std::less usa qsort nativo con rango comprobado.

## Backend PS4

Se compilaron 27 fuentes OpenGNM y 19 de vulkan-ps4, conservando sus fuentes externas originales.
La revisión local de vulkan-ps4 expone Vulkan 1.0 y tiene entry points sin implementación y sincronización
que no espera la jubilación real de GPU. No basta como reemplazo del renderer upstream Vulkan 1.3.

Se integra opcionalmente una ruta directa OpenGNM (`--ps4-gnm-root`, `--ps4-gnm-library`). Inicializa memoria
directa, emite comandos nativos y exige un EOP escrito realmente por la GPU. Un timeout conserva la memoria
potencialmente en vuelo. Pools y fences tienen rutas Orbis compiladas; los samplers y el renderer GCN siguen
pendientes. No se copian los límites de la GPU del PC a PS4.

El ejecutable completo con OpenGNM y las 198 funciones SDK se enlazó reutilizando los 1.162 objetos main.
No se ejecutó en PS4. Los PKG privados anteriores se conservan; no se publican archivos derivados del juego.
Esta revisión no se presenta como un PKG jugable ni como una primera imagen de Smash.

## Verificación y continuación

- 517 comprobaciones C: 52 plataforma, 49 servicios, 47 medios, 21 fibras y 348 runtime.
- 34 pruebas Python, sin omisiones, incluidos 1.000 casos contra Unicorn ARM64.
- Enlace estático host/Orbis, ELF/FSELF y empaquetado de fixtures propias, hashes y payload releído.
- Prueba upstream de rollback: 211 rollbacks y 512 frames resimulados; no son partidas verificadas de Smash.
- Pruebas originales del compilador y transferencia Vulkan descritas arriba.

Evidencia resumida: [arranque](docs/ps4/private-boot-summary.json),
[gráficos](docs/ps4/graphics-progress.json), [regresiones](docs/ps4/verification.json).
Los logs completos, binarios, recursos y código recompilado permanecen locales.

El trabajo de software inmediato es implementar TextureBuilder, el layout de memoria guest, las imágenes
nativas, descriptores, pipelines y ejecución/presentación de comandos; después seguir hasta abrir data.arc. Siguen pendientes excepciones/unwinder, módulos NRO, servicios que aparezcan y la integración
HID/audio. La falta de estos componentes no se presenta como una obligación de probar hardware.

La primera incertidumbre específica de hardware en la nueva ruta PS4 es el EOP de OpenGNM, además de CRT,
reservas de 62 GiB y permisos Orbis. No se solicita probar cada cambio. No hay mediciones de FPS ni
compatibilidad 13.52/GoldHEN confirmada.

Fuentes externas: [OpenGNM](https://github.com/PS4-OpenGNM/opengnm),
[PSBC](https://github.com/PS4-OpenGNM/opengnm-psbc),
[vulkan-ps4](https://github.com/PS4-OpenGNM/vulkan-ps4),
[OpenOrbis](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain).
