# Hoja de ruta PS4 después de v0.2.0

El objetivo final sigue siendo un juego nativo con partidas funcionales.

| Etapa | Estado | Criterio pendiente |
|---|---|---|
| Auditoría/aislamiento | Hecho; checkout original intacto | Actualizar al integrar más módulos |
| Servicios de plataforma | Memoria, permisos, archivos, sync, TLS, módulos y fibras implementados/compilados | ABI físico y adopción por handlers guest |
| Dispositivos | DualShock 4, PCM AudioOut y framebuffer CPU implementados | Verificación física, motion, puentes con el juego |
| Recompilación/enlace | ExeFS verificado, main completo generado y arranque real parcial en PC | Completar SDK, unwinder y módulos dinámicos |
| Carga/entrada guest | ELF multi-módulo, SDK RTTI/datos, heap, páginas, mutex/TLS; 306 constructores completados y nnMain entrado en PC | Foco/notificaciones oe e inicialización restante |
| Runtime completo | Soporte del lifter/texturas parcialmente integrado | Unwinder C++, más handlers SDK, contextos múltiples y NRO |
| GPU 3D | Framebuffer inicial e investigación Vulkan Orbis/Piglet | Stack disponible con revisión fija y prueba API/shaders/presentación antes de NVN |
| Menús/combate | Pendiente | Arranque real y una partida verificada |
| Rendimiento | Colas/pilas acotadas, redraw de UI por cambios y waits con límite | Memoria, CPU/GPU, frame time, caché y latencia durante una partida |

Las claves y el ExeFS ya están disponibles localmente. Continúa la inicialización real en PC;
la extracción, las fuentes generadas, los binarios derivados y data.arc permanecen locales.
El bootstrap concentra los futuros checks físicos de servicios, fibras, pantalla, mandos, AudioOut y cierre.
No se solicita probar cada commit. La pantalla identifica que todavía no hay gameplay.
Los objetivos 30/60 FPS se evaluarán después de una partida funcional.
