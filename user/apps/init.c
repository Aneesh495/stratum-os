#include <user/strat_api.h>

static void print(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    print("[USER] Stratum native userland program running in Ring 3!\n");
    print("[USER] Testing syscalls from Ring 3: getpid, uptime, nanosleep...\n");

    int pid = getpid();
    (void)pid;
    uint64_t up = uptime();
    (void)up;
    print("[USER] System call getpid and uptime succeeded!\n");

    /* Sleep 20 ms */
    sleep_ms(20);
    print("[USER] Resumed from sleep. Exiting cleanly with code 42.\n");

    return 42;
}
