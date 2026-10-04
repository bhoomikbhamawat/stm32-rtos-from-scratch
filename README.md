# Bare-Metal ARM Cortex-M4 RTOS & Real-Time Sensor Pipeline (STM32F407VG)

A HAL-free, CMSIS-free preemptive RTOS for the STM32F4-Discovery board, plus a
register-level I2C driver for the MPU-6050 IMU, a DWT-based context-switch
benchmark and a custom HardFault handler.

```
startup/startup_stm32f407.c  vector table, Reset_Handler (.data copy, .bss zero, FPU on)
linker/stm32f407vg.ld        memory map: FLASH / SRAM / CCM, .noinit, MSP reservation
inc/stm32f407.h              hand-written register map (RCC, GPIO, I2C, SCB, SysTick, DWT)
src/clock.c                  HSE 8 MHz -> PLL -> 168 MHz, flash wait states, ART accelerator
src/os.c                     TCBs, O(1) bitmap/CLZ scheduler (or linear), ready lists, SysTick, delays
src/os_port.s                PendSV context switch (incl. FPU S16-S31), SVC first-task launch
src/os_sync.c                counting semaphore (ISR-safe), mutex with priority inheritance
src/i2c.c                    polling I2C1 master, 400 kHz, timeouts, bus recovery
src/mpu6050.c                MPU-6050 init for 100 Hz ODR, burst read with DATA_RDY check
src/cs_bench.c               context-switch latency benchmark (gives you XX and YY)
src/fault.c                  HardFault/MemManage/BusFault/UsageFault crash recorder
src/main.c                   tasks: bench, imu (prio 3), proc (2), stats (1), idle (0)
tools/cs_bench.gdb           used by `make bench`
tools/gdbinit                interactive GDB with `cs`, `tasks`, `imu`, `fault` commands
tools/analyze_cs.py          mean / max / median / p99 / histogram from the raw samples
tests/os_selftest.c          on-target kernel test: priority inheritance + semaphore timeout
tools/selftest.gdb           used by `make selftest`
```

---

## 1. Install the toolchain (macOS)

```bash
brew install --cask gcc-arm-embedded   # arm-none-eabi-gcc + newlib + arm-none-eabi-gdb
brew install openocd                   # talks to the on-board ST-LINK
arm-none-eabi-gcc --version            # check
openocd --version
```

> Use the **cask** `gcc-arm-embedded` (Arm GNU Toolchain). The plain
> `arm-none-eabi-gcc` formula ships without newlib (`nano.specs`) and GDB.

## 2. Build

```bash
make                 # -> build/rtos_imu.elf / .bin / .hex, prints memory usage
make SIM_IMU=1       # same, but with a synthetic 100 Hz IMU (use until the MPU-6050 is soldered)
```

## 3. Flash and run

Plug the Discovery board in with the **mini-USB** (ST-LINK) connector.

```bash
make flash
```

What you should see:

