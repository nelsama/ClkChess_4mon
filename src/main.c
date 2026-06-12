/**
 * ============================================================================
 * Reloj de Ajedrez - Monitor 6502 + TM1638
 * ============================================================================
 * Reloj de ajedrez para dos jugadores usando el módulo TM1638.
 * 
 * Teclado del TM1638 (QYF-TM1638):
 *   Layout:     
 *   [ 1] [ 2] [ 3] [P1]  →  P1 (4)  = Finalizar turno del Jugador 1
 *   [ 5] [ 6] [ 7] [P2]  →  P2 (8)  = Finalizar turno del Jugador 2
 *   [ 9] [10] [11] [RST] →  RST(12) = Reiniciar partida
 *   [ -] [ 0] [PAU] [SET]→  PAU(15) = Pausa/Reanudar
 *                            SET(16) = Configuración (mantener 1s)
 *                            INC(10) = Aumentar tiempo en config
 *                            DEC( 9) = Disminuir tiempo en config
 * 
 * Display (8 dígitos):
 *   Normal:   "MMSSMMSS"  →  P1:MM:SS  P2:MM:SS  (sin separadores)
 *   Config:   "SET  XX:YY" → Configurando tiempo base
 *   Pausa:    "  PAUSADO " 
 *   Game Over: "P1  TIME!" o "P2  TIME!"
 * 
 * UART:
 *   Escriba "quit" o "q" + Enter para volver al monitor 6502.
 *   Cada movimiento se registra en consola.
 * ============================================================================
 */

#include <stdint.h>
#include "romapi.h"
#include "tm1638.h"
#include "sid.h"

/* ============================================================================
 * HARDWARE
 * ============================================================================ */
#define LEDS            (*(volatile uint8_t *)0xC001)   /* LEDs (lógica negativa) */

/* ============================================================================
 * CONSTANTES
 * ============================================================================ */
#define LONG_PRESS_MS   1000    /* Tiempo para presión larga */

#define DEFAULT_MINUTES 5       /* Tiempo por defecto: 5 minutos */
#define MIN_MINUTES     1       /* Mínimo tiempo configurable */
#define MAX_MINUTES     99      /* Máximo tiempo configurable */
#define TICK_MS         50      /* Delay del loop principal (ms) */
#define TICKS_PER_SEC   20      /* 20 ticks de 50ms = 1 segundo */

/* IDs de teclas del TM1638 */
#define KEY_P1      4   /* Jugador 1 */
#define KEY_P2      8   /* Jugador 2 */
#define KEY_DEC     9   /* Disminuir */
#define KEY_INC     10  /* Aumentar */
#define KEY_RST     12  /* Reiniciar */
#define KEY_PAU     15  /* Pausa */
#define KEY_SET     16  /* Configuración */

/* Versión del programa */
#define VERSION_MAJOR   1
#define VERSION_MINOR   0
#define VERSION_PATCH   0
#define VERSION_STR     "1.0.0"
#define VERSION_DISPLAY "CHESS1.0"

/* ============================================================================
 * TIPOS Y ESTADOS
 * ============================================================================ */

/* Estados del reloj de ajedrez */
typedef enum {
    CLOCK_STOPPED,          /* Reloj detenido (estado inicial o tras reset) */
    CLOCK_RUNNING,          /* Jugador activo cuenta regresiva */
    CLOCK_PAUSED,           /* Partida pausada */
    CLOCK_SETTINGS,         /* Modo configuración */
    CLOCK_GAME_OVER         /* Un jugador se quedó sin tiempo */
} clock_state_t;

/* ============================================================================
 * VARIABLES GLOBALES
 * ============================================================================ */
static uint16_t p1_time;            /* Tiempo restante del Jugador 1 (segundos) */
static uint16_t p2_time;            /* Tiempo restante del Jugador 2 (segundos) */
static uint16_t init_time;          /* Tiempo inicial configurado (segundos) */
static clock_state_t state;         /* Estado actual */
static uint8_t active_player;       /* Jugador activo (1 o 2) */

static uint8_t last_key;            /* Última tecla presionada */
static uint8_t key_processed;       /* Flag de tecla procesada */

