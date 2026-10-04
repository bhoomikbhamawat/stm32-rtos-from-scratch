# tools/selftest.gdb - used by `make selftest`
set pagination off
set confirm off
set print pretty on
target extended-remote :3333
monitor reset halt
load
monitor reset halt
break selftest_complete
continue
printf "\n================ KERNEL SELF-TEST ================\n"
printf "  priority inheritance : L prio while holding = %u (expect 3), after unlock = %u (expect 1)\n", g_selftest.L_prio_while_holding, g_selftest.L_prio_after_unlock
printf "  no inversion         : H got mutex at tick %u, L unlocked at %u, M finished at %u (H must be before M)\n", g_selftest.H_lock_tick, g_selftest.L_unlock_tick, g_selftest.M_done_tick
printf "  semaphore timeout    : rc = %d (expect -1) after %u ticks (expect 7, 8 if a tick lands mid-call)\n", g_selftest.sem_rc, g_selftest.sem_wait_ticks
printf "  give / take          : %d / %d (expect 0 / 0)\n", g_selftest.sem_give_rc, g_selftest.sem_take_rc
printf "  RESULT               : %s\n", g_selftest.pass ? "PASS" : "FAIL"
printf "==================================================\n\n"
delete
monitor resume
detach
quit
