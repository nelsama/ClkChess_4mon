# Changelog

## v1.0.0 - Reloj de Ajedrez

### Cambios completos respecto a la calculadora original

- **Nuevo**: Reloj de ajedrez para dos jugadores
- **Nuevo**: Display simultáneo de ambos tiempos (formato MMSSMMSS)
- **Nuevo**: Botones P1 (tecla 4) y P2 (tecla 8) para alternar turnos
- **Nuevo**: Botón de pausa/reanudar (tecla 15)
- **Nuevo**: Botón de reinicio (tecla 12)
- **Nuevo**: Modo de configuración de tiempo (1-99 minutos)
- **Nuevo**: Detección de tiempo agotado con mensaje en display
- **Nuevo**: Registro de movimientos por UART
- **Eliminado**: Dependencia de MSBasic (punto flotante)
- **Eliminado**: Código de calculadora (operaciones, conversiones float)
- **Simplificado**: Makefile sin objetivos de MSBasic
- **Simplificado**: Binario mucho más pequeño (~2KB vs ~13KB)
