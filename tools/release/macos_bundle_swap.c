#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <libproc.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>

#define LOCK_OWNER "owner"
#define LOCK_GUARD ".macos-update.guard"

struct lock_owner {
    pid_t pid;
    uint64_t start_sec;
    uint64_t start_usec;
    char token[81];
};

static int canonical_directory(const char *path, char resolved[PATH_MAX]) {
    if (path[0] != '/' || strlen(path) >= PATH_MAX || strstr(path, "/../") != NULL ||
        strstr(path, "/./") != NULL || strcmp(path, "/..") == 0 || strcmp(path, "/.") == 0) {
        errno = EINVAL;
        return -1;
    }
    char current[PATH_MAX] = "/";
    const char *component = path + 1;
    while (*component != '\0') {
        const char *slash = strchr(component, '/');
        size_t length = slash == NULL ? strlen(component) : (size_t)(slash - component);
        if (length == 0 || length >= NAME_MAX) {
            errno = EINVAL;
            return -1;
        }
        size_t used = strlen(current);
        if (used + length + (used > 1 ? 1 : 0) >= sizeof(current)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (used > 1)
            current[used++] = '/';
        memcpy(current + used, component, length);
        current[used + length] = '\0';
        struct stat st;
        if (lstat(current, &st) != 0 || S_ISLNK(st.st_mode)) {
            if (errno == 0)
                errno = ELOOP;
            return -1;
        }
        if (slash == NULL)
            break;
        component = slash + 1;
    }
    if (realpath(path, resolved) == NULL)
        return -1;
    if (strcmp(path, resolved) != 0) {
        errno = ELOOP;
        return -1;
    }
    struct stat st;
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

static int split_directory(const char *path, char parent[PATH_MAX], const char **name) {
    const char *slash = strrchr(path, '/');
    if (path[0] != '/' || slash == NULL || slash == path || slash[1] == '\0' ||
        strcmp(slash + 1, ".") == 0 || strcmp(slash + 1, "..") == 0 ||
        strlen(path) >= PATH_MAX) {
        errno = EINVAL;
        return -1;
    }
    char prefix[PATH_MAX];
    size_t length = (size_t)(slash - path);
    memcpy(prefix, path, length);
    prefix[length] = '\0';
    if (canonical_directory(prefix, parent) != 0)
        return -1;
    *name = slash + 1;
    return 0;
}

static int process_start(pid_t pid, uint64_t *start_sec, uint64_t *start_usec, uid_t *uid,
                         uint32_t *status) {
    struct proc_bsdinfo info;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info))
        return -1;
    *start_sec = info.pbi_start_tvsec;
    *start_usec = info.pbi_start_tvusec;
    *uid = info.pbi_uid;
    *status = info.pbi_status;
    return 0;
}

static int process_status(pid_t pid, uint32_t *status) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    struct kinfo_proc info;
    size_t length = sizeof(info);
    if (sysctl(mib, 4, &info, &length, NULL, 0) != 0 || length != sizeof(info))
        return -1;
    *status = (uint32_t)info.kp_proc.p_stat;
    return 0;
}

static int process_alive(pid_t pid) {
    if (pid <= 0)
        return 2;
    uint64_t sec, usec;
    uid_t uid;
    uint32_t status;
    int has_info = process_start(pid, &sec, &usec, &uid, &status) == 0;
    if (!has_info && process_status(pid, &status) == 0 && status == SZOMB)
        return 1;
    if (has_info && status == SZOMB)
        return 1;
    if (kill(pid, 0) == 0 || errno == EPERM)
        return has_info ? 0 : 2;
    return errno == ESRCH ? 1 : 2;
}

static int valid_token(const char *token) {
    size_t length = strlen(token);
    if (length == 0 || length > 80)
        return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)token[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.'))
            return 0;
    }
    return 1;
}