static uint8_t blink_counter;       /* Contador para parpadeo (cada 500ms) */
static uint8_t blink_state;         /* 1 = visible, 0 = oculto */

static uint8_t sound_timer;         /* Timer para apagar sonidos sostenidos */

/* ============================================================================
 * SONIDOS SID (no bloqueantes - el envelope ADSR maneja la duracion)
 * ============================================================================ */

/* Resetear voz y tocar una nota */
static void sound_play(uint8_t voice, uint16_t freq, uint8_t wave,
                       uint8_t a, uint8_t d, uint8_t s, uint8_t r) {
    sid_gate_off(voice);
    sid_voice(voice, freq, wave, a, d, s, r);
    sid_gate_on(voice);
}

/* Click al presionar tecla */
static void sound_click(void) {
    sound_play(0, NOTE_C7, SID_NOISE, 0, 8, 0, 2);
}

/* Cambio de turno */
static void sound_switch(void) {
    sound_play(0, NOTE_A5, SID_TRIANGLE, 0, 6, 0, 4);
}

/* Advertencia ultimos 10 segundos: beep corto y seco (sustain=0 se apaga solo) */
static void sound_warning(void) {
    sound_play(0, NOTE_C7, SID_SAWTOOTH, 0, 4, 0, 2);
}

/* Fin del juego: acorde sostenido y fuerte (3 voces) */
static void sound_game_over(void) {
    sound_play(0, NOTE_C4, SID_SAWTOOTH, 0, 8, 10, 10);
    sound_play(1, NOTE_E4, SID_SAWTOOTH, 0, 8, 10, 10);
    sound_play(2, NOTE_G4, SID_SAWTOOTH, 0, 8, 10, 10);
    sound_timer = 60;  /* Apagar tras ~3 segundos (60 ticks * 50ms) */
}

/* Pausa */
static void sound_pause(void) {
    sound_play(0, NOTE_C5, SID_TRIANGLE, 0, 8, 0, 6);
}

/* Reanudar */
static void sound_resume(void) {
    sound_play(0, NOTE_E5, SID_TRIANGLE, 0, 8, 0, 6);
}

/* ============================================================================
 * FUNCIONES AUXILIARES
 * ============================================================================ */

/* Enviar string por UART */
static void uart_print(const char *s) {
    while (*s) rom_uart_putc(*s++);
}

/* Formatear tiempo (segundos) a string de 4 dígitos: MMSS */
/* Ej: 305 seg → "0505" (5:05) */
static void format_time_4d(uint16_t seconds, char *buf) {
    uint8_t mins, secs;
    
    if (seconds >= 6000) {
        /* Más de 99:59 → mostrar solo minutos, saturar */
        mins = seconds / 60;
        if (mins > 99) mins = 99;
        buf[0] = '0' + mins / 10;
        buf[1] = '0' + mins % 10;
        buf[2] = '9';
        buf[3] = '9';
    } else {
        mins = seconds / 60;
        secs = seconds % 60;
        buf[0] = '0' + mins / 10;
        buf[1] = '0' + mins % 10;
        buf[2] = '0' + secs / 10;
        buf[3] = '0' + secs % 10;
    }
}