| LED | Meaning |
|---|---|
| Green blinks at 0.5 Hz (toggles every 1 s) | scheduler running, stats task alive |
| Blue toggles every 0.5 s | IMU pipeline at 100 Hz (real sensor or `SIM_IMU=1`) |
| Orange solid | MPU-6050 not found (expected until it's soldered) |
| Red solid | a fault was recorded (this boot or the last one) |

## 4. Getting XX and YY

### One command

```bash
make bench
```

This starts OpenOCD, flashes, runs until the benchmark finishes (breakpoint
on `cs_bench_complete()`), prints the result and dumps all 10,000 raw samples:

```
================ CONTEXT SWITCH BENCHMARK ================
  samples          : 10000 task-to-task switches
  CPU clock        : 168000000 Hz
  AVERAGE  (XX)    : ...  cycles  = ... ns
  WORST    (YY)    : ...  cycles  = ... ns
==========================================================
```

followed by `tools/analyze_cs.py` output (median, p99, p99.9, histogram).
**The MPU-6050 is not needed for this**: the benchmark runs at boot,
before the IMU task starts.

### By hand (good to know for the interview)

```bash
make debug                    # terminal 1: OpenOCD GDB server on :3333
make gdb                      # terminal 2: flashes, halts at reset
(gdb) break cs_bench_complete
(gdb) continue
(gdb) cs                      # prints XX / YY
(gdb) p g_cs                  # whole struct
(gdb) tasks                   # task table
```

### What exactly is measured

Two tasks at the same, highest priority ping-pong with `os_yield()`:

```
task A: t0 = DWT->CYCCNT; os_yield();   ──►   task B: t1 = DWT->CYCCNT
                                               sample = t1 - t0 - calib
```

So each sample is the **full yield-to-resume latency** a task sees:
`os_yield()` setting PENDSVSET + DSB/ISB → exception entry (8 regs stacked
by hardware) → PendSV (save R4-R11, run scheduler, restore) → exception
return → running in the other task. `calib` (the cost of one CYCCNT read,
1-2 cycles) is measured at start-up and subtracted.

SysTick is paused during the 10,000 switches and the two benchmark tasks
outrank everything else, so no interrupt or other task lands in a sample.
The worst case therefore reflects the switch path itself (pipeline,
flash/ART cache misses, bus contention), not interference.

### Numbers to collect for the resume

Run each and note the result:

Each option rebuilds automatically, no `make clean` needed:

| # | Run | Command | What it shows |
|---|---|---|---|
| 1 | Old scheduler, isolated | `make SCHED=linear bench` | the "before" number (O(n) scan) |
| 2 | New scheduler, isolated | `make bench` | the "after" number (bitmap + CLZ) |
| 3 | New scheduler, FPU context | `make BENCH_FPU=1 bench` | cost of saving S0-S31 |
| 4 | Old scheduler, under load | `make SCHED=linear BENCH_LOAD=1 bench` | worst case with interrupts |
| 5 | New scheduler, under load | `make BENCH_LOAD=1 bench` | worst case with interrupts |
| 6 | Kernel self-test | `make selftest` | priority inheritance works (PASS) |

Run 1 should reproduce the 235 cycles you already measured (the linear
code is unchanged). Run 2 minus run 1 is your optimisation.

Put the **-O2** numbers in the resume line. Always quote the conditions:
168 MHz, -O2, flash with ART accelerator on, 6 tasks.

### The two scheduler versions (`SCHED=linear` vs default `o1`)

- **linear**: PendSV calls `os_sched_select()`, which loops over all TCBs to
  find the highest-priority READY task. Cost grows with the task count.
- **o1**: every READY task sits in a circular list for its priority, and a
  32-bit `s_ready_bitmap` has bit *p* set when list *p* is non-empty. The
  highest ready priority is `31 - __builtin_clz(bitmap)`, which compiles to
  a single `CLZ` instruction. Selection is then a couple of loads, whatever
  the number of tasks. All state and priority changes go through
  `os_task_set_state()` / `os_task_set_prio()` so the lists stay consistent
  (including priority inheritance moving a task between lists).

### Under-load mode (`BENCH_LOAD=1`)

The default run isolates the switch: SysTick paused, no other interrupts,
so worst case = average. With `BENCH_LOAD=1`:

- SysTick keeps running (1 kHz: scans tasks, time-slices the two bench tasks)
- TIM2 fires an extra interrupt at 10 kHz (priority above SysTick and PendSV)

PendSV runs with interrupts masked (`cpsid i`), so an interrupt that
arrives mid-switch waits, then tail-chains in before the next task resumes.
Those samples become the worst case. The printout shows how many timer
IRQs and ticks happened during the run, and how many resumes were skipped
because a tick switched the tasks involuntarily (those aren't yield-to-resume
samples). Look at the histogram: most samples stay at the isolated value,
with a tail of interrupted ones. Quote the loaded worst case as "worst-case
under a 10 kHz interrupt load + 1 kHz tick".

> The emulator I used to test this (QEMU) does not implement the DWT, so
> real cycle counts only come from the board. Don't put estimated numbers
> on the resume; run `make bench` and use what it prints.

## 5. Wiring the MPU-6050 (GY-521) after soldering

| GY-521 | Discovery |
|---|---|
| VCC | 3V |
| GND | GND |
| SCL | PB6 |
| SDA | PB7 |
| AD0 | GND (address 0x68) |
| INT | not used |

Then `make clean && make flash` (without `SIM_IMU`). In GDB:

```
(gdb) imu
g_imu = { present = 1, who_am_i = 104 (0x68), rate_hz = 100, ... }
g_attitude = { roll_deg = ..., pitch_deg = ..., temp_c = ... }
```

`g_imu.rate_hz` is the **measured** number of new samples per second
(counted by the stats task), so you can say "measured 100 Hz" instead of
"configured for 100 Hz". If `present = 0`, check `g_imu.last_err`
(-1 = NACK: no device at 0x68, check wiring/AD0) and `g_i2c_stats`.

## 6. Testing the HardFault handler

```bash
make clean && make FAULT_TEST=1 flash   # 1=div by zero, 2=bus error, 3=undefined instr, 4=bad fn ptr
```

After 3 s the fault task crashes. With GDB attached the handler stops on a
`bkpt`; type `fault`:

```
exception 5 (BusFault) in task 'fault'
PC=0x08001b9a LR=0x08001bd1 ...
CFSR=0x00008200 ... BFAR=0xccccccc0
  BFSR.PRECISERR   precise data bus error (see BFAR)
  BFSR.BFARVALID   BFAR holds the faulting address
faulting line (PC): Line 122 of "src/fault.c"
caller (LR):        Line 254 of "src/main.c"
```

Without a debugger it lights red, resets after ~1 s, and the record
survives the reset in `.noinit` RAM; the next boot keeps the red LED on and
`fault` in GDB shows the previous boot's crash. (For case 4 the PC is the bad
target address itself; the LR shows the line that made the call.)