static int parse_owner(const char *text, struct lock_owner *owner) {
    long pid;
    unsigned long long sec, usec;
    char token[81], extra;
    if (sscanf(text, "%ld %llu %llu %80s %c", &pid, &sec, &usec, token, &extra) != 4 ||
        pid <= 0 || pid > INT32_MAX || !valid_token(token))
        return -1;
    owner->pid = (pid_t)pid;
    owner->start_sec = (uint64_t)sec;
    owner->start_usec = (uint64_t)usec;
    strlcpy(owner->token, token, sizeof(owner->token));
    return 0;
}

static int read_owner(int parent, const char *directory, struct lock_owner *owner) {
    int dir = openat(parent, directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir < 0)
        return -1;
    int file = openat(dir, LOCK_OWNER, O_RDONLY | O_NOFOLLOW);
    if (file < 0) {
        close(dir);
        return -1;
    }
    struct stat st;
    if (fstat(file, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        st.st_size >= (off_t)sizeof(char[256])) {
        close(file);
        close(dir);
        errno = EINVAL;
        return -1;
    }
    char text[256];
    ssize_t length = read(file, text, sizeof(text) - 1);
    int saved = errno;
    close(file);
    close(dir);
    errno = saved;
    if (length <= 0 || length >= (ssize_t)sizeof(text) - 1)
        return -1;
    text[length] = '\0';
    if (parse_owner(text, owner) != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/* 0 absent, 3 live owner, 4 dead owner, 2 unreadable/unknown. */
static int lock_state(int parent, const char *directory, struct lock_owner *owner) {
    struct stat st;
    if (fstatat(parent, directory, &st, AT_SYMLINK_NOFOLLOW) != 0)
        return errno == ENOENT ? 0 : 2;
    if (!S_ISDIR(st.st_mode) || read_owner(parent, directory, owner) != 0)
        return 2;
    uint64_t sec, usec;
    uid_t uid;
    uint32_t status;
    int has_info = process_start(owner->pid, &sec, &usec, &uid, &status) == 0;
    if (!has_info && process_status(owner->pid, &status) == 0 && status == SZOMB)
        return 4;
    if ((has_info && status == SZOMB) || (kill(owner->pid, 0) != 0 && errno == ESRCH))
        return 4;
    if (!has_info)
        return 2;
    return uid == getuid() && sec == owner->start_sec && usec == owner->start_usec ? 3 : 4;
}

static int acquire_guard(int parent) {
    int fd = openat(parent, LOCK_GUARD, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || flock(fd, LOCK_EX) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int same_owner(const struct lock_owner *left, const struct lock_owner *right) {
    return left->pid == right->pid && left->start_sec == right->start_sec &&
           left->start_usec == right->start_usec && strcmp(left->token, right->token) == 0;
}

static int remove_lock_directory(int parent, const char *name,
                                 const struct lock_owner *expected, int require_dead) {
    int dir = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir < 0)
        return -1;
    struct lock_owner owner;
    struct lock_owner current;
    int state = lock_state(parent, name, &current);
    struct stat opened, named;
    if (read_owner(parent, name, &owner) != 0 || !same_owner(&owner, expected) ||
        state != (require_dead ? 4 : 3) || !same_owner(&current, expected) ||
        fstat(dir, &opened) != 0 || fstatat(parent, name, &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISDIR(named.st_mode) || opened.st_dev != named.st_dev || opened.st_ino != named.st_ino) {
        close(dir);
        errno = EBUSY;
        return -1;
    }
    if (unlinkat(dir, LOCK_OWNER, 0) != 0 || unlinkat(parent, name, AT_REMOVEDIR) != 0) {
        close(dir);
        return -1;
    }
    close(dir);
    return fsync(parent);
}

static int publish_lock_directory(int parent, const char *directory, pid_t pid,
                                  const char *token, const struct lock_owner *owner) {
    char temporary[NAME_MAX];
    snprintf(temporary, sizeof(temporary), ".macos-update-lock-%ld-%08x",
             (long)pid, arc4random());
    if (mkdirat(parent, temporary, 0700) != 0)
        return 2;
    int dir = openat(parent, temporary, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (dir < 0) {
        unlinkat(parent, temporary, AT_REMOVEDIR);
        return 2;
    }
    int file = openat(dir, LOCK_OWNER, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    char text[256];
    int length = snprintf(text, sizeof(text), "%ld %llu %llu %s\n", (long)pid,
                          (unsigned long long)owner->start_sec,
                          (unsigned long long)owner->start_usec, token);
    int ok = file >= 0 && write(file, text, (size_t)length) == length &&
             fsync(file) == 0 && fsync(dir) == 0;
    if (file >= 0)
        close(file);
    close(dir);
    if (!ok) {
        int cleanup = openat(parent, temporary, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (cleanup >= 0) {
            unlinkat(cleanup, LOCK_OWNER, 0);
            close(cleanup);
        }
        unlinkat(parent, temporary, AT_REMOVEDIR);
        return 2;
    }
    if (renameatx_np(parent, temporary, parent, directory, RENAME_EXCL) != 0) {
        int cleanup = openat(parent, temporary, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (cleanup >= 0) {
            unlinkat(cleanup, LOCK_OWNER, 0);
            close(cleanup);
        }
        unlinkat(parent, temporary, AT_REMOVEDIR);
        return errno == EEXIST ? 3 : 2;
    }
    if (fsync(parent) != 0)
        return 5;
    return 0;
}

static int lock_command(const char *operation, const char *path, pid_t pid, const char *token) {
    char parent_path[PATH_MAX];
    const char *directory;
    if (split_directory(path, parent_path, &directory) != 0) {
        perror("bundle swap: lock path");
        return 2;
    }
    int parent = open(parent_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent < 0)
        return 2;
    int guard = acquire_guard(parent);
    if (guard < 0) {
        perror("bundle swap: lock guard");
        close(parent);
        return 2;
    }
    struct lock_owner owner;
    int state = lock_state(parent, directory, &owner);
    int result = 2;
    if (strcmp(operation, "status") == 0) {
        if (state == 3 || state == 4)
            printf("%ld %llu %llu %s\n", (long)owner.pid,
                   (unsigned long long)owner.start_sec,
                   (unsigned long long)owner.start_usec, owner.token);
        result = state;
    } else if (strcmp(operation, "acquire") == 0) {
        uint64_t sec, usec;
        uid_t uid;
        uint32_t status;
        struct lock_owner current;
        if (!valid_token(token) || pid <= 0 ||
            process_start(pid, &sec, &usec, &uid, &status) != 0 || status == SZOMB ||
            uid != getuid()) {
            fprintf(stderr, "bundle swap: lock owner must be this live process\n");
        } else if (state == 3) {
            result = 3;
        } else if (state == 2) {
            fprintf(stderr, "bundle swap: existing lock owner is unreadable; refusing recovery\n");
        } else {
            if (state == 4 && remove_lock_directory(parent, directory, &owner, 1) != 0) {
                fprintf(stderr, "bundle swap: stale lock could not be safely removed\n");
            } else {
                current.pid = pid;
                current.start_sec = sec;
                current.start_usec = usec;
                strlcpy(current.token, token, sizeof(current.token));
                result = publish_lock_directory(parent, directory, pid, token, &current);
            }
        }
    } else if (strcmp(operation, "release") == 0) {
        uint64_t sec, usec;
        uid_t uid;
        uint32_t status;
        if (state == 0) {
            result = 0;
        } else if (state == 3 && owner.pid == pid && strcmp(owner.token, token) == 0 &&
                   process_start(pid, &sec, &usec, &uid, &status) == 0 && status != SZOMB &&
                   uid == getuid() &&
                   owner.start_sec == sec && owner.start_usec == usec) {
            result = remove_lock_directory(parent, directory, &owner, 0) == 0 ? 0 : 2;
        } else {
            fprintf(stderr, "bundle swap: refusing to release a lock owned by another transaction\n");
        }
    }
    flock(guard, LOCK_UN);
    close(guard);
    close(parent);
    return result;
}

static int canonical_command(const char *path) {
    char resolved[PATH_MAX];
    if (canonical_directory(path, resolved) != 0) {
        perror("bundle swap: non-canonical or symlinked path");
        return 2;
    }
    puts(resolved);
    return 0;
}

static int game_running(const char *path, pid_t excluded) {
    char root[PATH_MAX];
    if (canonical_directory(path, root) != 0) {
        perror("bundle swap: install root");
        return -1;
    }
    int count = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    if (count <= 0) {
        perror("bundle swap: process list");
        return -1;
    }
    pid_t *pids = calloc((size_t)count + 32, sizeof(pid_t));
    if (pids == NULL) {
        perror("bundle swap: process list allocation");
        return -1;
    }
    count = proc_listpids(PROC_ALL_PIDS, 0, pids, (int)(((size_t)count + 32) * sizeof(pid_t)));
    if (count < 0) {
        perror("bundle swap: process list");
        free(pids);
        return -1;
    }
    size_t root_length = strlen(root);
    int busy = 0;
    for (int i = 0; i < count / (int)sizeof(pid_t); ++i) {
        pid_t pid = pids[i];
        if (pid <= 0 || pid == getpid() || pid == excluded)
            continue;
        struct proc_bsdinfo info;
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info) ||
            info.pbi_uid != getuid())
            continue;
        char executable[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(pid, executable, sizeof(executable)) <= 0)
            continue;
        if (strncmp(executable, root, root_length) == 0 &&
            executable[root_length] == '/' &&
            (strstr(executable + root_length, "/OpenBFME.app/Contents/MacOS/OpenBFME") != NULL ||
             strstr(executable + root_length,
                    "/OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher") != NULL)) {
            busy = 1;
            break;
        }
    }
    free(pids);
    return busy;
}

static int process_under(pid_t pid, const char *path) {
    char root[PATH_MAX];
    if (canonical_directory(path, root) != 0) {
        perror("bundle swap: process root");
        return -1;
    }
    struct proc_bsdinfo info;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info) ||
        info.pbi_uid != getuid())
        return 0;
    char executable[PROC_PIDPATHINFO_MAXSIZE];
    if (proc_pidpath(pid, executable, sizeof(executable)) <= 0)
        return 0;
    size_t root_length = strlen(root);
    return strncmp(executable, root, root_length) == 0 &&
           executable[root_length] == '/';
}

/* MACPLAY-1 (Sol r2, P1): `restore` (--restore-if-idle) puts a verified previous package back: the package that will be at the
 * install path afterwards (the staged sibling) must be complete; the damaged one at the install path may lack either app (an
 * interrupted update whose new OpenBFME.app was removed stranded the install beside its verified backup). The forward exchange keeps
 * requiring a complete package at the install path. */
static int swap_directories(const char *installed, const char *staged, int restore) {
    char left_parent[PATH_MAX], right_parent[PATH_MAX];
    const char *left, *right;
    if (split_directory(installed, left_parent, &left) != 0 ||
        split_directory(staged, right_parent, &right) != 0) {
        perror("bundle swap: directory path");
        return 1;
    }
    char canonical_installed[PATH_MAX], canonical_staged[PATH_MAX];
    if (snprintf(canonical_installed, sizeof(canonical_installed), "%s/%s", left_parent, left) >=
            (int)sizeof(canonical_installed) ||
        snprintf(canonical_staged, sizeof(canonical_staged), "%s/%s", right_parent, right) >=
            (int)sizeof(canonical_staged) ||
        strcmp(canonical_installed, installed) != 0 || strcmp(canonical_staged, staged) != 0) {
        fprintf(stderr, "bundle swap: symlinked or non-canonical package path\n");
        return 1;
    }
    size_t left_length = strlen(left);
    const char *root_suffix = "-macos-arm64";
    if (strcmp(left_parent, right_parent) != 0 || strcmp(left, right) == 0 ||
        strncmp(left, "openbfme-", strlen("openbfme-")) != 0 ||
        left_length <= strlen("openbfme-") + strlen(root_suffix) ||
        strcmp(left + left_length - strlen(root_suffix), root_suffix) != 0 ||
        strncmp(right, ".openbfme-update-", strlen(".openbfme-update-")) != 0) {
        fprintf(stderr, "bundle swap: a package root and its staged sibling are required\n");
        return 1;
    }
    int parent = open(left_parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent < 0) {
        perror("bundle swap: open parent");
        return 1;
    }
    struct stat a, b, app, launcher;
    if (fstatat(parent, left, &a, AT_SYMLINK_NOFOLLOW) != 0 ||
        fstatat(parent, right, &b, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISDIR(a.st_mode) || !S_ISDIR(b.st_mode) || a.st_dev != b.st_dev ||
        a.st_ino == b.st_ino) {
        fprintf(stderr, "bundle swap: paths must be different real directories on the same filesystem\n");
        close(parent);
        return 1;
    }
    int root_fd = openat(parent, restore ? right : left, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root_fd < 0 ||
        fstatat(root_fd, "OpenBFME.app", &app, AT_SYMLINK_NOFOLLOW) != 0 ||
        fstatat(root_fd, "OpenBFME Launcher.app", &launcher, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISDIR(app.st_mode) || !S_ISDIR(launcher.st_mode)) {
        fprintf(stderr, restore ? "bundle swap: the package to restore is not a complete OpenBFME package root\n"
                                : "bundle swap: installed path is not a complete OpenBFME package root\n");
        if (root_fd >= 0)
            close(root_fd);
        close(parent);
        return 1;
    }
    close(root_fd);
    if (renameatx_np(parent, left, parent, right, RENAME_SWAP) != 0) {
        perror("bundle swap: atomic exchange failed (no non-atomic fallback)");
        close(parent);
        return 1;
    }
    if (fsync(parent) != 0) {
        perror("bundle swap: exchange completed but parent directory sync failed");
        close(parent);
        return 4;
    }
    close(parent);
    return 0;
}

static int sync_file_and_parent(const char *path) {
    char parent_path[PATH_MAX];
    const char *name;
    if (split_directory(path, parent_path, &name) != 0) {
        perror("bundle swap: sync path");
        return 1;
    }
    int parent = open(parent_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent < 0) {
        perror("bundle swap: sync parent");
        return 1;
    }
    int file = openat(parent, name, O_RDWR | O_NOFOLLOW);
    struct stat st;
    if (file < 0 || fstat(file, &st) != 0 || !S_ISREG(st.st_mode) ||
        fsync(file) != 0 || fsync(parent) != 0) {
        perror("bundle swap: durable file sync failed");
        if (file >= 0)
            close(file);
        close(parent);
        return 1;
    }
    close(file);
    close(parent);
    return 0;
}

static int sync_parent(const char *path) {
    char parent_path[PATH_MAX];
    const char *name;
    if (split_directory(path, parent_path, &name) != 0) {
        perror("bundle swap: sync parent path");
        return 1;
    }
    int parent = open(parent_path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent < 0) {
        perror("bundle swap: open parent for sync");
        return 1;
    }
    int result = fsync(parent);
    if (result != 0)
        perror("bundle swap: parent directory sync failed");
    close(parent);
    return result == 0 ? 0 : 1;
}

/* The launcher authenticates both trees and journals the transaction before calling
 * this helper. Darwin's RENAME_SWAP keeps both complete app bundles at the install
 * path across interruption; a pair of ordinary directory renames cannot do that. */
int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--sync") == 0)
        return sync_file_and_parent(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--sync-parent") == 0)
        return sync_parent(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--pid-alive") == 0) {
        char *end = NULL;
        errno = 0;
        long value = strtol(argv[2], &end, 10);
        if (errno != 0 || end == argv[2] || *end != '\0' || value <= 0 || value > INT32_MAX)
            return 2;
        return process_alive((pid_t)value);
    }
    if (argc == 3 && strcmp(argv[1], "--canonical") == 0)
        return canonical_command(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--lock-status") == 0)
        return lock_command("status", argv[2], 0, "");
    if (argc == 5 && strcmp(argv[1], "--lock-acquire") == 0) {
        char *end = NULL;
        long pid = strtol(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || pid <= 0) {
            fprintf(stderr, "bundle swap: invalid lock owner process id\n");
            return 2;
        }
        return lock_command("acquire", argv[2], (pid_t)pid, argv[4]);
    }
    if (argc == 5 && strcmp(argv[1], "--lock-release") == 0) {
        char *end = NULL;
        long pid = strtol(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || pid <= 0) {
            fprintf(stderr, "bundle swap: invalid lock owner process id\n");
            return 2;
        }
        return lock_command("release", argv[2], (pid_t)pid, argv[4]);
    }
    if (argc == 4 && strcmp(argv[1], "--game-running") == 0) {
        char *end = NULL;
        long excluded = strtol(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || excluded <= 0) {
            fprintf(stderr, "bundle swap: invalid excluded process id\n");
            return 2;
        }
        int busy = game_running(argv[2], (pid_t)excluded);
        return busy < 0 ? 2 : busy ? 3 : 0;
    }
    if (argc == 4 && strcmp(argv[1], "--process-under") == 0) {
        char *end = NULL;
        long pid = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' || pid <= 0) {
            fprintf(stderr, "bundle swap: invalid process id\n");
            return 2;
        }
        int under = process_under((pid_t)pid, argv[3]);
        return under < 0 ? 2 : under ? 3 : 0;
    }
    if (argc == 5 && (strcmp(argv[1], "--swap-if-idle") == 0 || strcmp(argv[1], "--restore-if-idle") == 0)) {
        char *end = NULL;
        long excluded = strtol(argv[4], &end, 10);
        if (end == argv[4] || *end != '\0' || excluded <= 0) {
            fprintf(stderr, "bundle swap: invalid excluded process id\n");
            return 2;
        }
        int busy = game_running(argv[2], (pid_t)excluded);
        if (busy < 0)
            return 2;
        if (busy)
            return 3;
        return swap_directories(argv[2], argv[3], strcmp(argv[1], "--restore-if-idle") == 0);
    }
    if (argc != 3) {
        fprintf(stderr, "usage: openbfme-bundle-swap <installed-package-root> <staged-sibling>\n"
                        "       openbfme-bundle-swap --canonical <directory>\n"
                        "       openbfme-bundle-swap --sync-parent <path>\n"
                        "       openbfme-bundle-swap --pid-alive <pid>\n"
                        "       openbfme-bundle-swap --lock-status <lock-directory>\n"
                        "       openbfme-bundle-swap --lock-acquire <lock-directory> <owner-pid> <token>\n"
                        "       openbfme-bundle-swap --lock-release <lock-directory> <owner-pid> <token>\n"
                        "       openbfme-bundle-swap --game-running <install-root> <excluded-pid>\n"
                        "       openbfme-bundle-swap --process-under <pid> <directory>\n"
                        "       openbfme-bundle-swap --swap-if-idle <install-root> <staged-sibling> <excluded-pid>\n"
                        "       openbfme-bundle-swap --restore-if-idle <install-root> <previous-package-sibling> <excluded-pid>\n");
        return 2;
    }
    return swap_directories(argv[1], argv[2], 0);
}
