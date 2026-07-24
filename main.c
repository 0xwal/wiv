#include <assert.h>
#include <errno.h>
#include <cairo/cairo.h>
#include <getopt.h>
#include <libinput.h>
#include <libudev.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "devmgr.h"
#include "shm.h"
#include "pango.h"
#include "keymap.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#ifdef WSK_DEBUG
static FILE *trace_file = NULL;
#define WSK_TRACE(fmt, ...) do { \
	if (trace_file) { \
		struct timespec _ts; \
		clock_gettime(CLOCK_MONOTONIC, &_ts); \
		fprintf(trace_file, "[%ld.%06ld] " fmt "\n", \
			_ts.tv_sec, _ts.tv_nsec / 1000, ##__VA_ARGS__); \
		fflush(trace_file); \
	} \
} while(0)
#else
#define WSK_TRACE(fmt, ...) do {} while(0)
#endif

#define MASK_PATTERNS_MAX 32
#define MASK_BUFFER_MAX 256
#define MASK_THRESHOLD 3

struct mask_state {
	char patterns[MASK_PATTERNS_MAX][256];
	int num_patterns;
	int pos[MASK_PATTERNS_MAX];
	int matched[MASK_PATTERNS_MAX];
	bool active[MASK_PATTERNS_MAX];
	struct wsk_keypress *buffer[MASK_BUFFER_MAX];
	int buffer_len;
};

struct wsk_keypress {
	xkb_keysym_t sym;
	char name[128];
	char utf8[128];
	bool is_repeat;
	struct wsk_keypress *next;
};

struct wsk_output {
	struct wl_output *output;
	struct zxdg_output_v1 *xdg_output;
	int scale;
	enum wl_output_subpixel subpixel;
	char name[128];
	uint32_t registry_name;
	struct wsk_output *next;
};

struct wsk_state {
	int devmgr;
	pid_t devmgr_pid;
	struct udev *udev;
	struct libinput *libinput;

	uint32_t foreground, background, specialfg, repeatfg;
	const char *font;
	char *repeat_font;
	int timeout;
	int length_limit;
	uint32_t min_height;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_keyboard *keyboard;
	struct zxdg_output_manager_v1 *output_mgr;
	struct zwlr_layer_shell_v1 *layer_shell;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	uint32_t width, height;
	struct wsk_output *output, *outputs;

	struct xkb_state *xkb_state;
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;

	struct wsk_keypress *keys; //the begin of the output keylink
	struct timespec last_key;

	bool run;
	bool inspect;
	bool last_was_release;
	//state of function key
	int ctrl_l_hold;
	int ctrl_r_hold;
	int alt_l_hold;
	int alt_r_hold;
	int super_l_hold;
	int supre_r_hold;
	int shift_l_hold;
	int shift_r_hold;

	char current_combination_key[128];
	char prev_combination_keye[128];

	int combination_keye_repetition;
	bool key_held;
	struct timespec last_repeat_time;
	char repeat_state; /* 0=idle, 1=delayed (past initial 400ms), 2=active (repeating) */
	bool resize_pending;
	int sock_fd;
	bool paused;
	char sock_path[256];
	bool dirty;
	struct pool_buffer buffer_pool[2];
	cairo_surface_t *recording;
	cairo_t *recording_cairo;
	cairo_font_options_t *font_options;
	struct mask_state mask;

	enum { OUTPUT_DEFAULT, OUTPUT_PINNED } output_mode;
	char target_output_name[128];
	uint32_t anchor;
	int margin;
};

/* void logtofile(const char *fmt, ...) { */
/*   char buf[256]; */
/*   char cmd[256]; */
/*   va_list ap; */
/*   va_start(ap, fmt); */
/*   vsprintf((char *)buf, fmt, ap); */
/*   va_end(ap); */
/*   unsigned int i = strlen((const char *)buf); */
/*  */
/*   sprintf(cmd, "echo '%.*s' >> ~/log", i, buf); */
/*   system(cmd); */
/* } */
/*  */
/* void lognumtofile(unsigned int num) { */
/*   char cmd[256]; */
/*   sprintf(cmd, "echo '%x' >> ~/log", num); */
/*   system(cmd); */
/* } */

static void cairo_set_source_u32(cairo_t *cairo, uint32_t color) {
	cairo_set_source_rgba(cairo, (color >> (3 * 8) & 0xFF) / 255.0, (color >> (2 * 8) & 0xFF) / 255.0,
			      (color >> (1 * 8) & 0xFF) / 255.0, (color >> (0 * 8) & 0xFF) / 255.0);
}

static cairo_subpixel_order_t to_cairo_subpixel_order(enum wl_output_subpixel subpixel) {
	switch (subpixel) {
		case WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB:
			return CAIRO_SUBPIXEL_ORDER_RGB;
		case WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR:
			return CAIRO_SUBPIXEL_ORDER_BGR;
		case WL_OUTPUT_SUBPIXEL_VERTICAL_RGB:
			return CAIRO_SUBPIXEL_ORDER_VRGB;
		case WL_OUTPUT_SUBPIXEL_VERTICAL_BGR:
			return CAIRO_SUBPIXEL_ORDER_VBGR;
		default:
			return CAIRO_SUBPIXEL_ORDER_DEFAULT;
	}
	return CAIRO_SUBPIXEL_ORDER_DEFAULT;
}


//change default keyname to custom name
static uint32_t parse_color(const char *color);

static const KeymapEntry *keymap_entry(const char *name) {
	/* exact match first */
	for (size_t i = 0; i < KEYMAP_LEN; i++)
		if (!strcmp(keymap[i].name, name))
			return &keymap[i];
	/* try without _L or _R suffix */
	size_t len = strlen(name);
	if (len > 2 && name[len - 2] == '_' && (name[len - 1] == 'L' || name[len - 1] == 'R')) {
		char base[128];
		size_t base_len = len - 2;
		memcpy(base, name, base_len);
		base[base_len] = '\0';
		for (size_t i = 0; i < KEYMAP_LEN; i++)
			if (!strcmp(keymap[i].name, base))
				return &keymap[i];
	}
	return NULL;
}

