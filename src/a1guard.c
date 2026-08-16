/*
 * a1guard - game keep-alive daemon for A1-内存管理 [HyperOS4]
 *
 * Lightweight root daemon. While a protected game is running and the user
 * switches to another app, it pins the game's oom_score_adj to a foreground
 * level so LMKD targets other cached processes first, and optionally clears
 * unrelated cached apps when the system is under real memory pressure.
 *
 * When no protected game is running the daemon only scans /proc once per
 * idle interval, so its idle cost is negligible.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MOD_DIR      "/data/adb/modules/a1memory_hyperos4"
#define CFG_GUARD    MOD_DIR "/config/guard.conf"
#define CFG_GAME     MOD_DIR "/config/game.conf"
#define CFG_WHITE    MOD_DIR "/config/whitelist.conf"
#define PID_FILE     "/data/local/tmp/a1guard.pid"
#define LRU_FILE     "/data/local/tmp/a1guard.lru"

#define MAX_PKGS   64
#define MAX_PROCS  512
#define CMDLINE_SZ 256
#define LINE_SZ    512

#define FG_ADJ_MAX     250   /* process considered "in foreground" below this */

struct proc {
    int  pid;
    char name[CMDLINE_SZ];
};

struct config {
    int    enable;
    int    poll_ms;
    int    idle_poll_ms;
    int    protect_minutes;
    int    pin_adj_main;
    int    pin_adj_child;
    int    cleanup;
    double psi_threshold;
    int    cleanup_interval_ms;
    int    cleanup_cooldown_ms;
    int    cleanup_max_kill;
    int    log_enable;
    char   log_path[256];
};

static struct config cfg = {
    .enable = 1,
    .poll_ms = 2000,
    .idle_poll_ms = 5000,
    .protect_minutes = 30,
    .pin_adj_main = 0,
    .pin_adj_child = 200,
    .cleanup = 1,
    .psi_threshold = 40.0,
    .cleanup_interval_ms = 15000,
    .cleanup_cooldown_ms = 60000,
    .cleanup_max_kill = 8,
    .log_enable = 1,
    .log_path = "/data/local/tmp/a1guard.log",
};

static char games[MAX_PKGS][128];
static int  games_n;
static char white[MAX_PKGS][128];
static int  white_n;
static long long last_fg[MAX_PKGS];
static int  exempted[MAX_PKGS];

static long long cfg_mtime, game_mtime, white_mtime;
static long long last_cleanup_pass;
static long long last_cleanup;
static int  log_fd = -1;

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static long long file_mtime(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (long long)st.st_mtime;
}

static void log_msg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;

    if (!cfg.log_enable)
        return;
    if (log_fd < 0)
        log_fd = open(cfg.log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (log_fd < 0)
        return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    n = snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d ",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - (size_t)n, fmt, ap);
    va_end(ap);
    buf[n] = '\n';
    ssize_t w = write(log_fd, buf, (size_t)n + 1);
    (void)w;

    struct stat st;
    if (fstat(log_fd, &st) == 0 && st.st_size > 256 * 1024) {
        int tr = ftruncate(log_fd, 0);
        (void)tr;
    }
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                     s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
    return s;
}

