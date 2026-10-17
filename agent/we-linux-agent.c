/*
 * we-linux-agent <state-file>
 *
 * Receives the native window list from the KWin script (dev.welinux.Agent.Update on the session
 * bus) and writes it to <state-file>, where weshim.dll inside Wallpaper Engine picks it up.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#define BUS_NAME  "dev.welinux.Agent"
#define BUS_PATH  "/dev/welinux/Agent"
#define BUS_IFACE "dev.welinux.Agent"

static const char *state_path;

/* Write via a temporary file and rename, so the shim never reads a half-written list. */
static int write_state(const char *text)
{
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s.tmp", state_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -errno;
    fputs(text, f);
    fputc('\n', f);
    if (fclose(f) != 0) return -errno;
    return rename(tmp, state_path) == 0 ? 0 : -errno;
}

static int method_update(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
    const char *text;
    int r = sd_bus_message_read(m, "s", &text);
    if (r < 0) return r;
    r = write_state(text);
    if (r < 0) return sd_bus_error_set_errnof(error, -r, "writing %s: %s", state_path, strerror(-r));
    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable agent_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Update", "s", "", method_update, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <state-file>\n", argv[0]);
        return 2;
    }
    state_path = argv[1];

    sd_bus *bus = NULL;
    int r = sd_bus_open_user(&bus);
    if (r >= 0) r = sd_bus_add_object_vtable(bus, NULL, BUS_PATH, BUS_IFACE, agent_vtable, NULL);
    if (r >= 0) r = sd_bus_request_name(bus, BUS_NAME, 0);
    if (r < 0) {
        fprintf(stderr, "we-linux-agent: D-Bus setup failed: %s\n", strerror(-r));
        return 1;
    }

    /* Nothing is known until KWin reports; an empty list means "no native windows". */
    write_state("");

    for (;;) {
        r = sd_bus_process(bus, NULL);
        if (r < 0) break;
        if (r > 0) continue;
        r = sd_bus_wait(bus, UINT64_MAX);
        if (r < 0) break;
    }
    fprintf(stderr, "we-linux-agent: %s\n", strerror(-r));
    sd_bus_unref(bus);
    return 1;
}
