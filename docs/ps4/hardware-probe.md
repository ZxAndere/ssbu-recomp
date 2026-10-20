# Prueba física preparada — no ejecutada

El trabajo de software sigue en PC. No se requiere ahora una prueba intermedia en consola.
Los PKG privados anteriores permanecen locales; su contenido no equivale a un juego jugable.

## Nueva ruta gráfica experimental

El ELF local `build_ps4/graphics-probe/game-gnm.elf` enlaza las 112.876 funciones del main y 198 funciones
SDK con la nueva ruta OpenGNM. Reutiliza los 1.162 objetos del main. El log del ejecutable conserva la
ruta `/data/ssbu-ps4-diagnostic.log` por continuidad, aunque intenta ejecutar el código real del juego.

Antes de continuar la inicialización gráfica, la ruta OpenGNM debe comprobar:

1. Asignación y mapa de memoria directa para comandos con acceso GPU.
2. Emisión de estado inicial y un draw de cero vértices, que no produce una imagen.
3. Envío de `CACHE_FLUSH_AND_INV_TS_EVENT` con escritura EOP de un nonce de 64 bits.
4. Observación CPU del mismo nonce antes de dos segundos.

Solo la línea `GAME GRAPHICS: native GNM startup EOP completed` verifica esa secuencia concreta.
Un retorno del submit por sí solo no confirma finalización GPU. En timeout se conserva la memoria
que pueda seguir en vuelo. Esta prueba no comprueba shaders, texturas, presentación ni gráficos de Smash.

La ruta GNM todavía no expone todos los límites ni implementa samplers/pipelines del juego. Si el EOP
funciona, una parada posterior en una consulta NVN pendiente es un bloqueo de software conocido;
no debe interpretarse como un error de instalación ni ocultarse devolviendo capacidades inventadas.

## Otras incertidumbres físicas

| Última evidencia | Qué comprobaría una prueba física |
|---|---|
| Ningún primer log | Instalación, CRT/FSELF e imports Orbis |
| Fallo en fibras | ABI SysV, pila protegida y restauración de registros/FP |
| Fallo al reservar 62 GiB | Disponibilidad del espacio virtual PROT_NONE requerido por el juego |
| Fallo de permisos/commit | mmap/mprotect sobre páginas Orbis de 16 KiB |
| Menos de 306 constructores | Dirección y primer GUEST STOP frente al log PC |
| Submit GNM rechazado | Código real de GnmDriver y estado de servicios bajo el firmware del usuario |
| Timeout EOP | Orden de paquetes, memoria/cachés, inicialización del contexto o estado de cola GPU |
| EOP correcto | Cola nativa y coherencia de esa escritura, todavía sin prueba de renderer |
| Fallo de foco/mandos/audio/vídeo | ABI o servicio específico, registrado por separado |

Los adaptadores de foco, Pad, AudioOut y VideoOut siguen compilados. Su presencia no verifica HID,
audio o presentación de escenas del juego. No hay mediciones de FPS ni compatibilidad GoldHEN confirmada.