static void parse_guard_conf(void)
{
    FILE *f = fopen(CFG_GUARD, "r");
    char line[256];

    cfg = (struct config){
        .enable = 1, .poll_ms = 2000, .idle_poll_ms = 5000,
        .protect_minutes = 30, .pin_adj_main = 0, .pin_adj_child = 200,
        .cleanup = 1, .psi_threshold = 40.0,
        .cleanup_interval_ms = 15000, .cleanup_cooldown_ms = 60000,
        .cleanup_max_kill = 8, .log_enable = 1,
        .log_path = "/data/local/tmp/a1guard.log",
    };
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (*s == 0 || *s == '#')
            continue;
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *key = trim(s);
        char *val = trim(eq + 1);
        if (!strcmp(key, "enable")) cfg.enable = atoi(val);
        else if (!strcmp(key, "poll_ms")) cfg.poll_ms = atoi(val);
        else if (!strcmp(key, "idle_poll_ms")) cfg.idle_poll_ms = atoi(val);
        else if (!strcmp(key, "protect_minutes")) cfg.protect_minutes = atoi(val);
        else if (!strcmp(key, "pin_adj_main")) cfg.pin_adj_main = atoi(val);
        else if (!strcmp(key, "pin_adj_child")) cfg.pin_adj_child = atoi(val);
        else if (!strcmp(key, "cleanup")) cfg.cleanup = atoi(val);
        else if (!strcmp(key, "psi_threshold")) cfg.psi_threshold = atof(val);
        else if (!strcmp(key, "cleanup_interval_ms")) cfg.cleanup_interval_ms = atoi(val);
        else if (!strcmp(key, "cleanup_cooldown_ms")) cfg.cleanup_cooldown_ms = atoi(val);
        else if (!strcmp(key, "cleanup_max_kill")) cfg.cleanup_max_kill = atoi(val);
        else if (!strcmp(key, "log")) cfg.log_enable = atoi(val);
        else if (!strcmp(key, "log_path")) {
            snprintf(cfg.log_path, sizeof cfg.log_path, "%s", val);
        }
    }
    fclose(f);
    if (cfg.poll_ms < 500) cfg.poll_ms = 500;
    if (cfg.idle_poll_ms < 1000) cfg.idle_poll_ms = 1000;
    if (cfg.protect_minutes < 1) cfg.protect_minutes = 1;
    if (cfg.pin_adj_main < 0) cfg.pin_adj_main = 0;
    if (cfg.pin_adj_main > 1000) cfg.pin_adj_main = 1000;
    if (cfg.pin_adj_child < 0) cfg.pin_adj_child = 0;
    if (cfg.pin_adj_child > 1000) cfg.pin_adj_child = 1000;
    if (cfg.cleanup_interval_ms < 5000) cfg.cleanup_interval_ms = 5000;
    if (cfg.cleanup_cooldown_ms < 10000) cfg.cleanup_cooldown_ms = 10000;
    if (cfg.cleanup_max_kill < 1) cfg.cleanup_max_kill = 1;
    if (cfg.cleanup_max_kill > 32) cfg.cleanup_max_kill = 32;
}

static int load_list(const char *path, char list[][128], int max)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int n = 0;

    if (!f)
        return 0;
    while (n < max && fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (*s == 0 || *s == '#')
            continue;
        char *p = strchr(s, ' ');
        if (p)
            *p = 0;
        bool ok = true;
        for (const char *c = s; *c; c++) {
            char ch = *c;
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
                  ch == ':' || ch == '-'))
                ok = false;
        }
        if (*s && ok)
            snprintf(list[n++], 128, "%s", s);
        else if (*s)
            log_msg("ignored invalid package entry: %s", s);
    }
    fclose(f);
    return n;
}

static void reload_lists(void)
{
    games_n = load_list(CFG_GAME, games, MAX_PKGS);
    white_n = load_list(CFG_WHITE, white, MAX_PKGS);
    memset(last_fg, 0, sizeof last_fg);
    memset(exempted, 0, sizeof exempted);
    log_msg("config loaded: %d game(s), %d whitelisted, cleanup=%s",
            games_n, white_n, cfg.cleanup ? "on" : "off");
}

static bool in_list(const char *name, char list[][128], int n)
{
    for (int i = 0; i < n; i++) {
        size_t len = strlen(list[i]);
        if (strncmp(name, list[i], len) == 0 &&
            (name[len] == 0 || name[len] == ':'))
            return true;
    }
    return false;
}

static bool is_protected(const char *name, int *idx)
{
    for (int i = 0; i < games_n; i++) {
        size_t len = strlen(games[i]);
        if (strncmp(name, games[i], len) == 0 &&
            (name[len] == 0 || name[len] == ':')) {
            *idx = i;
            return true;
        }
    }
    return false;
}

static int read_adj(int pid)
{
    char path[64], buf[32];
    int fd, r;
    snprintf(path, sizeof path, "/proc/%d/oom_score_adj", pid);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    r = (int)read(fd, buf, sizeof buf - 1);
    close(fd);
    if (r <= 0)
        return -1;
    buf[r] = 0;
    return atoi(buf);
}