/* Actualizar display con ambos tiempos usando bajo nivel para DPs */
/* Formato: P1:MM:SS + P2:MM:SS = MMSSMMSS con DPs como separadores */
/* Los DPs encienden en el dígito de unidades de minuto:  digito 1 y 5 */
static void update_display(void) {
    char text[9];
    uint8_t segments[8];
    uint8_t grids[8];
    char p1_buf[5], p2_buf[5];
    
    format_time_4d(p1_time, p1_buf);
    format_time_4d(p2_time, p2_buf);
    
    /* Armar string de 8 caracteres sin puntos */
    text[0] = p1_buf[0];
    text[1] = p1_buf[1];
    text[2] = p1_buf[2];
    text[3] = p1_buf[3];
    text[4] = p2_buf[0];
    text[5] = p2_buf[1];
    text[6] = p2_buf[2];
    text[7] = p2_buf[3];
    text[8] = '\0';
    
    /* Convertir texto a segmentos */
    tm1638_encode_ascii8(text, segments);
    
    /* Activar DP (bit 7 = 0x80) como separador de minutos y segundos */
    /* encode_ascii invierte el orden: text[0] -> segments[7] (izquierda) */
    /* P1 text[1]='5' -> segments[6]: DP en unidades de minuto de P1 */
    segments[6] |= 0x80;
    /* P2 text[5]='5' -> segments[2]: DP en unidades de minuto de P2 */
    segments[2] |= 0x80;
    
    /* Parpadeo: titila el jugador en PAUSA (el que cuenta se ve siempre) */
    if (state == CLOCK_RUNNING && !blink_state) {
        if (active_player == 1) {
            /* P1 activo -> titila P2 (derecha): segments[3..0] */
            segments[3] = 0x00;
            segments[2] = 0x00;
            segments[1] = 0x00;
            segments[0] = 0x00;
        } else {
            /* P2 activo -> titila P1 (izquierda): segments[7..4] */
            segments[7] = 0x00;
            segments[6] = 0x00;
            segments[5] = 0x00;
            segments[4] = 0x00;
        }
    }
    
    /* Convertir a formato grid y mostrar */
    tm1638_digits_common_anode8(segments, grids);
    tm1638_display(grids);
}

/* Mostrar mensaje de 8 caracteres en el display */
static void show_message(const char *msg) {
    tm1638_show_text(msg);
}

/* ============================================================================
 * FUNCIONES DEL RELOJ
 * ============================================================================ */

/* Iniciar partida: arranca el reloj del Jugador 1 */
static void clock_start(void) {
    state = CLOCK_RUNNING;
    active_player = 1;
    blink_state = 1;  /* Mostrar inmediatamente */
    blink_counter = 0;
    uart_print("-- Partida iniciada --\r\n");
    uart_print("Jugador 1: ");
    {
        char buf[10];
        uint8_t m = p1_time / 60;
        uint8_t s = p1_time % 60;
        buf[0] = '0' + m / 10;
        buf[1] = '0' + m % 10;
        buf[2] = ':';
        buf[3] = '0' + s / 10;
        buf[4] = '0' + s % 10;
        buf[5] = '\0';
        uart_print(buf);
    }
    uart_print("  Jugador 2: ");
    {
        char buf[10];
        uint8_t m = p2_time / 60;
        uint8_t s = p2_time % 60;
        buf[0] = '0' + m / 10;
        buf[1] = '0' + m % 10;
        buf[2] = ':';
        buf[3] = '0' + s / 10;
        buf[4] = '0' + s % 10;
        buf[5] = '\0';
        uart_print(buf);
    }
    uart_print("\r\n");
    uart_print("Turno del Jugador 1\r\n");
}

/* Finalizar turno del jugador actual */
static void clock_switch_player(void) {
    uint8_t new_player;
    
    if (state == CLOCK_GAME_OVER) return;
    
    if (state == CLOCK_STOPPED) {
        /* Primera pulsación: iniciar partida */
        clock_start();
        return;
    }
    
    if (state == CLOCK_RUNNING) {
        /* Cambiar al otro jugador */
        new_player = (active_player == 1) ? 2 : 1;
        blink_state = 1;  /* Mostrar inmediatamente al nuevo jugador */
        blink_counter = 0;
        sound_switch();  /* Sonido de cambio de turno */
        
        uart_print("Jugador ");
        rom_uart_putc('0' + active_player);
        uart_print(" presiona - Turno del Jugador ");
        rom_uart_putc('0' + new_player);
        uart_print(" (P1:");
        {
            char buf[10];
            uint8_t m = p1_time / 60;
            uint8_t s = p1_time % 60;
            buf[0] = '0' + m / 10;
            buf[1] = '0' + m % 10;
            buf[2] = ':';
            buf[3] = '0' + s / 10;
            buf[4] = '0' + s % 10;
            buf[5] = '\0';
            uart_print(buf);
        }
        uart_print("  P2:");
        {
            char buf[10];
            uint8_t m = p2_time / 60;
            uint8_t s = p2_time % 60;
            buf[0] = '0' + m / 10;
            buf[1] = '0' + m % 10;
            buf[2] = ':';
            buf[3] = '0' + s / 10;
            buf[4] = '0' + s % 10;
            buf[5] = '\0';
            uart_print(buf);
        }
        uart_print(")\r\n");
        
        active_player = new_player;
        update_display();
    }
}

