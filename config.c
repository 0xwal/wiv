#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "config.h"

static KeymapEntry *overrides = NULL;
static int override_count = 0;
static int override_capacity = 0;

static char *config_get_path(void) {
	const char *xdg_home = getenv("XDG_CONFIG_HOME");
	char buf[1024];
	if (xdg_home) {
		snprintf(buf, sizeof(buf), "%s/wiv/keymap", xdg_home);
	} else {
		const char *home = getenv("HOME");
		if (!home)
			return NULL;
		snprintf(buf, sizeof(buf), "%s/.config/wiv/keymap", home);
	}
	return strdup(buf);
}

static void free_entry_fields(KeymapEntry *e) {
	free((char *)e->name);
	free((char *)e->display);
	free((char *)e->fg);
	e->name = NULL;
	e->display = NULL;
	e->fg = NULL;
}

static void add_override(const char *name, const char *display, const char *fg) {
	if (override_count >= override_capacity) {
		override_capacity = override_capacity ? override_capacity * 2 : 16;
		overrides = realloc(overrides, (size_t)override_capacity * sizeof(KeymapEntry));
	}
	overrides[override_count] = (KeymapEntry){name, display, fg};
	override_count++;
}

static int parse_line(char *line, KeymapEntry *out, const char **err_msg) {
	char buf[1024];
	int bi = 0;
	int field_index = 0;
	char *fields[3] = {NULL, NULL, NULL};

	for (char *p = line; *p; p++) {
		char c = *p;
		if (c == '\\') {
			p++;
			if (!*p) {
				*err_msg = "trailing backslash";
				goto err;
			}
			if (*p == '|') {
				if (bi >= 1023) {
					*err_msg = "field too long";
					goto err;
				}
				buf[bi++] = '|';
			} else if (*p == '\\') {
				if (bi >= 1023) {
					*err_msg = "field too long";
					goto err;
				}
				buf[bi++] = '\\';
			} else {
				*err_msg = "invalid escape character";
				goto err;
			}
		} else if (c == '|') {
			if (field_index > 2) {
				*err_msg = "too many fields";
				goto err;
			}
			if (bi == 0) {
				fields[field_index] = NULL;
			} else {
				buf[bi] = '\0';
				fields[field_index] = strdup(buf);
				bi = 0;
			}
			field_index++;
		} else {
			if (bi >= 1023) {
				*err_msg = "field too long";
				goto err;
			}
			buf[bi++] = c;
		}
	}

	if (field_index > 2) {
		*err_msg = "too many fields";
		goto err;
	}
	if (bi == 0) {
		fields[field_index] = NULL;
	} else {
		buf[bi] = '\0';
		fields[field_index] = strdup(buf);
	}

	if (!fields[0] || fields[0][0] == '\0') {
		*err_msg = "empty key name";
		goto err;
	}

	out->name = fields[0];
	out->display = fields[1];
	out->fg = fields[2];
	return 0;

err:
	free(fields[0]);
	free(fields[1]);
	free(fields[2]);
	return -1;
}

static int parse_file(FILE *f, int validate_only, int *err_line, const char **err_msg) {
	char linebuf[2048];
	int line_num = 0;

	while (fgets(linebuf, sizeof(linebuf), f)) {
		line_num++;
		size_t len = strlen(linebuf);
		while (len > 0 && (linebuf[len - 1] == '\n' || linebuf[len - 1] == '\r'))
			linebuf[--len] = '\0';
		if (len == 0 || linebuf[0] == '#')
			continue;

		KeymapEntry entry = {0};
		if (parse_line(linebuf, &entry, err_msg) < 0) {
			*err_line = line_num;
			return -1;
		}
		if (!validate_only)
			add_override(entry.name, entry.display, entry.fg);
		else
			free_entry_fields(&entry);
	}
	return 0;
}

int config_load(void) {
	char *path = config_get_path();
	if (!path)
		return 0;
	FILE *f = fopen(path, "r");
	free(path);
	if (!f)
		return 0;

	int err_line;
	const char *err_msg;
	int ret = parse_file(f, 0, &err_line, &err_msg);
	fclose(f);

	if (ret < 0) {
		fprintf(stderr, "wiv: keymap config line %d: %s\n", err_line, err_msg);
		config_free();
	}
	return ret;
}

int config_validate(void) {
	char *path = config_get_path();
	if (!path) {
		printf("OK\n");
		return 0;
	}
	FILE *f = fopen(path, "r");
	free(path);
	if (!f) {
		printf("OK\n");
		return 0;
	}

	int err_line;
	const char *err_msg;
	int ret = parse_file(f, 1, &err_line, &err_msg);
	fclose(f);

	if (ret == 0) {
		printf("OK\n");
		return 0;
	}
	fprintf(stderr, "%d: %s\n", err_line, err_msg);
	return -1;
}

const KeymapEntry *config_lookup(const char *name) {
	for (int i = 0; i < override_count; i++) {
		if (strcmp(overrides[i].name, name) == 0)
			return &overrides[i];
	}
	return NULL;
}

void config_free(void) {
	for (int i = 0; i < override_count; i++)
		free_entry_fields(&overrides[i]);
	free(overrides);
	overrides = NULL;
	override_count = 0;
	override_capacity = 0;
}