static void write_adj(int pid, int adj)
{
    char path[64], buf[16];
    int fd, len;
    snprintf(path, sizeof path, "/proc/%d/oom_score_adj", pid);
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    len = snprintf(buf, sizeof buf, "%d", adj);
    ssize_t w = write(fd, buf, (size_t)len);
    (void)w;
    close(fd);
}

static int scan_procs(struct proc *out, int max)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    int n = 0;

    if (!d)
        return 0;
    while ((e = readdir(d)) != NULL && n < max) {
        char *end;
        long pid = strtol(e->d_name, &end, 10);
        char path[320], buf[CMDLINE_SZ];
        int fd, r;
        if (*end != 0)
            continue;
        snprintf(path, sizeof path, "/proc/%s/cmdline", e->d_name);
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            continue;
        r = (int)read(fd, buf, sizeof buf - 1);
        close(fd);
        if (r <= 0)
            continue;
        buf[r] = 0;
        if (buf[0] == 0)
            continue;
        out[n].pid = (int)pid;
        snprintf(out[n].name, sizeof out[n].name, "%s", buf);
        n++;
    }
    closedir(d);
    return n;
}

static void apply_exemptions(int pkg_idx)
{
    char cmd[256];
    const char *pkg = games[pkg_idx];
    int rc;

    snprintf(cmd, sizeof cmd, "cmd deviceidle whitelist +%s >/dev/null 2>&1", pkg);
    rc = system(cmd);
    (void)rc;
    snprintf(cmd, sizeof cmd, "appops set %s RUN_IN_BACKGROUND allow >/dev/null 2>&1", pkg);
    rc = system(cmd);
    (void)rc;
    snprintf(cmd, sizeof cmd, "appops set %s RUN_ANY_IN_BACKGROUND allow >/dev/null 2>&1", pkg);
    rc = system(cmd);
    (void)rc;
    snprintf(cmd, sizeof cmd, "am set-standby-bucket %s active >/dev/null 2>&1", pkg);
    rc = system(cmd);
    (void)rc;
    exempted[pkg_idx] = 1;
    log_msg("exemptions applied: %s", pkg);
}

static void pin_processes(struct proc *procs, int n, int pkg_idx)
{
    const char *pkg = games[pkg_idx];
    int main_len = (int)strlen(pkg);

    for (int i = 0; i < n; i++) {
        const char *name = procs[i].name;
        if (strncmp(name, pkg, (size_t)main_len) != 0)
            continue;
        if (name[main_len] != 0 && name[main_len] != ':')
            continue;
        int target = name[main_len] == ':' ? cfg.pin_adj_child : cfg.pin_adj_main;
        int cur = read_adj(procs[i].pid);
        if (cur >= 0 && cur != target)
            write_adj(procs[i].pid, target);
    }
}

static double read_psi_avg60(void)
{
    FILE *f = fopen("/proc/pressure/memory", "r");
    char line[256];
    double avg60 = -1.0;

    if (!f)
        return -1.0;
    while (fgets(line, sizeof line, f)) {
        char kind[8];
        double a10, a60, a300;
        unsigned long long total;
        if (sscanf(line, "%7s avg10=%lf avg60=%lf avg300=%lf total=%llu",
                   kind, &a10, &a60, &a300, &total) == 5 &&
            strcmp(kind, "some") == 0) {
            avg60 = a60;
            break;
        }
    }
    fclose(f);
    return avg60;
}

