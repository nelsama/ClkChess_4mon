# Manual de Usuario — Reloj de Ajedrez 6502

## Índice

1. [Introducción](#1-introducción)
2. [Instalación](#2-instalación)
3. [El Teclado TM1638](#3-el-teclado-tm1638)
4. [El Display](#4-el-display)
5. [Cómo Jugar](#5-cómo-jugar)
6. [Configuración de Tiempo](#6-configuración-de-tiempo)
7. [Sonidos](#7-sonidos)
8. [Registro UART](#8-registro-uart)
9. [Solución de Problemas](#9-solución-de-problemas)
10. [Referencia Técnica](#10-referencia-técnica)

---

## 1. Introducción

El **Reloj de Ajedrez 6502** es un reloj digital para dos jugadores, diseñado para el **Monitor 6502** en la **Tang Nano 9K**. Utiliza:

- **Módulo TM1638**: display de 8 dígitos y 16 botones para la interfaz de usuario
- **Chip SID 6581**: sonido polifónico (3 voces) para efectos auditivos
- **ROM API del Monitor 6502**: comunicación UART y temporización

### Tiempos soportados

- Configurable de **1 a 99 minutos** por jugador
- Por defecto: **5 minutos** (ajedrez blitz)
- Sin límite de movimientos

---

## 2. Instalación

### Requisitos

- Tang Nano 9K con Monitor 6502 v2.2.0+
- Módulo TM1638 conectado al puerto 0xC000
- Chip SID 6581/8580 (o implementación FPGA) en $D400
- Terminal UART (opcional, para ver registro de movimientos)

### Cargar el programa

**Opción 1 — SD Card:**

1. Copiar `output/chess-clock.bin` a la SD Card como `CHESS`
2. En el monitor 6502:
   ```
   LOAD CHESS 0800
   R 0800
   ```

**Opción 2 — XMODEM:**

```
XRECV 0800
R 0800
```

### Conexiones

| Módulo | Puerto | Pines |
|--------|--------|-------|
| TM1638 | 0xC000 | Bit 0: CLK, Bit 1: DIO, Bit 2: STB |
| SID | $D400 | Mapeo estándar Commodore 64 |

---

## 3. El Teclado TM1638

El módulo QYF-TM1638 tiene 16 botones táctiles organizados en 4 filas × 4 columnas:

```
[ 1] [ 2] [ 3] [P1]      ← P1 = Blancas
[ 5] [ 6] [ 7] [P2]      ← P2 = Negras
[ 9] [10] [11] [RST]     ← Fila 3
[ -] [ 0] [PAU] [SET]    ← Fila 4
```

| Tecla | Nº | Función |
|-------|----|---------|
| **P1** | 4 | Finalizar turno de Blancas |
| **P2** | 8 | Finalizar turno de Negras |
| **RST** | 12 | Reiniciar partida |
| **PAU** | 15 | Pausar / Reanudar |
| **SET** | 16 | Configuración (mantener 1 segundo) |
| **+ (10)** | 10 | Aumentar tiempo en configuración |
| **- (9)** | 9 | Disminuir tiempo en configuración |
| 1,2,3,5,6,7,11,13,14 | — | Sin uso |

> **Nota:** Las teclas 1, 2, 3, 5, 6, 7, 11, 13, 14 no tienen función asignada. Pueden usarse para futuras expansiones.

---

## 4. El Display

El display de 8 dígitos muestra **ambos tiempos simultáneamente**:

```
05.00 05.00
^^^^    ^^^^
 BL      NG
```

- **Izquierda** (dígitos 0-3): tiempo de **Blancas**
- **Derecha** (dígitos 4-7): tiempo de **Negras**
- **Punto decimal** en dígitos 1 y 5: separa minutos de segundos

### Modos de visualización

| Modo | Display | Descripción |
|------|---------|-------------|
| Inicio | `CHESS1.0` | Banner de versión (1.5s) |
| Detenido | `05.00 05.00` | Esperando iniciar partida |
| Partida activa | `04.35 05.00` | Blancas activas, Negras titilan |
| Pausa | ` PAUSADO ` | Partida pausada |
| Configuración | `SET  05  ` | Ajustando minutos |
| Game Over | `BL LOST ` | Blancas perdieron |
| Game Over | `NG LOST ` | Negras perdieron |

### Parpadeo

El jugador **en espera** titila para indicar que no es su turno:
- **500ms visible** — se ve el tiempo completo
- **150ms oculto** — desaparece brevemente

El jugador **activo** se ve siempre fijo para que pueda leer su tiempo sin distracciones.

---

## 5. Cómo Jugar

### Iniciar una partida

1. Al cargar, el display muestra `05.00 05.00` (5 minutos cada uno)
2. Presione **P1** (Blancas) o **P2** (Negras) para comenzar
3. El reloj de **Blancas** arranca la cuenta regresiva (Blancas siempre abren)
4. Se escucha un **click** (confirmación de tecla) y un **tono** (cambio de turno)

### Alternar turnos

Cuando un jugador termina su movimiento:

1. Presiona su botón (**P1** o **P2**)
2. Su reloj se **detiene**
3. El reloj del oponente **se inicia**
4. Se escucha el tono de cambio de turno

> **Importante:** Blancas usan el botón **P1**, Negras usan el botón **P2**. No importa qué botón se presione para iniciar, las Blancas siempre empiezan contando.

### Pausar y reanudar

- Presione **PAU** para pausar la partida
- El display muestra ` PAUSADO `
- Presione **PAU** nuevamente para reanudar
- Al reanudar, el display vuelve a mostrar los tiempos actuales

### Reiniciar

- Presione **RST** en cualquier momento
- Ambos relojes vuelven al tiempo configurado
- El estado vuelve a "detenido" — debe presionar P1 (Blancas) o P2 (Negras) para iniciar

### Fin de la partida

Cuando un jugador se queda sin tiempo:

1. El display muestra `BL LOST ` (Blancas perdieron) o `NG LOST ` (Negras perdieron)
2. Suena un acorde grave (3 voces)
3. El registro UART indica el ganador (el que no perdió)
4. Presione **RST** para reiniciar

### Salir del programa

Escriba la letra **`q`** (minúscula o mayúscula) en la terminal UART. El programa:
- Silencia el SID
- Apaga el display TM1638
- Vuelve al monitor 6502

---

## 6. Configuración de Tiempo

### Cambiar el tiempo base

1. **Mantenga presionado SET** por 1 segundo
2. El display muestra `SET  XX  ` (XX = minutos actuales)
3. Use **+ (10)** para aumentar o **- (9)** para disminuir
4. Rango: **1 a 99 minutos**
5. Presione **SET** nuevamente para guardar
6. También puede presionar **P1** (Blancas) o **P2** (Negras) para guardar e iniciar inmediatamente

### Valores recomendados

| Tipo de partida | Minutos por jugador |
|-----------------|-------------------|
| Blitz extremo | 1-2 min |
| Blitz | 3-5 min |
| Rápido (rapid) | 10-25 min |
| Clásico | 30-99 min |

### Persistencia

La configuración se pierde al apagar la Tang Nano. El valor por defecto es 5 minutos.

---

## 7. Sonidos

El Reloj de Ajedrez usa el chip **SID 6581** con 3 voces independientes. Todos los sonidos son **no-bloqueantes**: el chip genera el sonido mientras el programa sigue funcionando.

### Tabla de sonidos

| Evento | Voz | Onda | Nota | Duración | Descripción |
|--------|-----|------|------|----------|-------------|
| Presionar tecla | 0 | Noise | C7 | ~200ms | Click de confirmación |
| Cambio de turno | 0 | Triangle | A5 | ~250ms | Tono suave ascendente |
| Alarma (≤10s) | 0 | Sawtooth | C7 | ~200ms | Beep cada segundo |
| Game Over | 0, 1, 2 | Sawtooth | C4+E4+G4 | ~3s | Acorde grave completo (3 voces) |
| Pausa | 0 | Triangle | C5 | ~400ms | Tono único |
| Reanudar | 0 | Triangle | E5 | ~400ms | Tono único |
| Salir (q) | todas | — | — | — | Silencio total |

### Volumen

El volumen maestro está configurado al máximo (15) para todos los efectos.
La melodía de bienvenida usa volumen 5/15 para no ser estridente. No es ajustable desde el menú.

---

## 8. Registro UART

La terminal UART muestra un registro detallado de la partida. Es opcional — el reloj funciona sin ella.

### Salida típica

```
================================================
  Reloj de Ajedrez - Monitor 6502
  Version 1.0.0
================================================
-- Reloj de Ajedrez iniciado --
Tiempo: 05:00 por jugador
Presione BL (P1) o NG (P2) para iniciar
-- Partida iniciada --
Blancas: 05:00  Negras: 05:00
Turno de Blancas
Blancas presiona - Turno de Negras (B:04:47  N:05:00)
Negras presiona - Turno de Blancas (B:04:47  N:04:52)
-- Partida pausada --
-- Partida reanudada --
*** Blancas sin tiempo!
-- Fin de la partida --
```

### Configuración de la terminal

| Parámetro | Valor |
|-----------|-------|
| Velocidad | 115200 baud (típica del Monitor 6502) |
| Datos | 8 bits |
| Paridad | Ninguna |
| Stop | 1 bit |
| Control de flujo | Ninguno |

---

## 9. Solución de Problemas

| Problema | Causa posible | Solución |
|----------|---------------|----------|
| No enciende el display | TM1638 no conectado | Verificar cables: CLK, DIO, STB, VCC, GND |
| Números incorrectos | Conexión flojadel bus de datos | Revisar pines del TM1638 |
| No hay sonido | SID no conectado o no implementado en FPGA | Verificar SID en $D400 |
| No responde a teclas | Anti-rebote muy corto o tecla atascada | Revisar botón, esperar 1s |
| El tiempo descuenta mal | Overhead del loop | Es normal ±1s por minuto, aceptable para ajedrez |
| No sale con 'q' | Terminal UART mal configurada | Verificar baudrate y puerto |
| Se ve borroso | Brillo muy alto o bajo | Ajustar `tm1638_set_brightness()` en código |
| Parpadeo molesto | Ciclo visible/oculto muy lento | Se puede ajustar en `blink_counter` del main loop |

---

## 10. Referencia Técnica

### Especificaciones

| Parámetro | Valor |
|-----------|-------|
| Microcontrolador | 6502 @ ~25MHz (FPGA) |
| Display | 8 dígitos 7-segmentos + DP |
| Teclado | 16 botones táctiles |
| Sonido | SID 6581, 3 voces, 4 formas de onda |
| Tiempo máximo | 99:59 por jugador |
| Precisión | ~1s por minuto |
| Tamaño del binario | ~9.8 KB |
| Memoria usada | $0800-$3FFF (~13KB disponibles) |

### Mapa de memoria

| Rango | Uso |
|-------|-----|
| $0020-$003F | Zero Page (variables del programa) |
| $0800-$0846 | Código de inicio (startup) |
| $0847-$2668 | Código principal y librerías |
| $2669-$2AB4 | Datos de solo lectura (strings, tablas) |
| $2ABA-$2AD7 | BSS (variables globales) |
| $3E00-$3FFF | Stack |
| $BF00-$BF50 | ROM API (funciones del monitor) |
| $C000-$C002 | Puerto TM1638 |
| $D400-$D418 | SID 6581 |

### Formatos de tiempo

| Valor | Display | Significado |
|-------|---------|-------------|
| 300s | 05.00 | 5 minutos |
| 90s | 01.30 | 1 minuto 30 segundos |
| 10s | 00.10 | 10 segundos |
| 5999s | 99.59 | 99 minutos 59 segundos (máximo) |

### Dependencias del proyecto

- `cc65` — Compilador C para 6502
- `Monitor 6502` v2.2.0+ — ROM API
- `tm1638-6502-cc65` — Librería de control TM1638
- `6581_SID-6502-cc65` — Librería de sonido SID

---

*Documentación generada para la versión 1.0.0 del Reloj de Ajedrez 6502.*
