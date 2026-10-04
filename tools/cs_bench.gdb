# tools/cs_bench.gdb - used by `make bench`.
# Flashes the ELF, runs until the benchmark finishes, prints XX / YY,
# dumps the raw samples for tools/analyze_cs.py, then lets the firmware run on.
set pagination off
set confirm off
set print pretty on

target extended-remote :3333
monitor reset halt
load
monitor reset halt

break cs_bench_complete
echo \n>>> running benchmark (should take well under a second)...\n
continue

if g_cs.dwt_ok == 0
  echo \n!!! WARNING: DWT CYCCNT was not counting - numbers are meaningless.\n
end

printf "\n================ CONTEXT SWITCH BENCHMARK ================\n"
printf "  samples          : %u task-to-task switches\n", g_cs.count
printf "  CPU clock        : %u Hz\n", g_cs.cpu_hz
printf "  scheduler        : %s\n", g_cs.sched_o1 ? "O(1) bitmap + CLZ" : "linear scan O(n)"
printf "  FPU frames       : %u  (1 = both tasks had FPU context)\n", g_cs.fpu_frames
if g_cs.load_mode
  printf "  LOAD MODE        : SysTick on + TIM2 @ %u Hz -> %u timer IRQs, %u ticks, %u involuntary resumes skipped\n", g_cs.load_hz, g_cs.load_irqs, g_cs.ticks, g_cs.skipped
else
  printf "  mode             : isolated (SysTick paused, no other IRQs)\n"
end
printf "  calibration      : %u cycles subtracted per sample (CYCCNT read)\n", g_cs.calib
printf "  ---------------------------------------------------------\n"
printf "  AVERAGE  (XX)    : %u.%02u cycles  = %u ns\n", g_cs.avg_x100 / 100, g_cs.avg_x100 % 100, g_cs.avg_ns
printf "  WORST    (YY)    : %u cycles  = %u ns   (sample #%u)\n", g_cs.max, g_cs.max_ns, g_cs.max_index
printf "  best             : %u cycles\n", g_cs.min
printf "==========================================================\n\n"

dump binary memory build/cs_samples.bin &g_cs_samples[0] &g_cs_samples[g_cs.count]
echo raw samples written to build/cs_samples.bin\n

delete
monitor resume
detach
quit
