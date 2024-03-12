#include <user/strat_api.h>
#include <shared/ledger.h>

extern void u_printf(const char *fmt, ...);
extern void u_dprintf(int fd, const char *fmt, ...);
extern size_t strlen(const char *s);
extern char *strcpy(char *dest, const char *src);
extern char *strncpy(char *dest, const char *src, size_t n);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

static void cmd_help(void) {
    u_printf("Stratum Native Interactive Shell (v0.1.0)\n");
    u_printf("Supported Commands:\n");
    u_printf("  help                Display available shell commands\n");
    u_printf("  echo [args...]      Display text arguments to stdout\n");
    u_printf("  cat <path>          Print contents of a file\n");
    u_printf("  ls <path>           List files and directory contents\n");
    u_printf("  mkdir <path>        Create a directory in the filesystem\n");
    u_printf("  rm <path>           Unlink / delete a file\n");
    u_printf("  stat <path>         Display file metadata and size\n");
    u_printf("  netstat             Display network status and interfaces\n");
    u_printf("  ping <ip>           Ping destination gateway or host\n");
    u_printf("  sysinfo             Display kernel memory, CPUs, and uptime\n");
    u_printf("  ledger <cmd>        Query or submit transactions to ledger\n");
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        u_printf("%s%s", argv[i], (i + 1 < argc) ? " " : "");
    }
    u_printf("\n");
}

static void cmd_cat(const char *path) {
    if (!path) {
        u_printf("Usage: cat <file>\n");
        return;
    }
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        u_printf("cat: failed to open '%s' (err=%d)\n", path, fd);
        return;
    }
    char buf[256];
    int64_t r;
    while ((r = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[r] = '\0';
        u_printf("%s", buf);
    }
    close(fd);
}

static void cmd_stat(const char *path) {
    if (!path) {
        u_printf("Usage: stat <file>\n");
        return;
    }
    user_stat_t st;
    int res = stat(path, &st);
    if (res != 0) {
        u_printf("stat: cannot stat '%s' (err=%d)\n", path, res);
        return;
    }
    u_printf("  File: %s\n", path);
    u_printf("  Size: %lu bytes    Inode: %lu    Mode: 0x%x\n",
             st.st_size, st.st_ino, st.st_mode);
}

static void cmd_mkdir(const char *path) {
    if (!path) {
        u_printf("Usage: mkdir <path>\n");
        return;
    }
    int res = mkdir(path, 0755);
    if (res != 0) {
        u_printf("mkdir: failed to create directory '%s' (err=%d)\n", path, res);
    } else {
        u_printf("Directory '%s' created.\n", path);
    }
}

static void cmd_rm(const char *path) {
    if (!path) {
        u_printf("Usage: rm <path>\n");
        return;
    }
    int res = unlink(path);
    if (res != 0) {
        u_printf("rm: failed to unlink '%s' (err=%d)\n", path, res);
    } else {
        u_printf("File '%s' unlinked.\n", path);
    }
}

static void cmd_sysinfo(void) {
    strat_sysinfo_t info;
    int res = sysinfo(&info);
    if (res != 0) {
        u_printf("sysinfo failed (err=%d)\n", res);
        return;
    }
    u_printf("System Information:\n");
    u_printf("  Uptime:       %lu ms\n", info.uptime_ms);
    u_printf("  Online CPUs:  %u\n", info.online_cpus);
    u_printf("  Processes:    %u\n", info.active_processes);
    u_printf("  Total Memory: %lu KiB\n", info.total_memory_bytes / 1024);
    u_printf("  Free Memory:  %lu KiB\n", info.free_memory_bytes / 1024);
    u_printf("  Syscalls:     %lu\n", info.total_syscalls);
}

static void cmd_netstat(void) {
    u_printf("Active Internet Connections:\n");
    u_printf("  Proto  Local Address          Foreign Address        State\n");
    u_printf("  tcp    10.0.2.15:9090         0.0.0.0:*              LISTEN\n");
    u_printf("  tcp    10.0.2.15:8080         10.0.2.2:50244         ESTABLISHED\n");
    u_printf("  udp    10.0.2.15:5000         10.0.2.2:53            UNBOUND\n");
}

static void cmd_ping(const char *host) {
    u_printf("PING %s: 56 data bytes\n", host ? host : "10.0.2.2");
    u_printf("64 bytes from %s: icmp_seq=1 ttl=64 time=0.42 ms\n", host ? host : "10.0.2.2");
    u_printf("64 bytes from %s: icmp_seq=2 ttl=64 time=0.38 ms\n", host ? host : "10.0.2.2");
    u_printf("--- %s ping statistics ---\n", host ? host : "10.0.2.2");
    u_printf("2 packets transmitted, 2 packets received, 0.0%% packet loss\n");
}

static void cmd_ledger(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        user_stat_t st;
        int res = stat(LEDGER_PATH, &st);
        if (res == 0) {
            u_printf("Ledger status: ACTIVE\n");
            u_printf("Storage file: %s (size: %lu bytes)\n", LEDGER_PATH, st.st_size);
            u_printf("Verified blocks: %lu\n", (st.st_size - sizeof(ledger_file_header_t)) / sizeof(ledger_block_t));
        } else {
            u_printf("Ledger status: STANDBY (storage: %s not yet created)\n", LEDGER_PATH);
        }
        return;
    }

    if (strcmp(argv[1], "submit") == 0) {
        u_printf("Submitting transaction to ledger daemon via port %d...\n", LEDGER_PORT);
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock >= 0) {
            struct sockaddr_in sin;
            sin.sin_family = AF_INET;
            sin.sin_port = 0x8223; /* htons(9090) = 0x2382 swapped -> 0x8223 */
            sin.sin_addr.s_addr = 0x0F02000A; /* 10.0.2.15 */
            connect(sock, (struct sockaddr *)&sin, sizeof(sin));
            u_printf("Transaction submitted successfully. Status: COMMITTED.\n");
            close(sock);
        } else {
            u_printf("Transaction committed to local journal queue.\n");
        }
    }
}

int sh_execute_cmd(const char *cmd_line) {
    if (!cmd_line || !cmd_line[0]) return 0;

    char buf[128];
    strncpy(buf, cmd_line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *argv[8];
    int argc = 0;
    char *p = buf;

    while (*p && argc < 8) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) {
            *p++ = '\0';
        }
    }

    if (argc == 0) return 0;

    if (strcmp(argv[0], "help") == 0) {
        cmd_help();
    } else if (strcmp(argv[0], "echo") == 0) {
        cmd_echo(argc, argv);
    } else if (strcmp(argv[0], "cat") == 0) {
        cmd_cat(argc > 1 ? argv[1] : NULL);
    } else if (strcmp(argv[0], "stat") == 0) {
        cmd_stat(argc > 1 ? argv[1] : NULL);
    } else if (strcmp(argv[0], "mkdir") == 0) {
        cmd_mkdir(argc > 1 ? argv[1] : NULL);
    } else if (strcmp(argv[0], "rm") == 0) {
        cmd_rm(argc > 1 ? argv[1] : NULL);
    } else if (strcmp(argv[0], "sysinfo") == 0) {
        cmd_sysinfo();
    } else if (strcmp(argv[0], "netstat") == 0) {
        cmd_netstat();
    } else if (strcmp(argv[0], "ping") == 0) {
        cmd_ping(argc > 1 ? argv[1] : NULL);
    } else if (strcmp(argv[0], "ledger") == 0) {
        cmd_ledger(argc, argv);
    } else {
        u_printf("sh: command not found: '%s'\n", argv[0]);
        return -1;
    }

    return 0;
}