//show key in keylink(begin at state->keys)
static void render_to_cairo(cairo_t *cairo, struct wsk_state *state, int scale, uint32_t *width, uint32_t *height) {
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_u32(cairo, state->background);
	cairo_paint(cairo);

	/* Use font metrics for stable height — avoids per-glyph shifting */
	int max_h = get_font_line_height(cairo, state->font, scale);
	if (state->repeat_font) {
		int repeat_h = get_font_line_height(cairo, state->repeat_font, scale);
		if (repeat_h > max_h)
			max_h = repeat_h;
	}

	/* Second pass: draw keys with vertical alignment offset */
	struct wsk_keypress *key = state->keys;
	const char *prev_display = NULL;
	while (key) {
		const KeymapEntry *entry = keymap_entry(key->name);
		const char *display;
		uint32_t color;

		if (state->inspect) {
			display = key->name;
			color = state->specialfg;
		} else if (entry) {
			color = entry->fg ? parse_color(entry->fg) : state->foreground;
			display = entry->display ? entry->display : (key->utf8[0] ? key->utf8 : key->name);
		} else if (key->utf8[0]) {
			display = key->utf8;
			color = state->foreground;
		} else {
			display = key->name;
			color = state->foreground;
		}

		if (key->is_repeat) {
			color = state->repeatfg;
		}

		const char *pad_before =
			(prev_display && prev_display[strlen(prev_display) - 1] == '+') ? "" : KEY_PAD_BEFORE;

		const char *use_font = key->is_repeat ? state->repeat_font : state->font;
		int w, h;
		get_text_size(cairo, use_font, &w, &h, NULL, scale, "%s%s%s", pad_before, display, KEY_PAD_AFTER);

		int target_h = max_h + (int)state->min_height;

		int y_offset = 0;
#if defined(TEXT_ALIGN_CENTER)
		y_offset = (target_h - h) / 2;
#elif defined(TEXT_ALIGN_BOTTOM)
		y_offset = target_h - h;
#endif

		cairo_set_source_u32(cairo, color);
		cairo_move_to(cairo, *width, y_offset);
		pango_printf(cairo, use_font, scale, "%s%s%s", pad_before, display, KEY_PAD_AFTER);

		*width += w;
		if ((int) *height < target_h)
			*height = target_h;
		prev_display = display;
		key = key->next;
	}
}

static void render_frame(struct wsk_state *state) {
	WSK_TRACE("render_frame entry");
	int scale = state->output ? state->output->scale : 1;

	if (!state->recording) {
		state->recording = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, NULL);
		state->recording_cairo = cairo_create(state->recording);

		state->font_options = cairo_font_options_create();
		cairo_font_options_set_hint_style(state->font_options, CAIRO_HINT_STYLE_FULL);
		cairo_font_options_set_antialias(state->font_options, CAIRO_ANTIALIAS_SUBPIXEL);
		if (state->output) {
			cairo_font_options_set_subpixel_order(state->font_options,
				to_cairo_subpixel_order(state->output->subpixel));
		}
	}

	cairo_t *cairo = state->recording_cairo;
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_set_font_options(cairo, state->font_options);

	cairo_save(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cairo);
	cairo_restore(cairo);

	uint32_t width = 0, height = 0;
	render_to_cairo(cairo, state, scale, &width, &height);

	if (height / scale != state->height || width / scale != state->width || state->width == 0) {
		if (width == 0 || height == 0) {
			if (state->width && state->height) {
				struct pool_buffer *buf = get_next_buffer(state->shm,
					state->buffer_pool, state->width * scale, state->height * scale);
				if (buf) {
					cairo_t *shm = buf->cairo;
					cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
					cairo_paint(shm);
					wl_surface_set_buffer_scale(state->surface, scale);
					wl_surface_attach(state->surface, buf->buffer, 0, 0);
					wl_surface_damage_buffer(state->surface, 0, 0,
						state->width * scale, state->height * scale);
					WSK_TRACE("wl_surface_commit (path=clear)");
					wl_surface_commit(state->surface);
				}
			}
			state->resize_pending = false;
		} else if (!state->resize_pending) {
			state->resize_pending = true;
			zwlr_layer_surface_v1_set_size(state->layer_surface, width / scale, height / scale);
			if (state->width && state->height) {
				struct pool_buffer *buf = get_next_buffer(state->shm,
					state->buffer_pool, state->width * scale, state->height * scale);
				if (buf) {
					cairo_t *shm = buf->cairo;
					cairo_save(shm);
					cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
					cairo_paint(shm);
					cairo_restore(shm);
					cairo_set_source_surface(shm, state->recording, 0.0, 0.0);
					cairo_paint(shm);
					wl_surface_set_buffer_scale(state->surface, scale);
					wl_surface_attach(state->surface, buf->buffer, 0, 0);
					wl_surface_damage_buffer(state->surface, 0, 0,
						state->width * scale, state->height * scale);
					WSK_TRACE("wl_surface_commit (path=resize)");
					wl_surface_commit(state->surface);
				}
			}
		}
	} else if (height > 0) {
		struct pool_buffer *buf = get_next_buffer(state->shm,
			state->buffer_pool, state->width * scale, state->height * scale);
		if (!buf) {
			return;
		}
		cairo_t *shm = buf->cairo;

		cairo_save(shm);
		cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
		cairo_paint(shm);
		cairo_restore(shm);

		cairo_set_source_surface(shm, state->recording, 0.0, 0.0);
		cairo_paint(shm);

		wl_surface_set_buffer_scale(state->surface, scale);
		wl_surface_attach(state->surface, buf->buffer, 0, 0);
		wl_surface_damage_buffer(state->surface, 0, 0,
			state->width * scale, state->height * scale);
		WSK_TRACE("wl_surface_commit (path=normal)");
		wl_surface_commit(state->surface);
	}
}

bool surface_is_configured(struct wsk_state *state) {
	return (state->width && state->height);
}

static void set_dirty(struct wsk_state *state) {
	WSK_TRACE("set_dirty called (was %d)", state->dirty);
	state->dirty = true;
}

#ifdef WSK_DEBUG
static void debug_print_display(struct wsk_state *state) {
	fprintf(trace_file, "[display] ");
	struct wsk_keypress *key = state->keys;
	if (!key) {
		fprintf(trace_file, "(empty)");
		fflush(trace_file);
		return;
	}
	int count = 0;
	while (key) {
		const char *display;
		const KeymapEntry *entry = keymap_entry(key->name);
		if (state->inspect) {
			display = key->name;
		} else if (entry && entry->display) {
			display = entry->display;
		} else if (key->utf8[0]) {
			display = key->utf8;
		} else {
			display = key->name;
		}
		if (key->is_repeat) {
			fprintf(trace_file, "‹%s›", display);
		} else {
			fprintf(trace_file, "%s", display);
		}
		count++;
		key = key->next;
	}
	fprintf(trace_file, "  [%d nodes]", count);
	fflush(trace_file);
}
#endif

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1, uint32_t serial,
				    uint32_t width, uint32_t height) {
	struct wsk_state *state = data;
	WSK_TRACE("configure serial=%u w=%u h=%u (current w=%u h=%u resize_pending=%d)",
		serial, width, height, state->width, state->height, state->resize_pending);
	state->width = width;
	state->height = height;
	state->resize_pending = false;
	zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *zwlr_layer_surface_v1) {
	struct wsk_state *state = data;
	state->run = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void surface_enter(void *data, struct wl_surface *wl_surface, struct wl_output *output) {
	struct wsk_state *state = data;
	struct wsk_output *wsk_output = state->outputs;
	while (wsk_output->output != output) {
		wsk_output = wsk_output->next;
	}
	state->output = wsk_output;
}

static void surface_leave(void *data, struct wl_surface *wl_surface, struct wl_output *output) {
	// Who cares (not really possible with layer shell)
}

static const struct wl_surface_listener wl_surface_listener = {
	.enter = surface_enter,
	.leave = surface_leave,
};

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format, int32_t fd, uint32_t size) {
	struct wsk_state *state = data;
	char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map_shm == MAP_FAILED) {
		close(fd);
		fprintf(stderr, "Unable to mmap keymap: %s", strerror(errno));
		return;
	}
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		munmap(map_shm, size);
		close(fd);
		return;
	}

	struct xkb_keymap *keymap = xkb_keymap_new_from_string(state->xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1,
							       XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_shm, size);
	close(fd);

	struct xkb_state *xkb_state = xkb_state_new(keymap);
	xkb_keymap_unref(state->xkb_keymap);
	xkb_state_unref(state->xkb_state);
	state->xkb_keymap = keymap;
	state->xkb_state = xkb_state;
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface,
			   struct wl_array *keys) {
	// Who cares
}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, struct wl_surface *surface) {
	// Who cares
}

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t time, uint32_t key,
			 uint32_t state) {
	// Who cares
}