## 7. Verification checklist (before you call it done)

- [ ] `make` builds with zero warnings
- [ ] green LED toggles every second (scheduler + SysTick + delay_until)
- [ ] `make bench` prints `dwt_ok`-clean numbers; record XX/YY at -O2
- [ ] `make BENCH_FPU=1 bench` gives a larger number (FPU context)
- [ ] `make SCHED=linear bench` ≈ 235 cycles; `make bench` (o1) is lower
- [ ] `make BENCH_LOAD=1 bench`: timer IRQs ≈ 10,000/s × run time, worst case > average
- [ ] `make selftest` prints PASS and the green LED stays on
- [ ] `tasks` in GDB: bench_a/bench_b state 0 (exited), ~5000 switches each
- [ ] `make SIM_IMU=1 flash`: blue LED toggles every 0.5 s, `g_imu.rate_hz == 100`
- [ ] each `FAULT_TEST=1..4` decodes to the right CFSR bit and source line
- [ ] after soldering: `who_am_i == 0x68`, `rate_hz == 100`, roll/pitch follow board tilt
- [ ] (optional) scope/logic analyser on PB6/PB7 to see the 400 kHz I2C bursts

### Troubleshooting

- `make bench` hangs at "running benchmark": press Ctrl-C, then `p g_cs` and `bt`.
  If `g_cs.count` is stuck, check `tasks`.
- OpenOCD can't find the ST-LINK: try another USB cable (some are charge-only),
  or `openocd -f interface/stlink.cfg -f target/stm32f4x.cfg`.
- `arm-none-eabi-gdb` complains about Python on macOS: install the Python version
  it asks for, or use `gdb-multiarch` if you have it.

## 8. Resume line

Fill in XX/YY from `make bench`:

> Built a HAL-free preemptive RTOS on an STM32F407VG (Cortex-M4F) with PendSV
> context switching, SysTick-driven scheduling, task control blocks,
> priority-based scheduling, and mutex/semaphore synchronization; implemented
> a register-level I2C MPU-6050 driver for 100 Hz IMU sampling, with
> DWT-based profiling of context-switch overhead, measuring **XX cycles
> average and YY cycles worst-case over 10,000 switches**, and a custom
> HardFault handler for crash diagnostics.

With the scheduler optimisation, a stronger version:

> …replaced the O(n) scheduler with an O(1) ready-bitmap/CLZ scheduler,
> cutting context-switch latency from 235 to **NN cycles** (DWT-measured
> over 10,000 switches; **WW cycles worst-case under a 10 kHz interrupt
> load**)…

Tip: add the time too (e.g. "XX cycles (≈N ns at 168 MHz)").
`g_cs.avg_ns` and `g_cs.max_ns` give you that.

## 9. Design choices to be ready to defend

- **Why PendSV for the switch?** It has the lowest priority, so it runs only
  after every other ISR has finished (tail-chained). A switch never
  preempts an interrupt handler, and several switch requests collapse into one.
- **Why is SysTick above PendSV?** The tick must never be delayed by a
  switch; it only *requests* one.
- **What does the hardware save vs. the software?** HW: R0-R3, R12, LR, PC,
  xPSR (+S0-S15, FPSCR when FPCA=1). SW (PendSV): R4-R11, EXC_RETURN,
  +S16-S31 if EXC_RETURN bit 4 = 0.
- **Lazy FPU stacking**: HW reserves space for S0-S15 but only writes them if
  the handler touches the FPU. Our `vstmdb s16-s31` triggers that write.
- **Why SVC to start the first task?** Returning from an exception with
  EXC_RETURN 0xFFFFFFFD is the clean way to get into Thread mode on PSP
  with a known register state. CONTROL is zeroed first so no FPU frame is
  left on the old main stack.
- **Scheduler**: O(1). A ready bitmap + `CLZ` picks the highest priority, and
  per-priority circular lists give round-robin. If the head of a level is
  the task being switched out, advance to the next one (yield / time slice).
  If a higher priority preempted it, the head stays, so the preempted task
  resumes first. The old O(n) scan is kept (`SCHED=linear`) for comparison.
  Remaining O(n) part: SysTick still scans all tasks to wake sleepers. A
  sorted delta list would fix that.
- **Why mask all interrupts in PendSV?** Simple and safe on a single core,
  but it adds interrupt latency (visible in `BENCH_LOAD=1`). FreeRTOS masks
  only up to a configurable BASEPRI so high-priority ISRs are never delayed.
- **STM32 timer ISR gotcha**: clear the flag at the START of the ISR. The
  write takes a few cycles to reach the peripheral, and clearing it as the
  last instruction can re-trigger the interrupt.
- **Priority inheritance**: basic, single-level. It drops straight back to
  base priority on unlock (correct when a task holds one contended mutex at a time).
- **Deferred (future work)**: stack overflow detection (MPU guard region
  or canaries), watchdog-based task health, DMA I2C, bootloader with image
  validation.
