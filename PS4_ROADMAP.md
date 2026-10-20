# Hoja de ruta PS4 después de v0.2.0

El objetivo final sigue siendo un juego nativo con partidas funcionales.

| Etapa | Estado | Criterio pendiente |
|---|---|---|
| Auditoría/aislamiento | Hecho; checkout original intacto | Actualizar al integrar más módulos |
| Servicios de plataforma | Memoria, permisos, archivos, sync, TLS, módulos y fibras implementados/compilados | ABI físico y adopción por handlers guest |
| Dispositivos | DualShock 4, PCM AudioOut y framebuffer CPU implementados | Verificación física, motion, puentes con el juego |
| Recompilación/enlace | Lifter original a C a x86-64, direcciones traducidas y enlace estático probados con código propio | ExeFS propio de Smash y símbolos reales |
| Carga/entrada guest | ELF, relocaciones y ejecución acotada implementados | Imports/datos SDK e inicialización del juego |
| Runtime completo | Soporte del lifter/texturas parcialmente integrado | Unwinder C++, heaps, handlers SDK, contextos múltiples y NRO |
| GPU 3D | Framebuffer inicial e investigación Vulkan Orbis/Piglet | Stack disponible con revisión fija y prueba API/shaders/presentación antes de NVN |
| Menús/combate | Pendiente | Arranque real y una partida verificada |
| Rendimiento | Colas/pilas acotadas, redraw de UI por cambios y waits con límite | Memoria, CPU/GPU, frame time, caché y latencia durante una partida |

El siguiente paso local es una ruta al ExeFS descifrado o a claves propias ya presentes en el PC.
`lift.py` y el enlace privado están preparados. La extracción/actualización y data.arc permanecen locales.
El bootstrap concentra los futuros checks físicos de servicios, fibras, pantalla, mandos, AudioOut y cierre.
No se solicita probar cada commit. La pantalla identifica que todavía no hay gameplay.
Los objetivos 30/60 FPS se evaluarán después de una partida funcional.