static void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial, uint32_t mods_depressed,
			       uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
	// Who cares
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard, int32_t rate, int32_t delay) {
	// TODO
}

static const struct wl_keyboard_listener wl_keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
	struct wsk_state *state = data;
	if (state->keyboard) {
		// TODO: support multiple seats
		return;
	}

	if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD)) {
		fprintf(stderr, "wl_seat does not support keyboard");
		state->run = false;
		return;
	}

	state->keyboard = wl_seat_get_keyboard(wl_seat);
	wl_keyboard_add_listener(state->keyboard, &wl_keyboard_listener, state);
}

static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
	struct wsk_state *state = data;
	/* TODO: support multiple seats */
	if (libinput_udev_assign_seat(state->libinput, "seat0") != 0) {
		fprintf(stderr, "Failed to assign libinput seat\n");
		state->run = false;
		return;
	}
}

static const struct wl_seat_listener wl_seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width,
			    int32_t physical_height, int32_t subpixel, const char *make, const char *model,
			    int32_t transform) {
	struct wsk_output *output = data;
	output->subpixel = subpixel;
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height,
			int32_t refresh) {
	// Who cares
}

static void output_done(void *data, struct wl_output *wl_output) {
	// Who cares
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
	struct wsk_output *output = data;
	output->scale = factor;
}

static const struct wl_output_listener wl_output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
};

static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name) {
	struct wsk_output *output = data;
	strncpy(output->name, name, sizeof(output->name) - 1);
	output->name[sizeof(output->name) - 1] = '\0';
}

static void xdg_output_handle_logical_position(void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y) {}

static void xdg_output_handle_logical_size(void *data, struct zxdg_output_v1 *xdg_output, int32_t width,
					   int32_t height) {}

static void xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output) {}

static void xdg_output_handle_description(void *data, struct zxdg_output_v1 *xdg_output, const char *description) {
	// Not needed
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = xdg_output_handle_logical_position,
	.logical_size = xdg_output_handle_logical_size,
	.done = xdg_output_handle_done,
	.name = xdg_output_handle_name,
	.description = xdg_output_handle_description,
};

//add keyboard event listen
static void registry_global(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface,
			    uint32_t version) {
	struct wsk_state *state = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		state->seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 5);
	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		state->output_mgr = wl_registry_bind(wl_registry, name, &zxdg_output_manager_v1_interface, 3);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = wl_registry_bind(wl_registry, name, &zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wsk_output *output = calloc(1, sizeof(struct wsk_output));
		output->output = wl_registry_bind(wl_registry, name, &wl_output_interface, 3);
		output->scale = 1;
		output->name[0] = '\0';
		output->xdg_output = NULL;
		output->registry_name = name;
		struct wsk_output **link = &state->outputs;
		while (*link) {
			link = &(*link)->next;
		}
		*link = output;
		wl_output_add_listener(output->output, &wl_output_listener, output);
		if (state->output_mgr) {
			output->xdg_output = zxdg_output_manager_v1_get_xdg_output(state->output_mgr, output->output);
			zxdg_output_v1_add_listener(output->xdg_output, &xdg_output_listener, output);
		}
	}
}

