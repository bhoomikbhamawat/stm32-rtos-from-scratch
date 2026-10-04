# Bare-metal STM32F407VG RTOS + MPU-6050 pipeline
#
#   make                 build (build/rtos_imu.elf / .bin / .hex)
#   make flash           program the board via ST-LINK + OpenOCD
#   make bench           flash, run, and print the context-switch stats (XX / YY)
#   make debug           start an OpenOCD GDB server (then `make gdb` in another terminal)
#   make gdb             attach GDB to the running OpenOCD server
#   make selftest        build + run the kernel self-test (priority inheritance, sem timeout)
#   make size / clean
#
# Build options (pass on the command line):
#   BENCH_FPU=1          benchmark with FPU context in both tasks (extended frames)
#   FAULT_TEST=1..4      add a task that crashes on purpose after 3 s
#                        1=div-by-zero 2=bus error 3=undefined instr 4=bad fn pointer
#   SCHED=linear         original O(n) scheduler (default: o1 = bitmap + CLZ)
#   BENCH_LOAD=1         benchmark under load: SysTick running + 10 kHz timer ISR
#   SIM_IMU=1            synthetic 100 Hz IMU data (no MPU-6050 needed)
#   OPT=-O0              change optimisation (default -O2; quote numbers with it!)

TARGET    := rtos_imu
BUILD     := build

PREFIX    ?= arm-none-eabi-
CC        := $(PREFIX)gcc
OBJCOPY   := $(PREFIX)objcopy
SIZE      := $(PREFIX)size
GDB       ?= $(PREFIX)gdb
OPENOCD   ?= openocd
PYTHON    ?= python3

OPENOCD_CFG := -f board/stm32f4discovery.cfg

BENCH_FPU  ?= 0
FAULT_TEST ?= 0
SIM_IMU    ?= 0
SCHED      ?= o1
BENCH_LOAD ?= 0
OPT        ?= -O2

CPU     := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS    := -DCS_BENCH_FPU=$(BENCH_FPU) -DFAULT_TEST=$(FAULT_TEST) -DSIM_IMU=$(SIM_IMU) -DOS_SCHED_O1=$(if $(filter linear,$(SCHED)),0,1) -DCS_BENCH_LOAD=$(BENCH_LOAD)
CFLAGS  := $(CPU) $(OPT) -g3 -std=c11 -Wall -Wextra -Wshadow -Wdouble-promotion \
           -ffreestanding -ffunction-sections -fdata-sections -fno-common \
           -Iinc $(DEFS) -MMD -MP
ASFLAGS := $(CPU) -g
LDFLAGS := $(CPU) -T linker/stm32f407vg.ld -nostartfiles \
           --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map -Wl,--print-memory-usage
LDLIBS  := -lm

SRCS_C  := startup/startup_stm32f407.c $(wildcard src/*.c)
SRCS_S  := $(wildcard src/*.s)
OBJS    := $(addprefix $(BUILD)/,$(SRCS_C:.c=.o) $(SRCS_S:.s=.o))
DEPS    := $(OBJS:.o=.d)

# Rebuild everything when the options change
OPTS_STAMP := $(BUILD)/.opts_$(BENCH_FPU)_$(FAULT_TEST)_$(SIM_IMU)_$(SCHED)_$(BENCH_LOAD)_$(subst -,,$(OPT))

.PHONY: all flash bench selftest debug gdb size clean

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex

$(OPTS_STAMP):
	@mkdir -p $(BUILD)
	@rm -f $(BUILD)/.opts_*
	@touch $@

$(BUILD)/%.o: %.c $(OPTS_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s $(OPTS_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) linker/stm32f407vg.ld
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

flash: $(BUILD)/$(TARGET).elf
	$(OPENOCD) $(OPENOCD_CFG) -c "program $< verify reset exit"

# Flash, run until cs_bench_complete(), print g_cs, dump raw samples, analyse
bench: $(BUILD)/$(TARGET).elf
	@echo ">>> starting OpenOCD in the background"
	@$(OPENOCD) $(OPENOCD_CFG) > $(BUILD)/openocd.log 2>&1 & echo $$! > $(BUILD)/openocd.pid
	@sleep 2
	-$(GDB) -q -batch -x tools/cs_bench.gdb $<
	@kill `cat $(BUILD)/openocd.pid` 2>/dev/null || true
	@rm -f $(BUILD)/openocd.pid
	$(PYTHON) tools/analyze_cs.py $(BUILD)/cs_samples.bin --cpu-hz 168000000

# Kernel self-test: same kernel, tests/os_selftest.c instead of src/main.c
SELFTEST_OBJS := $(filter-out $(BUILD)/src/main.o,$(OBJS)) $(BUILD)/tests/os_selftest.o

$(BUILD)/selftest.elf: $(SELFTEST_OBJS) linker/stm32f407vg.ld
	$(CC) $(LDFLAGS) $(SELFTEST_OBJS) $(LDLIBS) -o $@

selftest: $(BUILD)/selftest.elf
	@$(OPENOCD) $(OPENOCD_CFG) > $(BUILD)/openocd.log 2>&1 & echo $$! > $(BUILD)/openocd.pid
	@sleep 2
	-$(GDB) -q -batch -x tools/selftest.gdb $<
	@kill `cat $(BUILD)/openocd.pid` 2>/dev/null || true
	@rm -f $(BUILD)/openocd.pid

debug:
	$(OPENOCD) $(OPENOCD_CFG)

gdb: $(BUILD)/$(TARGET).elf
	$(GDB) -q -x tools/gdbinit $<

size: $(BUILD)/$(TARGET).elf
	$(SIZE) -A $<

clean:
	rm -rf $(BUILD)

-include $(DEPS)
