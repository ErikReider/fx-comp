#ifndef FX_COMP_CONFIG_CONFIG_H
#define FX_COMP_CONFIG_CONFIG_H

struct comp_config {
	float tiling_split_ratio;
	int tiling_gaps_inner;
	int tiling_gaps_outer;
};

struct comp_config *comp_config_init(char *custom_config_path);

#endif // !FX_COMP_CONFIG_CONFIG_H
