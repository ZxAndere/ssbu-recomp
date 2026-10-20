# Hoja de ruta PS4

El primer hito es un programa de diagnóstico sin datos de Smash. No hay port jugable ni FPS medidos.
Las etapas se cierran con evidencia de la columna de aceptación, no solo con código escrito.

| Etapa | Implementación | Criterio para avanzar | Estado |
|---|---|---|---|
| 0. Base | Auditoría, copia independiente, exclusión de NSZ/PKG/SELF y datos privados | Evidencia por archivo/línea y proyecto Windows intacto | Hecho |
| 1. Plataforma mínima | API C, Windows/OpenOrbis: log, reloj, memoria RW, dos hilos, mutex, lectura a offset de 64 bits | Pruebas PC, ELF x86-64, FSELF y PKG validado/releído | Hecho en PC; ejecución Orbis pendiente |
| 2. Consola | Ejecutar diagnóstico, recuperar `/data/ssbu-ps4-diagnostic.log`, comprobar cierre desde menú PS | 52 comprobaciones sin fallo y resultados de los sondeos guest documentados en PS4 real | Pendiente |
| 3. Entrada y salida de imagen | `scePad` + usuario, framebuffer CPU `sceVideoOut`, sincronización y liberación de memoria directa | Imagen estable, botones/ejes/desconexión registrados; sin bloqueos al cerrar | Pendiente |
| 4. Direcciones y módulos | Decidir mapa directo o traducción guest→host; registro estático para un fragmento recompilado del dump propio | Cargar datos y ejecutar una función pequeña con resultado comprobado; sin DLL Windows | Pendiente |
| 5. Runtime sin renderer | Abstracción adoptada por hostrt, os_threads, nnfs; TLS, fibras, excepciones y tablas SDK | Pruebas de carga, sincronización, guardas y archivos del runtime; sin saves originales | Pendiente |
| 6. Audio/vídeo | AudioOut, Opus Orbis, decodificador de vídeo separado | PCM conocido, reloj, liberación de buffers y consumo medidos; vídeo comprobado por separado | Pendiente |
| 7. Renderizador 3D | Evaluar Mesa/RADV Orbis con revisiones fijadas u otra vía viable; eliminar superficie Win32 | Triángulo y texturas en hardware; consultas de API/extensiones y shaders; después integrar NVN | Pendiente |
| 8. Arranque y juego | Inicialización real, menús y combate con update 13.0.4 propio | Log, imagen y controles verificados en PS4; registrar fallos en cada fase | Pendiente |
| 9. Rendimiento | Presupuesto de memoria, CPU/GPU, caché de shaders, recursos y latencia | Mediciones reproducibles por escena y duración, más percentiles de frame time | Pendiente |

No se rebaja la semántica de sincronización a stubs que siempre devuelvan éxito para lograr un arranque aparente.
Las bases guest pequeñas del diagnóstico solo prueban la posibilidad de mapear páginas; no garantizan espacio para
la imagen, NRO, heaps, stacks, datos del juego y buffers gráficos simultáneamente.

El objetivo de 30 FPS y la evaluación posterior de 60 FPS quedan condicionados a un combate funcional.
Antes se medirán memoria residente/pico, distribución de tiempos por frame, tiempo de compilación de shaders y
latencia de entrada. No hay estimaciones ni resultados de rendimiento de Smash en PS4 en esta entrega.

Próxima acción concreta: instalar el PKG de diagnóstico en la consola del usuario, ejecutarlo una vez,
cerrarlo desde el menú PS y recuperar su log. La sesión actual no dispone de conexión a esa consola.