static void run_cleanup(void)
{
    long long now = now_ms();
    int kills = 0;
    FILE *f;
    char line[LINE_SZ];

    if (!cfg.cleanup)
        return;
    if (now - last_cleanup_pass < cfg.cleanup_interval_ms)
        return;
    last_cleanup_pass = now;
    if (now - last_cleanup < cfg.cleanup_cooldown_ms)
        return;

    double psi = read_psi_avg60();
    if (psi < 0) {
        log_msg("cleanup: /proc/pressure/memory unavailable, disabled");
        cfg.cleanup = 0;
        return;
    }
    if (psi < cfg.psi_threshold)
        return;

    int rc = system("dumpsys activity lru > " LRU_FILE " 2>/dev/null");
    (void)rc;
    f = fopen(LRU_FILE, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f) && kills < cfg.cleanup_max_kill) {
        char *p = strchr(line, ':');
        char *save = NULL, *tok;
        long pid;
        char name[CMDLINE_SZ];
        if (!p)
            continue;
        p++;
        tok = strtok_r(p, " \t\r\n", &save);
        if (!tok || strncmp(tok, "cch", 3) != 0)
            continue;
        while ((tok = strtok_r(NULL, " \t\r\n", &save)) != NULL) {
            char *colon = strchr(tok, ':');
            if (!colon || tok[0] < '0' || tok[0] > '9')
                continue;
            pid = atol(tok);
            snprintf(name, sizeof name, "%s", colon + 1);
            char *slash = strchr(name, '/');
            char *uid = NULL;
            if (slash) {
                *slash = 0;
                uid = slash + 1;
            }
            if (!uid || !(uid[0] == 'u' && uid[1] >= '0' && uid[1] <= '9'))
                continue;   /* system / shell processes: skip */
            int gi;
            if (is_protected(name, &gi))
                continue;
            if (in_list(name, white, white_n))
                continue;
            kill((pid_t)pid, SIGKILL);
            kills++;
            log_msg("cleanup: killed cached %s (pid %ld, psi_avg60 %.1f)",
                    name, pid, psi);
            break;
        }
    }
    fclose(f);
    last_cleanup = now;
    if (kills > 0)
        log_msg("cleanup: %d process(es) killed", kills);
}

static void on_term(int sig)
{
    (void)sig;
    if (log_fd >= 0)
        close(log_fd);
    unlink(PID_FILE);
    _exit(0);
}

int main(void)
{
    struct proc procs[MAX_PROCS];
    FILE *pf;

#ifndef A1GUARD_TEST
    if (getuid() != 0) {
        fprintf(stderr, "a1guard: must run as root\n");
        return 1;
    }
#endif

    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);

    pf = fopen(PID_FILE, "w");
    if (pf) {
        fprintf(pf, "%d\n", getpid());
        fclose(pf);
    }

    cfg_mtime = file_mtime(CFG_GUARD);
    game_mtime = file_mtime(CFG_GAME);
    white_mtime = file_mtime(CFG_WHITE);
    parse_guard_conf();
    reload_lists();

    for (;;) {
        long long cm = file_mtime(CFG_GUARD);
        long long gm = file_mtime(CFG_GAME);
        long long wm = file_mtime(CFG_WHITE);
        if (cm != cfg_mtime || gm != game_mtime || wm != white_mtime) {
            cfg_mtime = cm; game_mtime = gm; white_mtime = wm;
            if (log_fd >= 0) {
                close(log_fd);
                log_fd = -1;
            }
            parse_guard_conf();
            reload_lists();
        }

        bool any_bg = false;
        if (cfg.enable && games_n > 0) {
            int n = scan_procs(procs, MAX_PROCS);
            for (int gi = 0; gi < games_n; gi++) {
                bool running = false, fg = false;
                int main_len = (int)strlen(games[gi]);
                for (int i = 0; i < n; i++) {
                    const char *name = procs[i].name;
                    if (strncmp(name, games[gi], (size_t)main_len) != 0)
                        continue;
                    if (name[main_len] != 0 && name[main_len] != ':')
                        continue;
                    running = true;
                    int adj = read_adj(procs[i].pid);
                    if (adj >= 0 && adj <= FG_ADJ_MAX)
                        fg = true;
                }
                if (!running)
                    continue;
                if (fg) {
                    last_fg[gi] = now_ms();
                    continue;
                }
                long long win = (long long)cfg.protect_minutes * 60 * 1000;
                if (now_ms() - last_fg[gi] > win)
                    continue;
                any_bg = true;
                if (!exempted[gi])
                    apply_exemptions(gi);
                pin_processes(procs, n, gi);
            }
        }

        if (any_bg)
            run_cleanup();

        usleep((useconds_t)(cfg.enable ? cfg.poll_ms : cfg.idle_poll_ms) * 1000);
    }
}
