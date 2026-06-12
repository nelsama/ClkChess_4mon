# ============================================================================
# Makefile - Reloj de Ajedrez para Monitor 6502
# ============================================================================
# Uso:
#   make        - Compilar el programa
#   make clean  - Limpiar archivos generados
#   make info   - Ver tamaño del binario
#   make map    - Ver mapa de memoria
# ============================================================================

# Herramientas (se asume que están en PATH)
CC = cl65
CA65 = ca65
LD = ld65

# Ruta de CC65 (ajustar según instalación)
CC65_LIB = D:\cc65\lib\none.lib

# Directorios
SRC_DIR = src
CONFIG_DIR = config
BUILD_DIR = build
OUTPUT_DIR = output
LIB_DIR = ../../libs

# Configuración del linker
LD_CONFIG = $(CONFIG_DIR)/programa.cfg

# ============================================
# LIBRERÍAS
# ============================================
TM1638_DIR = $(LIB_DIR)/tm1638-6502-cc65
SID_DIR = $(LIB_DIR)/6581_SID-6502-cc65

INCLUDES = -I$(TM1638_DIR)/include -I$(SID_DIR)/src

# Nombre del programa
PROGRAM_NAME = chess-clock

# Archivos de salida
PROGRAM = $(OUTPUT_DIR)/$(PROGRAM_NAME).bin
MAP_FILE = $(OUTPUT_DIR)/$(PROGRAM_NAME).map

# Archivos fuente
C_SOURCES = $(SRC_DIR)/main.c
ASM_SOURCES = $(SRC_DIR)/startup.s

# Archivos objeto
C_OBJECTS = $(BUILD_DIR)/main.o
ASM_OBJECTS = $(BUILD_DIR)/startup.o
TM1638_OBJ = $(BUILD_DIR)/tm1638.o
SID_OBJ = $(BUILD_DIR)/sid.o

OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS) $(TM1638_OBJ) $(SID_OBJ)

# Flags del compilador C
CFLAGS = -t none $(INCLUDES) -O --cpu 6502 -I $(SRC_DIR) -I include

# Flags del ensamblador
ASFLAGS = -t none --cpu 6502

# Flags del linker
LDFLAGS = -C $(LD_CONFIG) -m $(MAP_FILE)

# ============================================================================
# REGLAS PRINCIPALES
# ============================================================================

all: dirs $(PROGRAM)
	@echo ""
	@echo "========================================"
	@echo "Programa generado: $(PROGRAM)"
	@ls -la $(PROGRAM) 2>/dev/null || echo "(verificar tamano)"
	@echo "========================================"
	@echo "Para usar:"
	@echo "  1. Copiar a SD como CHESS"
	@echo "  2. En el monitor:"
	@echo "     LOAD CHESS 0800"
	@echo "     R 0800"
	@echo "========================================"

dirs:
	@mkdir -p $(BUILD_DIR) $(OUTPUT_DIR)

# Compilar C
$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c
	$(CC) -c $(CFLAGS) -o $@ $<

# Ensamblar startup
$(BUILD_DIR)/startup.o: $(SRC_DIR)/startup.s
	$(CA65) $(ASFLAGS) -o $@ $<

# Linkar
$(PROGRAM): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) $(CC65_LIB)

# Compilar TM1638
$(TM1638_OBJ): $(TM1638_DIR)/src/tm1638.c
	$(CC) -c $(CFLAGS) -o $@ $<

# Ensamblar SID
$(SID_OBJ): $(SID_DIR)/src/sid.s
	$(CA65) $(ASFLAGS) -o $@ $<

# ============================================================================
# UTILIDADES
# ============================================================================

info:
	@echo "========================================"
	@echo "Informacion del programa"
	@echo "========================================"
	@if [ -f $(PROGRAM) ]; then ls -la $(PROGRAM); else echo "Error: Programa no compilado"; fi

map:
	@if [ -f $(MAP_FILE) ]; then cat $(MAP_FILE); else echo "Error: Archivo de mapa no encontrado. Compilar primero."; fi

clean:
	@rm -rf $(BUILD_DIR) $(OUTPUT_DIR)
	@echo "Limpieza completa"

# ============================================================================
# AYUDA
# ============================================================================

help:
	@echo "Uso del makefile:"
	@echo "  make        - Compilar el programa"
	@echo "  make clean  - Limpiar archivos generados"
	@echo "  make info   - Ver informacion del binario"
	@echo "  make map    - Ver mapa de memoria"

.PHONY: all dirs clean info map help