/* Pausar o reanudar la partida */
static void clock_toggle_pause(void) {
    if (state == CLOCK_RUNNING) {
        state = CLOCK_PAUSED;
        show_message(" PAUSADO ");
        sound_pause();
        uart_print("-- Partida pausada --\r\n");
    } else if (state == CLOCK_PAUSED) {
        state = CLOCK_RUNNING;
        blink_state = 1;   /* Mostrar inmediatamente al reanudar */
        blink_counter = 0;
        sound_resume();
        update_display();
        uart_print("-- Partida reanudada --\r\n");
    }
}

/* Reiniciar partida */
static void clock_reset(void) {
    p1_time = init_time;
    p2_time = init_time;
    state = CLOCK_STOPPED;
    active_player = 0;
    blink_state = 1;
    blink_counter = 0;
    update_display();
    uart_print("-- Partida reiniciada --\r\n");
    uart_print("Tiempo: ");
    {
        char buf[10];
        uint8_t m = init_time / 60;
        uint8_t s = init_time % 60;
        buf[0] = '0' + m / 10;
        buf[1] = '0' + m % 10;
        buf[2] = ':';
        buf[3] = '0' + s / 10;
        buf[4] = '0' + s % 10;
        buf[5] = '\0';
        uart_print(buf);
    }
    uart_print(" por jugador\r\n");
    uart_print("Presione P1 o P2 para iniciar\r\n");
}

/* Verificar si un jugador se quedó sin tiempo */
static void clock_check_timeout(void) {
    uint8_t loser;
    char msg[9];
    
    if (state != CLOCK_RUNNING) return;
    
    loser = 0;
    if (p1_time == 0) loser = 1;
    if (p2_time == 0) loser = 2;
    
    if (loser > 0) {
        state = CLOCK_GAME_OVER;
        msg[0] = 'P';
        msg[1] = '0' + loser;
        msg[2] = ' ';
        msg[3] = 'T';
        msg[4] = 'I';
        msg[5] = 'M';
        msg[6] = 'E';
        msg[7] = '!';
        msg[8] = '\0';
        sound_game_over();
        show_message(msg);
        
        uart_print("*** ");
        uart_print("Jugador ");
        rom_uart_putc('0' + loser);
        uart_print(" se quedo sin tiempo!\r\n");
        uart_print("-- Fin de la partida --\r\n");
    }
}

/* Tick del reloj: descuenta 1 segundo del jugador activo */
static void clock_tick(void) {
    uint16_t remaining;
    
    if (state != CLOCK_RUNNING) return;
    
    if (active_player == 1) {
        if (p1_time > 0) p1_time--;
        remaining = p1_time;
    } else {
        if (p2_time > 0) p2_time--;
        remaining = p2_time;
    }
    
    /* Alarma cuando quedan 10s o menos */
    if (remaining <= 10 && remaining > 0) {
        sound_warning();
    }
    
    update_display();
    clock_check_timeout();
}

/* ============================================================================
 * MODO CONFIGURACIÓN
 * ============================================================================ */

static uint8_t settings_minutes;    /* Minutos en configuración */

/* Forward declarations */
static void settings_update_display(void);
static void settings_change(int8_t delta);

