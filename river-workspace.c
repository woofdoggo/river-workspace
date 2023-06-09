#include "river-control-unstable-v1.h"
#include "river-status-unstable-v1.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

static enum { LEFT, RIGHT } direction;
static enum { FOCUS, WINDOW } shift_mode;
static int tag_count = 0;
static bool done = false;

static struct wl_display *wl_display = NULL;
static struct wl_output *wl_output = NULL;
static struct wl_registry *wl_registry = NULL;
static struct wl_seat *wl_seat = NULL;
static struct zriver_control_v1 *river_control = NULL;
static struct zriver_output_status_v1 *river_output_status = NULL;
static struct zriver_seat_status_v1 *river_seat_status = NULL;
static struct zriver_status_manager_v1 *river_status_manager = NULL;

static const struct zriver_output_status_v1_listener output_status_listener;

static void
set_tags(const char *command, char *mask) {
    zriver_control_v1_add_argument(river_control, command);
    zriver_control_v1_add_argument(river_control, mask);

    // We don't care about the callback for success/failure.
    zriver_control_v1_run_command(river_control, wl_seat);
    wl_display_roundtrip(wl_display);
}

static void
on_wl_registry_global(void *data, struct wl_registry *wl_registry, uint32_t id,
                      const char *interface, uint32_t version) {
    if (strcmp(interface, wl_output_interface.name) == 0) {
        wl_output = wl_registry_bind(wl_registry, id, &wl_output_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        wl_seat = wl_registry_bind(wl_registry, id, &wl_seat_interface, 1);
    } else if (strcmp(interface, zriver_control_v1_interface.name) == 0) {
        river_control = wl_registry_bind(wl_registry, id, &zriver_control_v1_interface, 1);
    } else if (strcmp(interface, zriver_status_manager_v1_interface.name) == 0) {
        river_status_manager =
            wl_registry_bind(wl_registry, id, &zriver_status_manager_v1_interface, 1);
    }
}

static void
on_wl_registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t id) {
    // Noop.
}

static void
on_output_status_listener_layout_name(void *data, struct zriver_output_status_v1 *status,
                                      const char *name) {
    // Noop.
}

static void
on_output_status_listener_layout_name_clear(void *data, struct zriver_output_status_v1 *status) {
    // Noop.
}

static void
on_output_status_listener_focused_tags(void *data, struct zriver_output_status_v1 *status,
                                       uint32_t tagmask) {
    if (done) {
        return;
    }
    done = true;

    uint32_t newmask = 0;
    if (direction == RIGHT) {
        for (int i = 0; i < tag_count; i++) {
            if (tagmask & (1 << i)) {
                newmask |= 1 << ((i + 1 + tag_count) % tag_count);
            }
        }
    } else {
        for (int i = 0; i < tag_count; i++) {
            if (tagmask & (1 << i)) {
                newmask |= 1 << ((i - 1 + tag_count) % tag_count);
            }
        }
    }
    char *tags = malloc(32);
    snprintf(tags, 32, "%d", newmask);

    if (shift_mode == FOCUS) {
        printf("%s\n", tags);
        set_tags("set-focused-tags", tags);
    } else {
        set_tags("set-view-tags", tags);
        set_tags("set-focused-tags", tags);
    }
}

static void
on_output_status_listener_urgent_tags(void *data, struct zriver_output_status_v1 *status,
                                      uint32_t tagmask) {
    // Noop, we don't care about urgent tags.
}

static void
on_output_status_listener_view_tags(void *data, struct zriver_output_status_v1 *status,
                                    struct wl_array *array) {
    // Noop, we don't care about view tags.
}

static void
on_seat_status_focused_output(void *data, struct zriver_seat_status_v1 *status,
                              struct wl_output *output) {
    river_output_status =
        zriver_status_manager_v1_get_river_output_status(river_status_manager, output);
    zriver_output_status_v1_add_listener(river_output_status, &output_status_listener, NULL);
    wl_display_roundtrip(wl_display);
}

static void
on_seat_status_focused_view(void *data, struct zriver_seat_status_v1 *status, const char *view) {
    // Noop, we don't care about focused view.
}

static void
on_seat_status_mode(void *data, struct zriver_seat_status_v1 *status, const char *mode) {
    // Noop, we don't care about mode.
}

static void
on_seat_status_unfocused_output(void *data, struct zriver_seat_status_v1 *status,
                                struct wl_output *wl_output) {
    // Noop, we don't care about unfocused output.
}

static const struct wl_registry_listener wl_registry_listener = {
    .global = on_wl_registry_global,
    .global_remove = on_wl_registry_global_remove,
};

static const struct zriver_output_status_v1_listener output_status_listener = {
    .layout_name = on_output_status_listener_layout_name,
    .layout_name_clear = on_output_status_listener_layout_name_clear,

    .focused_tags = on_output_status_listener_focused_tags,
    .urgent_tags = on_output_status_listener_urgent_tags,
    .view_tags = on_output_status_listener_view_tags,
};

static const struct zriver_seat_status_v1_listener seat_status_listener = {
    .focused_output = on_seat_status_focused_output,
    .focused_view = on_seat_status_focused_view,
    .mode = on_seat_status_mode,
    .unfocused_output = on_seat_status_unfocused_output,
};

void
help(int argc, char *argv[]) {
    printf("USAGE: %s TAG_COUNT [focus|window] [left|right]\n",
           argc > 0 ? argv[0] : "river-workspace");
}

int
main(int argc, char *argv[]) {
    if (argc != 4) {
        help(argc, argv);
        return 1;
    }
    if (!(tag_count = atoi(argv[1]))) {
        help(argc, argv);
        return 1;
    } else {
        if (tag_count < 1 || tag_count > 32) {
            printf("invalid tag count [1..32]\n");
            return 1;
        }
    }
    if (strcmp(argv[2], "focus") == 0) {
        shift_mode = FOCUS;
    } else if (strcmp(argv[2], "window") == 0) {
        shift_mode = WINDOW;
    } else {
        help(argc, argv);
        return 1;
    }
    if (strcmp(argv[3], "left") == 0) {
        direction = LEFT;
    } else if (strcmp(argv[3], "right") == 0) {
        direction = RIGHT;
    } else {
        help(argc, argv);
        return 1;
    }

    wl_display = wl_display_connect(NULL);
    assert(wl_display);
    wl_registry = wl_display_get_registry(wl_display);
    assert(wl_registry);
    wl_registry_add_listener(wl_registry, &wl_registry_listener, NULL);
    wl_display_roundtrip(wl_display);
    assert(wl_output && wl_seat && river_control && river_status_manager);

    river_seat_status =
        zriver_status_manager_v1_get_river_seat_status(river_status_manager, wl_seat);
    assert(river_seat_status);
    zriver_seat_status_v1_add_listener(river_seat_status, &seat_status_listener, NULL);
    wl_display_roundtrip(wl_display);

    zriver_control_v1_destroy(river_control);

    return 0;
}
