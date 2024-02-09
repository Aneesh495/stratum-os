#include <user/strat_api.h>

void exit(int status) {
    syscall1(SYS_exit, (uint64_t)status);
    while (1) {
        __asm__ volatile("pause");
    }
}
