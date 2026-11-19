// a5kctl: command-line client for the a5kui control socket.
//   a5kctl [--sock PATH] key <code> [value]
//   a5kctl [--sock PATH] shot [path]
//   a5kctl [--sock PATH] nfc        (simulated NFC phone tap; host fake)
//   a5kctl [--sock PATH] quit
// Default socket: $A5KCTL_SOCK or /android/a5k/a5kctl.sock (device) — pass --sock on host.
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv) {
    const char *sock = getenv("A5KCTL_SOCK");
    // Device default is chroot-relative (a5kctl also runs inside chroot /android).
    if (!sock) sock = "/a5k/a5kctl.sock";

    int i = 1;
    if (i < argc && strcmp(argv[i], "--sock") == 0 && i + 1 < argc) { sock = argv[i + 1]; i += 2; }
    if (i >= argc) { fprintf(stderr, "usage: a5kctl [--sock PATH] key N [v] | shot [path] | nfc | quit\n"); return 2; }

    char line[256]; line[0] = 0;
    for (; i < argc; i++) {
        strncat(line, argv[i], sizeof line - strlen(line) - 2);
        strncat(line, " ", sizeof line - strlen(line) - 1);
    }
    strncat(line, "\n", sizeof line - strlen(line) - 1);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, sock, sizeof sa.sun_path - 1);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        fprintf(stderr, "a5kctl: cannot connect %s\n", sock);
        return 1;
    }
    write(fd, line, strlen(line));
    char buf[256];
    ssize_t r = read(fd, buf, sizeof buf - 1);
    if (r > 0) { buf[r] = 0; fputs(buf, stdout); }
    close(fd);
    return 0;
}