static void registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name) {
	/* This space deliberately left blank */
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static int caculat_del_charnum_of_int(int num) {
	int count = 0;

	if (num == 1) {
		return 0;
	}

	while (num != 0) {
		num /= 10;
		++count;
	}
	return count + 1;
}

static int caculat_add_charnum_of_int(int num) {
	int count = 0;

	while (num != 0) {
		num /= 10;
		++count;
	}
	return count;
}


static void del_last_key(struct wsk_state *state, int n) {
	struct wsk_keypress **temp_keypress;
	while (n > 0) {
		struct wsk_keypress **link = &state->keys;
		while (*link) {
			temp_keypress = &(*link)->next;
			if ((*temp_keypress) == NULL) {
				free(*link);
				*link = NULL;
			} else {
				link = temp_keypress;
			}
		}
		n--;
	}
}

static void attach_to_last(struct wsk_state *state, struct wsk_keypress *key) {
	struct wsk_keypress **attach = &state->keys;
	//get the end of the output keylink
	while (*attach) {
		attach = &(*attach)->next;
	}
	*attach = key;
}

static const char *numchar_map[] = {
	REPEAT_0, REPEAT_1, REPEAT_2, REPEAT_3, REPEAT_4,
	REPEAT_5, REPEAT_6, REPEAT_7, REPEAT_8, REPEAT_9
};

static void change_numchar_to_special(char *target, char numchar) {
	if (numchar >= '0' && numchar <= '9') {
		strcpy(target, numchar_map[numchar - '0']);
	}
}

static void attach_repeat_flag(struct wsk_state *state, int num, int num_len) {
	struct wsk_keypress *repeat_flag = calloc(1, sizeof(struct wsk_keypress));
	strcpy(repeat_flag->name, REPEAT_MARKER);
	repeat_flag->is_repeat = true;
	attach_to_last(state, repeat_flag);

	char *repeat_num_char = calloc(num_len + 1, sizeof(char));
	sprintf(repeat_num_char, "%d", num);

	for (int i = 0; i < num_len; i++) {
		//   printf("%c\n", a[i]); // 打印每个字符
		struct wsk_keypress *repeat_num = calloc(1, sizeof(struct wsk_keypress));
		change_numchar_to_special(repeat_num->name, repeat_num_char[i]);
		repeat_num->is_repeat = true;
		attach_to_last(state, repeat_num);
	}

	free(repeat_num_char);
}

/* Generate a synthetic repeat for a held key */
static void generate_held_key_repeat(struct wsk_state *state) {
	int del_charnum = caculat_del_charnum_of_int(state->combination_keye_repetition);
	if (state->combination_keye_repetition > 2)
		del_last_key(state, del_charnum);
	state->combination_keye_repetition++;
	if (state->combination_keye_repetition > 2) {
		int add_charnum = caculat_add_charnum_of_int(state->combination_keye_repetition);
		attach_repeat_flag(state, state->combination_keye_repetition, add_charnum);
	}
	set_dirty(state);
}

static int pattern_char_matches(char pchar, struct wsk_keypress *kp) {
	if (pchar == '?')
		return kp->utf8[0] != '\0' ? 1 : 0;
	if (kp->utf8[0] != '\0')
		return tolower((unsigned char) kp->utf8[0]) == tolower((unsigned char) pchar) ? 1 : 0;
	return tolower((unsigned char) kp->name[0]) == tolower((unsigned char) pchar) ? 1 : 0;
}

static void mask_reset(struct mask_state *m) {
	for (int i = 0; i < m->num_patterns; i++) {
		m->pos[i] = 0;
		m->matched[i] = 0;
		m->active[i] = false;
	}
	m->buffer_len = 0;
}

static int mask_check(struct mask_state *m, struct wsk_keypress *kp) {
	if (m->num_patterns == 0)
		return 0;

	bool any_active = false;
	bool any_full = false;

	for (int i = 0; i < m->num_patterns; i++) {
		int plen = (int) strlen(m->patterns[i]);
		if (plen == 0)
			continue;

		int ppos = m->active[i] ? m->pos[i] : 0;

		if (ppos >= plen)
			continue;

		if (pattern_char_matches(m->patterns[i][ppos], kp)) {
			m->active[i] = true;
			m->pos[i] = ppos + 1;
			m->matched[i]++;
			any_active = true;
			if (m->pos[i] >= plen) {
				any_full = true;
			}
		} else {
			m->active[i] = false;
		}
	}

	if (any_full)
		return 2; // DISCARD

	if (!any_active) {
		int max_m = 0;
		for (int i = 0; i < m->num_patterns; i++)
			if (m->matched[i] > max_m)
				max_m = m->matched[i];
		if (max_m >= MASK_THRESHOLD)
			return 2; // DISCARD
		if (max_m > 0)
			return 3; // FLUSH
		return 0; // PASS
	}

	return 1; // BUFFER
}

static void mask_buffer_add(struct mask_state *m, struct wsk_keypress *kp) {
	if (m->buffer_len >= MASK_BUFFER_MAX)
		return;
	m->buffer[m->buffer_len++] = kp;
}

static int append_key_with_modifiers(struct wsk_state *state, struct wsk_keypress *kp) {
	struct wsk_keypress **link = &state->keys;
	while (*link)
		link = &(*link)->next;
	int n = 0;

	if (state->shift_l_hold) {
		struct wsk_keypress *tk = calloc(1, sizeof(struct wsk_keypress));
		strcpy(tk->name, "Shift_L");
		*link = tk;
		link = &(*link)->next;
		n++;
	}
	if (state->shift_r_hold) {
		struct wsk_keypress *tk = calloc(1, sizeof(struct wsk_keypress));
		strcpy(tk->name, "Shift_R");
		*link = tk;
		link = &(*link)->next;
		n++;
	}
	if (state->ctrl_l_hold) {
		struct wsk_keypress *tk = calloc(1, sizeof(struct wsk_keypress));
		strcpy(tk->name, "Control_L");
		*link = tk;
		link = &(*link)->next;
		n++;
	}
	if (state->ctrl_r_hold) {
		struct wsk_keypress *tk = calloc(1, sizeof(struct wsk_keypress));
		strcpy(tk->name, "Control_R");
		*link = tk;
		link = &(*link)->next;
		n++;
	}
	if (state->super_l_hold) {
		struct wsk_keypress *tk = calloc(1, sizeof(struct wsk_keypress));
		strcpy(tk->name, "Super_L");
		*link = tk;
		link = &(*link)->next;
		n++;
	}
	if (state->supre_r_hold) {
		struct wsk_keypress *tk = calloc(1, sizeof(struct wsk_keypress));
		strcpy(tk->name, "Super_R");
		*link = tk;
		link = &(*link)->next;
		n++;
	}
	if (state->alt_l_hold) {
		struct wsk_keypress *tk = calloc(1, sizeof(struct wsk_keypress));
		strcpy(tk->name, "Alt_L");
		*link = tk;
		link = &(*link)->next;
		n++;
	}
	if (state->alt_r_hold) {
		struct wsk_keypress *tk = calloc(1, sizeof(struct wsk_keypress));
		strcpy(tk->name, "Alt_R");
		*link = tk;
		link = &(*link)->next;
		n++;
	}

	*link = kp;
	kp->next = NULL;
	n++;
	return n;
}

static void mask_flush(struct wsk_state *state, struct wsk_keypress *current_kp) {
	for (int i = 0; i < state->mask.buffer_len; i++)
		append_key_with_modifiers(state, state->mask.buffer[i]);
	append_key_with_modifiers(state, current_kp);
	// Reset repeat detection
	memset(state->prev_combination_keye, 0, sizeof(state->prev_combination_keye));
	state->combination_keye_repetition = 1;
	mask_reset(&state->mask);
}

static void mask_discard(struct wsk_state *state, struct wsk_keypress *current_kp) {
	for (int i = 0; i < state->mask.buffer_len; i++)
		free(state->mask.buffer[i]);
	free(current_kp);
	mask_reset(&state->mask);
}

//listen key keydown and record to keylink
static void handle_libinput_event(struct wsk_state *state, struct libinput_event *event) {
	if (!state->xkb_state) {
		return;
	}

	enum libinput_event_type event_type = libinput_event_get_type(event);
	if (event_type != LIBINPUT_EVENT_KEYBOARD_KEY) {
		return;
	}

	struct libinput_event_keyboard *kbevent = libinput_event_get_keyboard_event(event);

	uint32_t keycode = libinput_event_keyboard_get_key(kbevent) + 8;
	enum libinput_key_state key_state = libinput_event_keyboard_get_key_state(kbevent);
	xkb_state_update_key(state->xkb_state, keycode,
			     key_state == LIBINPUT_KEY_STATE_RELEASED ? XKB_KEY_UP : XKB_KEY_DOWN);

	xkb_keysym_t keysym = xkb_state_key_get_one_sym(state->xkb_state, keycode);

	struct wsk_keypress *keypress;
	keypress = calloc(1, sizeof(struct wsk_keypress));
	assert(keypress);
	keypress->sym = keysym;
	xkb_keysym_get_name(keypress->sym, keypress->name, sizeof(keypress->name));
	if (xkb_state_key_get_utf8(state->xkb_state, keycode, keypress->utf8, sizeof(keypress->utf8)) <= 0 ||
	    keypress->utf8[0] <= ' ') {
		keypress->utf8[0] = '\0';
	}

	WSK_TRACE("key_event sym=%u name=%s state=%s", keysym, keypress->name, key_state == LIBINPUT_KEY_STATE_PRESSED ? "pressed" : "released");

	switch (key_state) {
		case LIBINPUT_KEY_STATE_RELEASED:
			//if 'ctrl shift alt super' release, clear it's press state
			if (strlen(keypress->name) > 2 &&
			    strstr("Control_LControl_RAlt_LAlt_RSuper_LSuper_RShift_LShift_RMeta_LMeta_R",
				   keypress->name)) {
				if (strcmp(keypress->name, "Control_L") == 0) {
					state->ctrl_l_hold = 0;
				} else if (strcmp(keypress->name, "Control_R") == 0) {
					state->ctrl_r_hold = 0;
				} else if (strcmp(keypress->name, "Alt_L") == 0 ||
					   strcmp(keypress->name, "Meta_L") == 0) {
					state->alt_l_hold = 0;
				} else if (strcmp(keypress->name, "Alt_R") == 0 ||
					   strcmp(keypress->name, "Meta_R") == 0) {
					state->alt_r_hold = 0;
				} else if (strcmp(keypress->name, "Super_L") == 0) {
					state->super_l_hold = 0;
				} else if (strcmp(keypress->name, "Super_R") == 0) {
					state->supre_r_hold = 0;
				} else if (strcmp(keypress->name, "Shift_L") == 0) {
					state->shift_l_hold = 0;
				} else if (strcmp(keypress->name, "Shift_R") == 0) {
					state->shift_r_hold = 0;
				}
				free(keypress);
				state->last_was_release = 1;
				clock_gettime(CLOCK_MONOTONIC, &state->last_key);
				return;
			}
			break;
		case LIBINPUT_KEY_STATE_PRESSED:
			//if 'ctrl shift alt super' press,mark it's press state
			if (strlen(keypress->name) > 2 &&
			    strstr("Control_LControl_RAlt_LAlt_RSuper_LSuper_RShift_LShift_RMeta_LMeta_R",
				   keypress->name)) {
				if (strcmp(keypress->name, "Control_L") == 0) {
					state->ctrl_l_hold = 1;
				} else if (strcmp(keypress->name, "Control_R") == 0) {
					state->ctrl_r_hold = 1;
				} else if (strcmp(keypress->name, "Alt_L") == 0 ||
					   strcmp(keypress->name, "Meta_L") == 0) {
					state->alt_l_hold = 1;
				} else if (strcmp(keypress->name, "Alt_R") == 0 ||
					   strcmp(keypress->name, "Meta_R") == 0) {
					state->alt_r_hold = 1;
				} else if (strcmp(keypress->name, "Super_L") == 0) {
					state->super_l_hold = 1;
				} else if (strcmp(keypress->name, "Super_R") == 0) {
					state->supre_r_hold = 1;
				} else if (strcmp(keypress->name, "Shift_L") == 0) {
					state->shift_l_hold = 1;
				} else if (strcmp(keypress->name, "Shift_R") == 0) {
					state->shift_r_hold = 1;
				}
				free(keypress);
				state->last_was_release = 0;
				clock_gettime(CLOCK_MONOTONIC, &state->last_key);
				return;
			} else {
				// Pattern masking — intercept before display
				int mask_result = mask_check(&state->mask, keypress);

				// Backspace while pattern buffer active — pop last
				if (state->mask.buffer_len > 0 && (strcmp(keypress->name, "BackSpace") == 0 ||
								   strcmp(keypress->name, "Delete") == 0)) {
					struct wsk_keypress *last = state->mask.buffer[state->mask.buffer_len - 1];
					free(last);
					state->mask.buffer_len--;
					for (int p = 0; p < state->mask.num_patterns; p++) {
						if (state->mask.active[p] && state->mask.pos[p] > 0) {
							state->mask.pos[p]--;
							if (state->mask.matched[p] > 0)
								state->mask.matched[p]--;
						}
					}
					free(keypress);
					state->last_was_release = (key_state == LIBINPUT_KEY_STATE_RELEASED);
					clock_gettime(CLOCK_MONOTONIC, &state->last_key);
					return;
				}

				// Key repeat while pattern buffer active — flush/discard
				if (state->combination_keye_repetition > 1 && state->mask.buffer_len > 0) {
					int max_m = 0;
					for (int i = 0; i < state->mask.num_patterns; i++)
						if (state->mask.matched[i] > max_m)
							max_m = state->mask.matched[i];
					if (max_m >= MASK_THRESHOLD) {
						for (int i = 0; i < state->mask.buffer_len; i++)
							free(state->mask.buffer[i]);
					} else {
						for (int i = 0; i < state->mask.buffer_len; i++)
							append_key_with_modifiers(state, state->mask.buffer[i]);
					}
					mask_reset(&state->mask);
					// Fall through to normal repeat handling
				}

				if (mask_result == 1) {
					mask_buffer_add(&state->mask, keypress);
					state->last_was_release = (key_state == LIBINPUT_KEY_STATE_RELEASED);
					clock_gettime(CLOCK_MONOTONIC, &state->last_key);
					return;
				}
				if (mask_result == 2) {
					mask_discard(state, keypress);
					state->last_was_release = (key_state == LIBINPUT_KEY_STATE_RELEASED);
					clock_gettime(CLOCK_MONOTONIC, &state->last_key);
					return;
				}
				if (mask_result == 3) {
					// Build current_combination_key from the keys we're about to flush
					memset(state->current_combination_key, 0,
					       sizeof(state->current_combination_key));
					mask_flush(state, keypress);
					set_dirty(state);
					state->last_was_release = (key_state == LIBINPUT_KEY_STATE_RELEASED);
					clock_gettime(CLOCK_MONOTONIC, &state->last_key);
					return;
				}

				struct wsk_keypress **link = &state->keys;
				while (*link) {
					link = &(*link)->next;
				}

				int special_key_num = 0;
				memset(state->current_combination_key, 0, sizeof(state->current_combination_key));

				// Build combo string (just string, no nodes yet)
				if (state->shift_l_hold)
					strcat(state->current_combination_key, "Shift_L");
				if (state->shift_r_hold)
					strcat(state->current_combination_key, "Shift_R");
				if (state->ctrl_l_hold)
					strcat(state->current_combination_key, "Control_L");
				if (state->ctrl_r_hold)
					strcat(state->current_combination_key, "Control_R");
				if (state->super_l_hold)
					strcat(state->current_combination_key, "Super_L");
				if (state->supre_r_hold)
					strcat(state->current_combination_key, "Super_R");
				if (state->alt_l_hold)
					strcat(state->current_combination_key, "Alt_L");
				if (state->alt_r_hold)
					strcat(state->current_combination_key, "Alt_R");
				strcat(state->current_combination_key, keypress->name);

				if (strcmp(state->prev_combination_keye, "") != 0 &&
				    strcmp(state->prev_combination_keye, state->current_combination_key) == 0) {
					// === REPEAT PATH ===
					if (state->combination_keye_repetition < 2) {
						// 2nd press: add key node (no counter yet)
						*link = keypress;
					} else {
						// 3rd+ press: counter shows, don't add key node
						free(keypress);
						if (state->combination_keye_repetition > 2)
							del_last_key(state,
								caculat_del_charnum_of_int(state->combination_keye_repetition));
					}
					state->combination_keye_repetition++;
					if (state->combination_keye_repetition > 2) {
						int add_charnum =
							caculat_add_charnum_of_int(state->combination_keye_repetition);
						attach_repeat_flag(state, state->combination_keye_repetition,
								   add_charnum);
					}
				} else {
					// === FIRST PRESS PATH: add modifier nodes + main key ===
					if (state->shift_l_hold) {
						struct wsk_keypress *temp_keypress = calloc(1, sizeof(struct wsk_keypress));
						strcpy(temp_keypress->name, "Shift_L");
						special_key_num++;
						*link = temp_keypress;
						link = &(*link)->next;
					}
					if (state->shift_r_hold) {
						struct wsk_keypress *temp_keypress = calloc(1, sizeof(struct wsk_keypress));
						strcpy(temp_keypress->name, "Shift_R");
						special_key_num++;
						*link = temp_keypress;
						link = &(*link)->next;
					}
					if (state->ctrl_l_hold) {
						struct wsk_keypress *temp_keypress = calloc(1, sizeof(struct wsk_keypress));
						strcpy(temp_keypress->name, "Control_L");
						special_key_num++;
						*link = temp_keypress;
						link = &(*link)->next;
					}
					if (state->ctrl_r_hold) {
						struct wsk_keypress *temp_keypress = calloc(1, sizeof(struct wsk_keypress));
						strcpy(temp_keypress->name, "Control_R");
						special_key_num++;
						*link = temp_keypress;
						link = &(*link)->next;
					}
					if (state->super_l_hold) {
						struct wsk_keypress *temp_keypress = calloc(1, sizeof(struct wsk_keypress));
						strcpy(temp_keypress->name, "Super_L");
						special_key_num++;
						*link = temp_keypress;
						link = &(*link)->next;
					}
					if (state->supre_r_hold) {
						struct wsk_keypress *temp_keypress = calloc(1, sizeof(struct wsk_keypress));
						strcpy(temp_keypress->name, "Super_R");
						special_key_num++;
						*link = temp_keypress;
						link = &(*link)->next;
					}
					if (state->alt_l_hold) {
						struct wsk_keypress *temp_keypress = calloc(1, sizeof(struct wsk_keypress));
						strcpy(temp_keypress->name, "Alt_L");
						special_key_num++;
						*link = temp_keypress;
						link = &(*link)->next;
					}
					if (state->alt_r_hold) {
						struct wsk_keypress *temp_keypress = calloc(1, sizeof(struct wsk_keypress));
						strcpy(temp_keypress->name, "Alt_R");
						special_key_num++;
						*link = temp_keypress;
						link = &(*link)->next;
					}

					*link = keypress;
					special_key_num++;

					memset(state->prev_combination_keye, 0, sizeof(state->prev_combination_keye));
					strcat(state->prev_combination_keye, state->current_combination_key);
					state->combination_keye_repetition = 1;
				}
			}
			break;
	}

	/* Track physical key hold (for synthetic repeat timer) */
	if (key_state == LIBINPUT_KEY_STATE_PRESSED) {
		state->key_held = true;
	} else if (key_state == LIBINPUT_KEY_STATE_RELEASED) {
		state->key_held = false;
		state->repeat_state = 0;
	}

	state->last_was_release = (key_state == LIBINPUT_KEY_STATE_RELEASED);
	clock_gettime(CLOCK_MONOTONIC, &state->last_key);
	set_dirty(state);
#ifdef WSK_DEBUG
	debug_print_display(state);
#endif
}

static int libinput_open_restricted(const char *path, int flags, void *data) {
	int *fd = data;
	return devmgr_open(*fd, path);
}

static void libinput_close_restricted(int fd, void *data) {
	close(fd);
}

static const struct libinput_interface libinput_impl = {
	.open_restricted = libinput_open_restricted,
	.close_restricted = libinput_close_restricted,
};

static uint32_t parse_color(const char *color) {
	if (color[0] == '#') {
		++color;
	}

	int len = strlen(color);
	if (len != 6 && len != 8) {
		fprintf(stderr,
			"Invalid color %s, defaulting to color "
			"0xFFFFFFFF\n",
			color);
		return 0xFFFFFFFF;
	}
	uint32_t res = (uint32_t) strtoul(color, NULL, 16);
	if (strlen(color) == 6) {
		res = (res << 8) | 0xFF;
	}
	return res;
}

void clear_full_keylink(struct wsk_keypress *key, struct wsk_state *state) {
	WSK_TRACE("clear_full_keylink");
#ifdef WSK_DEBUG
	printf("\n");
	fflush(stdout);
#endif
	if (!key) {
		return;
	}
	while (key) {
		struct wsk_keypress *next = key->next;
		free(key);
		key = next;
	}
	state->combination_keye_repetition = 1;
	memset(state->current_combination_key, 0, sizeof(state->current_combination_key));
	memset(state->prev_combination_keye, 0, sizeof(state->prev_combination_keye));
	state->keys = NULL;
	set_dirty(state);
}

static const char *get_sock_path(char *buf, size_t bufsize) {
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (dir) {
		snprintf(buf, bufsize, "%s/wiv.sock", dir);
	} else {
		snprintf(buf, bufsize, "/tmp/wiv.sock");
	}
	return buf;
}

int main(int argc, char *argv[]) {
	struct wsk_state state = {0};
	if (devmgr_start(&state.devmgr, &state.devmgr_pid, INPUTDEVPATH) > 0) {
		return 1;
	}

	int ret = 0;

	bool want_pause = false;
	bool want_resume = false;

	state.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	state.margin = 32;
	state.background = COLOR_BACKGROUND;
	state.specialfg = COLOR_SPECIAL_FG;
	state.repeatfg = COLOR_REPEAT_FG;
	state.foreground = COLOR_FOREGROUND;
	state.font = DEFAULT_FONT;
	state.timeout = 200;
	state.length_limit = 100;
	state.ctrl_l_hold = 0;
	state.ctrl_r_hold = 0;
	state.alt_l_hold = 0;
	state.alt_r_hold = 0;
	state.super_l_hold = 0;
	state.supre_r_hold = 0;
	state.shift_l_hold = 0;
	state.shift_r_hold = 0;
	state.combination_keye_repetition = 1;
	state.key_held = false;
	state.last_repeat_time.tv_sec = 0;
	state.last_repeat_time.tv_nsec = 0;
	state.repeat_state = 0;
	state.min_height = DISPLAY_MIN_HEIGHT;
	state.mask = (struct mask_state) {0};
	state.inspect = false;
	state.resize_pending = false;
	state.last_was_release = true;
	state.sock_fd = -1;
	state.paused = false;
	state.sock_path[0] = '\0';

	int c;
	while ((c = getopt(argc, argv, "hib:f:s:r:F:t:a:m:o:l:D:H:PR")) != -1) {
		switch (c) {
			case 'l':
				state.length_limit = atoi(optarg);
				break;
			case 'b':
				state.background = parse_color(optarg);
				break;
			case 'f':
				state.foreground = parse_color(optarg);
				break;
			case 's':
				state.specialfg = parse_color(optarg);
				break;
			case 'r':
				state.repeatfg = parse_color(optarg);
				break;
			case 'F':
				state.font = optarg;
				break;
			case 't':
				state.timeout = atoi(optarg);
				break;
			case 'a':
				if (strcmp(optarg, "top") == 0) {
					state.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
				} else if (strcmp(optarg, "left") == 0) {
					state.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
				} else if (strcmp(optarg, "right") == 0) {
					state.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
				} else if (strcmp(optarg, "bottom") == 0) {
					state.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
				}
				break;
			case 'm':
				state.margin = atoi(optarg);
				break;
			case 'i':
				state.inspect = true;
				break;
			case 'o':
				state.output_mode = OUTPUT_PINNED;
				strncpy(state.target_output_name, optarg, sizeof(state.target_output_name) - 1);
				break;
#ifdef WSK_DEBUG
			case 'D':
				trace_file = fopen(optarg, "w");
				if (!trace_file) {
					fprintf(stderr, "Failed to open trace file: %s\n", optarg);
					return 1;
				}
			WSK_TRACE("trace started pid=%d", getpid());
				break;
		#endif
			case 'H':
				state.min_height = (uint32_t)atoi(optarg);
				break;
			case 'P':
				state.paused = true;
				want_pause = true;
				break;
			case 'R':
				state.paused = false;
				want_resume = true;
				break;
			default:
				fprintf(stderr, "usage: wshowkeys [-b|-f|-s|-r #RRGGBB[AA]] [-F font] "
						"[-t timeout]\n\t[-a top|left|right|bottom] [-m margin] "
						"[-o output] [-l numOfLengthLimit] [-H padding] [-i] [-P] [-R]");
				return 1;
		}
	}

	/* IPC socket setup for pause/resume */
	{
		char path_buf[256];
		const char *path = get_sock_path(path_buf, sizeof(path_buf));
		strncpy(state.sock_path, path, sizeof(state.sock_path) - 1);
		state.sock_path[sizeof(state.sock_path) - 1] = '\0';

		state.sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (state.sock_fd < 0) {
			fprintf(stderr, "socket: %s\n", strerror(errno));
			ret = 1;
			goto exit;
		}

		struct sockaddr_un addr = {0};
		addr.sun_family = AF_UNIX;
		char buf[256];
		snprintf(buf, sizeof(buf), "%s", path);
		memcpy(addr.sun_path, buf, strlen(buf) + 1);

		if (bind(state.sock_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
			if (listen(state.sock_fd, 5) < 0) {
				fprintf(stderr, "listen: %s\n", strerror(errno));
				close(state.sock_fd);
				state.sock_fd = -1;
				ret = 1;
				goto exit;
			}
		} else {
			/* Bind failed — check if another instance is running */
			int conn_fd = socket(AF_UNIX, SOCK_STREAM, 0);
			if (conn_fd < 0) {
				fprintf(stderr, "socket: %s\n", strerror(errno));
				close(state.sock_fd);
				state.sock_fd = -1;
				ret = 1;
				goto exit;
			}
			if (connect(conn_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
				if (want_pause) {
					char cmd = 'P';
					write(conn_fd, &cmd, 1);
					close(conn_fd);
					close(state.sock_fd);
					return 0;
				}
				if (want_resume) {
					char cmd = 'R';
					write(conn_fd, &cmd, 1);
					close(conn_fd);
					close(state.sock_fd);
					return 0;
				}
				fprintf(stderr, "wiv: already running, use wiv -P to pause or wiv -R to resume\n");
				close(conn_fd);
				close(state.sock_fd);
				state.sock_fd = -1;
				ret = 1;
				goto exit;
			}
			close(conn_fd);
			/* No other instance — stale socket, unlink and retry */
			unlink(path);
			if (bind(state.sock_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
				fprintf(stderr, "bind: %s\n", strerror(errno));
				close(state.sock_fd);
				state.sock_fd = -1;
				ret = 1;
				goto exit;
			}
			if (listen(state.sock_fd, 5) < 0) {
				fprintf(stderr, "listen: %s\n", strerror(errno));
				close(state.sock_fd);
				state.sock_fd = -1;
				ret = 1;
				goto exit;
			}
		}
	}

	state.repeat_font = scale_font_size(state.font, REPEAT_FONT_SCALE);

	const char *mask_env = getenv("WIV_MASK");
	if (mask_env) {
		char *env_copy = strdup(mask_env);
		if (env_copy) {
			char *tok = strtok(env_copy, ",");
			while (tok && state.mask.num_patterns < MASK_PATTERNS_MAX) {
				strncpy(state.mask.patterns[state.mask.num_patterns], tok, 255);
				state.mask.patterns[state.mask.num_patterns][255] = '\0';
				state.mask.num_patterns++;
				tok = strtok(NULL, ",");
			}
			free(env_copy);
		}
	}

	state.udev = udev_new();
	if (!state.udev) {
		fprintf(stderr, "udev_create: %s\n", strerror(errno));
		ret = 1;
		goto exit;
	}

	state.libinput = libinput_udev_create_context(&libinput_impl, &state.devmgr, state.udev);
	udev_unref(state.udev);
	if (!state.libinput) {
		fprintf(stderr, "libinput_udev_create_context: %s\n", strerror(errno));
		ret = 1;
		goto exit;
	}

	state.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!state.xkb_context) {
		fprintf(stderr, "xkb_context_new: %s\n", strerror(errno));
		ret = 1;
		goto exit;
	}

	state.display = wl_display_connect(NULL);
	if (!state.display) {
		fprintf(stderr, "wl_display_connect: %s\n", strerror(errno));
		ret = 1;
		goto exit;
	}

	state.registry = wl_display_get_registry(state.display);
	assert(state.registry);
	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	struct {
		const char *name;
		void *ptr;
	} need_globals[] = {
		"wl_compositor", &state.compositor, "wl_shm",	       &state.shm,
		"wl_seat",	 &state.seat,	    "wlr_layer_shell", &state.layer_shell,
	};
	for (size_t i = 0; i < sizeof(need_globals) / sizeof(need_globals[0]); ++i) {
		if (!need_globals[i].ptr) {
			fprintf(stderr,
				"Error: required Wayland interface '%s' "
				"is not present\n",
				need_globals[i].name);
			ret = 1;
			goto exit;
		}
	}

	wl_seat_add_listener(state.seat, &wl_seat_listener, &state);
	wl_display_roundtrip(state.display);

	// Resolve startup output for PINNED mode
	struct wl_output *startup_output = NULL;
	if (state.output_mode == OUTPUT_PINNED) {
		struct wsk_output *wsk_out = state.outputs;
		while (wsk_out) {
			if (strcmp(wsk_out->name, state.target_output_name) == 0) {
				startup_output = wsk_out->output;
				break;
			}
			wsk_out = wsk_out->next;
		}
		if (!startup_output) {
			fprintf(stderr, "output '%s' not found, using default\n", state.target_output_name);
			state.output_mode = OUTPUT_DEFAULT;
		}
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	assert(state.surface);
	wl_surface_add_listener(state.surface, &wl_surface_listener, &state);

	struct wl_output *layer_output = NULL;
	if (state.output_mode == OUTPUT_PINNED)
		layer_output = startup_output;

	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell, state.surface, layer_output,
								    ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "showkeys");
	assert(state.layer_surface);

	// 创建空的输入区域
	struct wl_region *input_region = wl_compositor_create_region(state.compositor);
	wl_surface_set_input_region(state.surface, input_region);
	wl_region_destroy(input_region); // 销毁region，因为surface已经复制了一份

	zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, &state);
	zwlr_layer_surface_v1_set_size(state.layer_surface, 1, 1);
	zwlr_layer_surface_v1_set_anchor(state.layer_surface, state.anchor);
	zwlr_layer_surface_v1_set_margin(state.layer_surface, state.margin, state.margin, state.margin, state.margin);
	zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, -1);
	wl_surface_commit(state.surface);

	struct pollfd pollfds[3];
	int npollfds = 2;
	pollfds[0] = (struct pollfd){
		.fd = libinput_get_fd(state.libinput),
		.events = POLLIN,
	};
	pollfds[1] = (struct pollfd){
		.fd = wl_display_get_fd(state.display),
		.events = POLLIN,
	};
	if (state.sock_fd >= 0) {
		pollfds[2] = (struct pollfd){
			.fd = state.sock_fd,
			.events = POLLIN,
		};
		npollfds = 3;
	}

	state.run = true;
	while (state.run) {
		errno = 0;
		do {
			if (wl_display_flush(state.display) == -1 && errno != EAGAIN) {
				fprintf(stderr, "wl_display_flush: %s\n", strerror(errno));
				break;
			}
		} while (errno == EAGAIN);

		int timeout = -1;
		if (!state.paused) {
			if (state.keys) {
				timeout = 200;
			}
			if (state.key_held && (timeout == -1 || REPEAT_RATE < timeout))
				timeout = REPEAT_RATE;  /* Wake up often during key hold */
		}

		if (poll(pollfds, npollfds, timeout) < 0) {
			fprintf(stderr, "poll: %s\n", strerror(errno));
			break;
		}
		WSK_TRACE("poll returned revents: libinput=0x%x wayland=0x%x timeout=%d",
			pollfds[0].revents, pollfds[1].revents, timeout);

		/* Check for fd errors */
		if (pollfds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
			fprintf(stderr, "poll fd error: libinput fd revents=%#x\n", pollfds[0].revents);
			break;
		}
		if (pollfds[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
			fprintf(stderr, "poll fd error: Wayland fd revents=%#x\n", pollfds[1].revents);
			break;
		}
		if (npollfds == 3 && (pollfds[2].revents & (POLLHUP | POLLERR | POLLNVAL))) {
			fprintf(stderr, "poll fd error: IPC socket revents=%#x\n", pollfds[2].revents);
			break;
		}

		if (!state.paused) {
			/* Clear out old keys */
			struct timespec now;
			struct wsk_keypress *key = state.keys;
			int all_key_len = 0;

			clock_gettime(CLOCK_MONOTONIC, &now);
			long elapsed_ns =
				(now.tv_sec - state.last_key.tv_sec) * 1000000000L + (now.tv_nsec - state.last_key.tv_nsec);
			WSK_TRACE("timeout check: last_was_release=%d elapsed_ms=%ld timeout=%d keys=%p",
				state.last_was_release, elapsed_ns / 1000000, state.timeout, (void*)key);
			if (state.last_was_release && elapsed_ns > (long) state.timeout * 1000000L) {
				if (state.keys) {
					clear_full_keylink(key, &state);
					for (int i = 0; i < state.mask.buffer_len; i++)
						free(state.mask.buffer[i]);
					mask_reset(&state.mask);
				}
			} else {
				const char *prev_display = NULL;
				while (key) {
					const char *display;
					if (state.inspect) {
						display = key->name;
					} else {
						const KeymapEntry *entry = keymap_entry(key->name);
						if (entry) {
							display = entry->display ? entry->display
										 : (key->utf8[0] ? key->utf8 : key->name);
						} else if (key->utf8[0]) {
							display = key->utf8;
						} else {
							display = key->name;
						}
					}
					const char *pad_before = (prev_display && prev_display[strlen(prev_display) - 1] == '+')
									 ? ""
									 : KEY_PAD_BEFORE;
					all_key_len += strlen(pad_before) + strlen(display) + strlen(KEY_PAD_AFTER);
					prev_display = display;
					struct wsk_keypress *next = key->next;
					key = next;
				}
				if (all_key_len > state.length_limit) {
					key = state.keys;
					struct wsk_keypress *next = key->next;
					free(key);
					state.keys = next;
					set_dirty(&state);
				}
			}
		}

		if ((pollfds[0].revents & POLLIN)) {
			if (libinput_dispatch(state.libinput) != 0) {
				fprintf(stderr, "libinput_dispatch: %s\n", strerror(errno));
				break;
			}
			struct libinput_event *event;
			while ((event = libinput_get_event(state.libinput))) {
				if (!state.paused) {
					handle_libinput_event(&state, event);
				}
				libinput_event_destroy(event);
				WSK_TRACE("libinput event processed");
			}
		}

		if (!state.paused) {
			/* Synthetic repeat for held key (libinput drops kernel auto-repeat) */
			if (state.key_held && state.prev_combination_keye[0] != '\0') {
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				long elapsed_ms = (now.tv_sec - state.last_key.tv_sec) * 1000L
						+ (now.tv_nsec - state.last_key.tv_nsec) / 1000000L;

				if (elapsed_ms >= REPEAT_DELAY) {
					if (state.repeat_state == 0) {
						state.repeat_state = 1;
						state.last_repeat_time = now;
					}
					if (state.repeat_state == 1) {
						/* First repeat after delay */
						state.repeat_state = 2;
						state.last_repeat_time = now;
						generate_held_key_repeat(&state);
					} else if (state.repeat_state == 2) {
						long repeat_elapsed = (now.tv_sec - state.last_repeat_time.tv_sec) * 1000L
									+ (now.tv_nsec - state.last_repeat_time.tv_nsec) / 1000000L;
						if (repeat_elapsed >= REPEAT_RATE) {
							state.last_repeat_time = now;
							generate_held_key_repeat(&state);
						}
					}
				}
			}
		}

		if ((pollfds[1].revents & POLLIN)) {
			int ret_dispatch = wl_display_dispatch(state.display);
			WSK_TRACE("wl_display_dispatch returned %d", ret_dispatch);
			if (ret_dispatch == -1) {
				fprintf(stderr, "wl_display_dispatch: %s\n", strerror(errno));
				break;
			}
		}

		/* Handle IPC socket connections (pause/resume signaling) */
		if (npollfds == 3 && (pollfds[2].revents & POLLIN)) {
			int client_fd = accept(state.sock_fd, NULL, NULL);
			if (client_fd >= 0) {
				char cmd = 0;
				ssize_t _nr = read(client_fd, &cmd, 1);
				if (_nr == 1) {
					if (cmd == 'P') {
						clear_full_keylink(state.keys, &state);
						state.paused = true;
						if (surface_is_configured(&state) && state.surface) {
							render_frame(&state);
						}
					} else if (cmd == 'R') {
						state.paused = false;
					}
				}
				close(client_fd);
			}
		}

		if (!state.paused) {
			WSK_TRACE("pre-render dirty=%d configured=%d keys=%p",
				state.dirty, surface_is_configured(&state), (void*)state.keys);
			if (state.dirty) {
				state.dirty = false;
				if (surface_is_configured(&state) && state.surface) {
					render_frame(&state);
				}
			}
		}
	}

	if (state.recording_cairo) {
		cairo_destroy(state.recording_cairo);
	}
	if (state.recording) {
		cairo_surface_destroy(state.recording);
	}
	if (state.font_options) {
		cairo_font_options_destroy(state.font_options);
	}
	destroy_buffer(&state.buffer_pool[0]);
	destroy_buffer(&state.buffer_pool[1]);
	WSK_TRACE("shutting down");
#ifdef WSK_DEBUG
	if (trace_file) {
		fclose(trace_file);
	}
#endif
	free(state.repeat_font);
exit:
	if (state.sock_fd >= 0) {
		close(state.sock_fd);
	}
	unlink(state.sock_path);
	wl_display_disconnect(state.display);
	libinput_unref(state.libinput);
	devmgr_finish(state.devmgr, state.devmgr_pid);
	return ret;
}