static void settings_enter(void) {
    if (state == CLOCK_SETTINGS) {
        /* Salir de configuración, guardar tiempo */
        init_time = settings_minutes * 60;
        clock_reset();
        uart_print("Tiempo configurado: ");
        {
            char buf[10];
            uint8_t m = init_time / 60;
            uint8_t s = init_time % 60;
            buf[0] = '0' + m / 10;
            buf[1] = '0' + m % 10;
            buf[2] = ':';
            buf[3] = '0' + s / 10;
            buf[4] = '0' + s % 10;
            buf[5] = '\0';
            uart_print(buf);
        }
        uart_print("\r\n");
        return;
    }
    
    /* Entrar a configuración */
    settings_minutes = init_time / 60;
    if (settings_minutes < MIN_MINUTES) settings_minutes = MIN_MINUTES;
    if (settings_minutes > MAX_MINUTES) settings_minutes = MAX_MINUTES;
    
    state = CLOCK_SETTINGS;
    uart_print("-- Modo configuracion --\r\n");
    uart_print("Use + y - para ajustar minutos. Presione SET para guardar.\r\n");
    settings_update_display();
}

static void settings_update_display(void) {
    char display[9];
    display[0] = 'S';
    display[1] = 'E';
    display[2] = 'T';
    display[3] = ' ';
    display[4] = '0' + settings_minutes / 10;
    display[5] = '0' + settings_minutes % 10;
    display[6] = ':';
    display[7] = '0' + 0;  /* Segundos siempre 00 en config */
    display[8] = '\0';
    /* Pero TM1638 no muestra ':' así que ponemos espacio */
    display[6] = ' ';
    display[7] = '0';
    tm1638_show_text(display);
}

static void settings_change(int8_t delta) {
    int16_t new_val = (int16_t)settings_minutes + delta;
    if (new_val < MIN_MINUTES) new_val = MIN_MINUTES;
    if (new_val > MAX_MINUTES) new_val = MAX_MINUTES;
    settings_minutes = (uint8_t)new_val;
    settings_update_display();
    
    /* Mostrar valor por UART */
    uart_print("Tiempo: ");
    {
        char buf[10];
        buf[0] = '0' + settings_minutes / 10;
        buf[1] = '0' + settings_minutes % 10;
        buf[2] = ':';
        buf[3] = '0';
        buf[4] = '0';
        buf[5] = '\0';
        uart_print(buf);
        uart_print("\r\n");
    }
}

/* ============================================================================
 * MANEJO DE TECLAS
 * ============================================================================ */

/* Leer UART: si se presiona 'q' (sin Enter), salir inmediatamente */
static uint8_t check_uart_command(void) {
    char c;
    
    while (rom_uart_rx_ready()) {
        c = rom_uart_getc();
        if (c == 'q' || c == 'Q') {
            return 1;  /* 'q' directo = salir */
        }
    }
    return 0;
}

