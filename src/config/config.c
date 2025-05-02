#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <cjson/cJSON.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>

#include "comp/server.h"
#include "config/config.h"

#define build_config_path(...)                                                 \
	g_build_path(G_DIR_SEPARATOR_S, G_DIR_SEPARATOR_S, __VA_ARGS__, "fx-comp", \
				 "config.json", NULL)

#define if_json_object(name, parent)                                           \
	cJSON *name = cJSON_GetObjectItemCaseSensitive(parent, #name);             \
	if (name && cJSON_IsObject(name))

#define json_setter(json, name, value, type, default_value)                    \
	value = default_value;                                                     \
	{                                                                          \
		cJSON *variable = cJSON_GetObjectItemCaseSensitive(json, name);        \
		type result;                                                           \
		if (json_try_get_##type(variable, &result)) {                          \
			value = result;                                                    \
		}                                                                      \
	}

static inline bool json_try_get_int(const cJSON *json, int *value) {
	if (json && cJSON_IsNumber(json)) {
		*value = cJSON_GetNumberValue(json);
		return true;
	}
	return false;
}

static inline bool json_try_get_double(const cJSON *json, double *value) {
	if (json && cJSON_IsNumber(json)) {
		*value = cJSON_GetNumberValue(json);
		return true;
	}
	return false;
}

static void initialize_config_values(struct comp_config *config, cJSON *json) {
	// Compositor
	if_json_object(compositor, json) {
		// Tiling
		if_json_object(tiling, compositor) {
			json_setter(tiling, "split-ratio", config->tiling_split_ratio,
						double, 0.5);
			// Gaps
			if_json_object(gaps, tiling) {
				json_setter(gaps, "inner", config->tiling_gaps_inner, int, 12);
				json_setter(gaps, "outer", config->tiling_gaps_outer, int, 12);
			}
		}
	}
}

static char **get_config_paths(char *custom_path, size_t *length) {
	// The number of predefined path locations
	const int known_dir_count = 3;

	char **sys_dirs = (char **)g_get_system_config_dirs();
	const size_t sys_dirs_len = g_strv_length(sys_dirs);
	*length = known_dir_count + sys_dirs_len;

	char **config_dirs = calloc(*length, sizeof(*config_dirs));
	if (custom_path) {
		config_dirs[0] = strdup(g_strstrip(custom_path));
	}
	config_dirs[1] = build_config_path(g_get_user_config_dir());
	// Fallback directory for Debian users
	config_dirs[2] = build_config_path("usr", "local", "etc", "xdg");
	for (size_t i = known_dir_count; i < sys_dirs_len && sys_dirs[i]; i++) {
		config_dirs[i] = build_config_path(sys_dirs[i]);
	}

	return config_dirs;
}

static cJSON *read_file(char *custom_path) {
	// Paths to look in
	size_t config_dirs_len;
	char **config_dirs = get_config_paths(custom_path, &config_dirs_len);

	cJSON *json = NULL;
	FILE *fd = NULL;
	wlr_log(WLR_ERROR, "Looking for config");
	for (size_t i = 0; i < config_dirs_len; i++) {
		if (!config_dirs[i] || access(config_dirs[i], R_OK) != 0) {
			continue;
		}
		wlr_log(WLR_ERROR, "- %s", config_dirs[i]);
		fd = fopen(config_dirs[i], "r");
		if (!fd) {
			continue;
		}

		// Get the file size
		int seek = fseek(fd, 0, SEEK_END);
		const long size = ftell(fd);
		if (seek < 0 || size < 0) {
			wlr_log(WLR_ERROR, "Could not determine the config file size");
			fclose(fd);
			continue;
		}
		fseek(fd, 0, SEEK_SET);

		char *buffer = malloc(size + 1);
		fread(buffer, size, 1, fd);
		fclose(fd);
		buffer[size] = 0;

		// Remove comments and parse the file contents
		cJSON_Minify(buffer);
		json = cJSON_Parse(buffer);
		free(buffer);

		if (!json) {
			const char *error_ptr = cJSON_GetErrorPtr();
			if (error_ptr) {
				wlr_log(WLR_ERROR, "Config Error:\n%s", error_ptr);
			}
			cJSON_Delete(json);
			continue;
		}

		wlr_log(WLR_DEBUG, "Using config file: %s", config_dirs[i]);
		goto done;
	}

	wlr_log(WLR_ERROR, "Could not find config file");
	json = NULL;

done:
	for (size_t i = 0; i < config_dirs_len; i++) {
		free(config_dirs[i]);
	}
	free(config_dirs);
	return json;
}

struct comp_config *comp_config_init(char *custom_config_path) {
	struct comp_config *config = calloc(1, sizeof(*config));
	if (!config) {
		wlr_log(WLR_ERROR, "Could not allocate comp_config");
		return NULL;
	}

	wl_list_init(&config->variables);

	char *path = NULL;
	if (custom_config_path) {
		path = strdup(custom_config_path);
	}
	cJSON *json = read_file(path);
	free(path);

	initialize_config_values(config, json);

	cJSON_Delete(json);
	return config;
}

void comp_config_destroy(void) {
	struct config_variable *variable, *tmp;
	wl_list_for_each_safe(variable, tmp, &server.config->variables, link) {
		config_variable_destroy(variable);
	}
	free(server.config);
	server.config = NULL;
}
