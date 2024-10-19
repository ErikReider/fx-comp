#ifndef FX_COMP_TRANSACTION_H
#define FX_COMP_TRANSACTION_H

#include <stdbool.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/util/box.h>

struct comp_toplevel_state {
	int x;
	int y;
	int width;
	int height;
};

void comp_toplevel_state_print(struct comp_toplevel_state *state,
							   const char *str);

/*
 * Transaction
 */

struct comp_transaction {
	struct wl_event_source *timer;

	struct wl_list instructions;
	size_t num_waiting;

	size_t num_configures;
	struct timespec commit_time;
};

struct comp_transaction_instruction {
	struct wl_list transaction_link;

	struct comp_transaction *transaction;
	struct comp_object *object;

	struct comp_toplevel_state state;
	uint32_t serial;
	bool ready;
	bool server_request;
};

void comp_transaction_commit_dirty(bool server_request);
void comp_transaction_instruction_mark_ready(
	struct comp_transaction_instruction *instruction);

#endif // !FX_COMP_TRANSACTION_H
