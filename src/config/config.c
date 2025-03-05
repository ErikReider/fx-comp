#include <assert.h>
#include <cjson/cJSON.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

#include "config/config.h"

#define KNOWN_DIR_COUNT 3
#define build_config_path(...)                                                 \
	g_build_path(G_DIR_SEPARATOR_S, G_DIR_SEPARATOR_S, __VA_ARGS__, "fx-comp", \
				 "config.json", NULL)

#define if_json_object(variable, parent, name)                                 \
	cJSON *variable = cJSON_GetObjectItemCaseSensitive(parent, name);          \
	if (variable && cJSON_IsObject(variable))

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
	//
	// Compositor
	//
	if_json_object(compositor, json, "compositor") {
		// Tiling
		if_json_object(tiling, compositor, "tiling") {
			json_setter(tiling, "split-ratio", config->tiling_split_ratio,
						double, 0.5);
			if_json_object(gaps, tiling, "gaps") {
				json_setter(gaps, "inner", config->tiling_gaps_inner, int, 12);
				json_setter(gaps, "outer", config->tiling_gaps_outer, int, 12);
			}
		}
	}
}

static cJSON *read_file(char *custom_path) {
	// Paths to look in
	char **sys_dirs = (char **)g_get_system_config_dirs();
	const size_t sys_dirs_len = g_strv_length(sys_dirs);
	const size_t config_dirs_len = KNOWN_DIR_COUNT + sys_dirs_len;

	if (custom_path) {
		g_strstrip(custom_path);
	}

	char **config_dirs = calloc(config_dirs_len, sizeof(*config_dirs));
	config_dirs[0] = custom_path;
	config_dirs[1] = build_config_path(g_get_user_config_dir());
	// Fallback directory for Debian users
	config_dirs[2] = build_config_path("usr", "local", "etc", "xdg");
	for (size_t i = 0; i < sys_dirs_len && sys_dirs[i]; i++) {
		config_dirs[KNOWN_DIR_COUNT + i] = build_config_path(sys_dirs[i]);
	}

	FILE *fd = NULL;
	wlr_log(WLR_ERROR, "Looking for config");
	for (size_t i = 0; i < config_dirs_len; i++) {
		if (!config_dirs[i]) {
			continue;
		}
		wlr_log(WLR_ERROR, "- %s", config_dirs[i]);
		fd = fopen(config_dirs[i], "r");
		if (!fd) {
			continue;
		}

		// Get the file size
		fseek(fd, 0, SEEK_END);
		const long size = ftell(fd);
		fseek(fd, 0, SEEK_SET);

		char *buffer = malloc(size + 1);
		fread(buffer, size, 1, fd);
		fclose(fd);
		buffer[size] = 0;

		// Remove comments and parse the file contents
		cJSON_Minify(buffer);
		cJSON *json = cJSON_Parse(buffer);
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
		free(config_dirs);
		return json;
	}

	free(config_dirs);
	wlr_log(WLR_ERROR, "Could not find config file");
	return NULL;
}

struct comp_config *comp_config_init(char *custom_config_path) {
	struct comp_config *config = calloc(1, sizeof(*config));
	if (!config) {
		wlr_log(WLR_ERROR, "Could not allocate comp_config");
		return NULL;
	}

	cJSON *json = read_file(custom_config_path);

	initialize_config_values(config, json);

	cJSON_Delete(json);
	return config;
}