/* Esperar que se suelte una tecla, midiendo tiempo */
/* Retorna: 0 = presión corta, 1 = presión larga, 2 = rebote */
static uint8_t wait_key_release(uint8_t expected_key) {
    uint16_t count = 0;
    uint16_t long_press_ticks = LONG_PRESS_MS / 10;
    
    while (tm1638_get_key_pressed() == expected_key) {
        rom_delay_ms(10);
        count++;
        
        if (count >= long_press_ticks) {
            return 1;  /* Presión larga */
        }
    }
    
    if (count < 5) return 2;  /* Rebote */
    return 0;  /* Presión corta */
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    uint8_t key;
    uint8_t tick_counter;   /* Contador de ticks para 1 segundo */
    uint8_t hold_result;
    
    /* Banner por UART */
    uart_print("\r\n");
    uart_print("================================================\r\n");
    uart_print("  Reloj de Ajedrez - Monitor 6502\r\n");
    uart_print("  Version ");
    uart_print(VERSION_STR);
    uart_print("\r\n");
    uart_print("================================================\r\n");
    uart_print("Teclado TM1638:\r\n");
    uart_print("  [ 1] [ 2] [ 3] [P1]    P1 = Jugador 1\r\n");
    uart_print("  [ 5] [ 6] [ 7] [P2]    P2 = Jugador 2\r\n");
    uart_print("  [ 9] [10] [11] [RST]   RST = Reiniciar\r\n");
    uart_print("  [ -] [ 0] [PAU] [SET]  PAU = Pausa, SET = Config\r\n");
    uart_print("  9(-) y 10(+) ajustan tiempo en config\r\n");
    uart_print("Mantener SET 1s = Configuracion\r\n");
    uart_print("Escriba 'quit' o 'q' + Enter para salir\r\n\r\n");
    uart_print("-- Reloj de Ajedrez iniciado --\r\n");
    
    /* Apagar LEDs del hardware */
    LEDS = 0xFF;
    
    /* Inicializar TM1638 */
    tm1638_init();
    tm1638_set_brightness(4);  /* Brillo medio */
    
    /* Inicializar SID (sonido) */
    sid_init();
    sid_volume(15);  /* Volumen maximo */
    
    /* Mostrar banner en display */
    tm1638_show_text(VERSION_DISPLAY);
    rom_delay_ms(1500);
    
    /* Inicializar tiempo por defecto */
    init_time = DEFAULT_MINUTES * 60;
    clock_reset();
    
    tick_counter = 0;
    
    /* Loop principal */
    while (1) {
        /* Verificar comando UART (quit) */
        if (check_uart_command()) {
            uart_print("\r\n-- Fin --\r\n");
            sid_mute();                      /* Silenciar SID */
            tm1638_clear_display();          /* Apagar display */
            tm1638_set_brightness(0);         /* Brillo minimo */
            LEDS = 0x00;                      /* Apagar LEDs HW */
            uart_print("Volviendo al monitor 6502...\r\n");
            break;
        }
        
        /* Tick: 20 iteraciones * 50ms = 1 segundo */
        tick_counter++;
        if (tick_counter >= TICKS_PER_SEC) {
            tick_counter = 0;
            clock_tick();
        }
        
        /* Parpadeo: 500ms encendido / 150ms apagado */
        if (state == CLOCK_RUNNING) {
            blink_counter++;
            if (blink_state) {
                /* Fase encendido: 500ms (10 ticks) */
                if (blink_counter >= 10) {
                    blink_counter = 0;
                    blink_state = 0;
                    update_display();
                }
            } else {
                /* Fase apagado: 150ms (3 ticks) */
                if (blink_counter >= 3) {
                    blink_counter = 0;
                    blink_state = 1;
                    update_display();
                }
            }
        }
        
        /* Timer de sonido: apagar voces cuando expira */
        if (sound_timer > 0) {
            sound_timer--;
            if (sound_timer == 0) {
                sid_gate_off(0);
                sid_gate_off(1);
                sid_gate_off(2);
            }
        }
        
        /* Leer tecla del TM1638 */
        key = tm1638_get_key_pressed();
        
        if (key > 0) {
            if (key != last_key) {
                last_key = key;
                key_processed = 0;
            }
            
            if (!key_processed) {
                /* === TECLA SET (16) - Presión larga = Configuración === */
                if (key == KEY_SET) {
                    hold_result = wait_key_release(key);
                    if (hold_result == 1) {
                        /* Presión larga: entrar/salir de configuración */
                        settings_enter();
                    }
                    key_processed = 1;
                    key = 0;
                
                /* === MODO CONFIGURACIÓN === */
                } else if (state == CLOCK_SETTINGS) {
                    if (key == KEY_INC) {
                        settings_change(1);
                    } else if (key == KEY_DEC) {
                        settings_change(-1);
                    } else if (key == KEY_P1 || key == KEY_P2) {
                        /* Salir y empezar partida */
                        init_time = settings_minutes * 60;
                        clock_reset();
                        if (key == KEY_P1 || key == KEY_P2) {
                            clock_switch_player();  /* Inicia partida */
                        }
                    }
                    key_processed = 1;
                
                /* === TECLAS NORMALES === */
                } else {
                    switch (key) {
                        case KEY_P1:
                        case KEY_P2:
                            sound_click();
                            clock_switch_player();
                            break;
                        
                        case KEY_RST:
                            sound_click();
                            clock_reset();
                            break;
                        
                        case KEY_PAU:
                            clock_toggle_pause();
                            break;
                    }
                    key_processed = 1;
                }
            }
            
        } else {
            /* Tecla liberada */
            last_key = 0;
            key_processed = 0;
        }
        
        /* Pequeño delay para no saturar el bus TM1638 */
        rom_delay_ms(TICK_MS);
    }
    
    return 0;
}
