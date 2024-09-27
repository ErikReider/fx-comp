#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "comp/object.h"
#include "comp/server.h"
#include "comp/transaction.h"
#include "constants.h"
#include "desktop/toplevel.h"

void comp_toplevel_state_print(struct comp_toplevel_state *state,
							   const char *str) {
	printf("%s: %i %i %i %i\n", str, state->width, state->height, state->x,
		   state->y);
}

/*
 * Transactions
 *
 * Thanks Sway for the overkill but effective transaction system! :D
 */

static struct comp_transaction *transaction_create(void) {
	struct comp_transaction *transaction = calloc(1, sizeof(*transaction));
	if (!transaction) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_transaction");
		return NULL;
	}

	wl_list_init(&transaction->instructions);

	return transaction;
}

static void transaction_destroy(struct comp_transaction *transaction) {
	// Free instructions
	struct comp_transaction_instruction *instruction, *tmp;
	wl_list_for_each_reverse_safe(instruction, tmp, &transaction->instructions,
								  transaction_link) {
		struct comp_object *object = instruction->object;
		object->num_txn_refs--;
		if (object->instruction == instruction) {
			object->instruction = NULL;
		}
		wl_list_remove(&instruction->transaction_link);
		free(instruction);
	}

	if (transaction->timer) {
		wl_event_source_remove(transaction->timer);
	}
	free(transaction);
}

static void transaction_add_node(struct comp_transaction *transaction,
								 struct comp_object *object,
								 bool server_request) {
	struct comp_transaction_instruction *instruction = NULL;

	// Check if we have an instruction for this node already, in which case we
	// update that instead of creating a new one.
	if (object->num_txn_refs > 0) {
		struct comp_transaction_instruction *other;
		wl_list_for_each_reverse(other, &transaction->instructions,
								 transaction_link) {
			if (other->object == object) {
				instruction = other;
				break;
			}
		}
	}

	if (!instruction) {
		instruction = calloc(1, sizeof(struct comp_transaction_instruction));
		if (!instruction) {
			wlr_log(WLR_ERROR, "Unable to allocate instruction");
			return;
		}
		instruction->transaction = transaction;
		instruction->object = object;
		instruction->server_request = server_request;

		wl_list_insert(&transaction->instructions,
					   &instruction->transaction_link);
		object->num_txn_refs++;
	} else if (server_request) {
		instruction->server_request = true;
	}

	// Copy toplevel state
	switch (object->type) {
	case COMP_OBJECT_TYPE_OUTPUT:
	case COMP_OBJECT_TYPE_WORKSPACE:
	case COMP_OBJECT_TYPE_UNMANAGED:
	case COMP_OBJECT_TYPE_XDG_POPUP:
	case COMP_OBJECT_TYPE_LAYER_SURFACE:
	case COMP_OBJECT_TYPE_WIDGET:
		break;
	case COMP_OBJECT_TYPE_TOPLEVEL:;
		struct comp_toplevel *toplevel = object->data;
		instruction->state = toplevel->pending_state;
		break;
	}
}

static void transaction_apply(struct comp_transaction *transaction) {
	wlr_log(WLR_DEBUG, "Applying transaction %p", transaction);

	if (server.debug.log_txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *commit = &transaction->commit_time;
		float ms = (now.tv_sec - commit->tv_sec) * 1000 +
				   (now.tv_nsec - commit->tv_nsec) / 1000000.0;
		wlr_log(WLR_DEBUG,
				"Transaction %p: %.1fms waiting "
				"(%.1f frames if 60Hz)",
				transaction, ms, ms / (1000.0f / 60));
	}

	// Apply the instruction state to the object's current state
	struct comp_transaction_instruction *instruction;
	wl_list_for_each_reverse(instruction, &transaction->instructions,
							 transaction_link) {
		struct comp_object *object = instruction->object;

		switch (object->type) {
		case COMP_OBJECT_TYPE_OUTPUT:
		case COMP_OBJECT_TYPE_WORKSPACE:
		case COMP_OBJECT_TYPE_UNMANAGED:
		case COMP_OBJECT_TYPE_XDG_POPUP:
		case COMP_OBJECT_TYPE_LAYER_SURFACE:
		case COMP_OBJECT_TYPE_WIDGET:
			break;
		case COMP_OBJECT_TYPE_TOPLEVEL:;
			struct comp_toplevel *toplevel = object->data;
			if (!toplevel || object->destroying) {
				break;
			}
			toplevel->state.width = instruction->state.width;
			toplevel->state.height = instruction->state.height;
			toplevel->state.x = instruction->state.x;
			toplevel->state.y = instruction->state.y;

			toplevel->pending_state = toplevel->state;

			comp_toplevel_run_transaction(toplevel);
			break;
		}

		object->instruction = NULL;
	}
}

static void transaction_commit_pending(void);

static void transaction_progress(void) {
	if (!server.queued_transaction) {
		return;
	}
	if (server.queued_transaction->num_waiting > 0) {
		return;
	}
	transaction_apply(server.queued_transaction);
	transaction_destroy(server.queued_transaction);
	server.queued_transaction = NULL;

	if (!server.pending_transaction) {
		return;
	}

	transaction_commit_pending();
}

static int timed_out_func(void *data) {
	struct comp_transaction *transaction = data;
	wlr_log(WLR_DEBUG, "Transaction %p timed out (%zi waiting)", transaction,
			transaction->num_waiting);
	transaction->num_waiting = 0;
	transaction_progress();
	return 0;
}

static bool should_configure(struct comp_toplevel *toplevel,
							 struct comp_transaction_instruction *instruction) {
	if (toplevel->object.destroying) {
		return false;
	}

	if (!instruction->server_request) {
		return false;
	}

	if (toplevel->type == COMP_TOPLEVEL_TYPE_XWAYLAND) {
		if (toplevel->state.x != instruction->state.x ||
			toplevel->state.y != instruction->state.y) {
			return true;
		}
	}
	if (toplevel->state.width != instruction->state.width ||
		toplevel->state.height != instruction->state.height) {
		return true;
	}
	return false;
}

static void transaction_commit(struct comp_transaction *transaction) {
	wlr_log(WLR_DEBUG, "Transaction %p committing with %i instructions",
			transaction, wl_list_length(&transaction->instructions));
	transaction->num_waiting = 0;
	struct comp_transaction_instruction *instruction;
	wl_list_for_each_reverse(instruction, &transaction->instructions,
							 transaction_link) {
		struct comp_object *object = instruction->object;
		if (object->type == COMP_OBJECT_TYPE_TOPLEVEL &&
			should_configure(object->data, instruction)) {
			struct comp_toplevel *toplevel = object->data;
			instruction->serial = comp_toplevel_configure(
				toplevel, instruction->state.width, instruction->state.height,
				instruction->state.x, instruction->state.y);

			bool hidden =
				object->destroying && !object->scene_tree->node.enabled;
			if (!hidden) {
				instruction->ready = false;
				++transaction->num_waiting;
			}

			comp_toplevel_send_frame_done(toplevel);
		}
		object->instruction = instruction;
	}

	transaction->num_configures = transaction->num_waiting;
	if (server.debug.log_txn_timings) {
		clock_gettime(CLOCK_MONOTONIC, &transaction->commit_time);
	}

	if (transaction->num_waiting) {
		// Set up a timer which the views must respond within
		transaction->timer = wl_event_loop_add_timer(
			server.wl_event_loop, timed_out_func, transaction);
		if (transaction->timer) {
			wl_event_source_timer_update(transaction->timer,
										 TRANSACTION_TIME_MS);
		} else {
			wlr_log(WLR_ERROR, "Unable to create transaction timer "
							   "(some imperfect frames might be rendered)");
			transaction->num_waiting = 0;
		}
	}
}

static void transaction_commit_pending(void) {
	if (server.queued_transaction) {
		return;
	}
	struct comp_transaction *transaction = server.pending_transaction;
	server.pending_transaction = NULL;
	server.queued_transaction = transaction;
	transaction_commit(transaction);
	transaction_progress();
}

void comp_transaction_commit_dirty(bool server_request) {
	if (wl_list_empty(&server.dirty_objects)) {
		return;
	}

	if (!server.pending_transaction) {
		server.pending_transaction = transaction_create();
		if (!server.pending_transaction) {
			return;
		}
	}

	struct comp_object *object, *tmp;
	wl_list_for_each_reverse_safe(object, tmp, &server.dirty_objects,
								  dirty_link) {
		if (object->destroying) {
			continue;
		}
		wl_list_remove(&object->dirty_link);
		transaction_add_node(server.pending_transaction, object,
							 server_request);
		object->dirty = false;
	}

	transaction_commit_pending();
}

void comp_transaction_instruction_mark_ready(
	struct comp_transaction_instruction *instruction) {
	struct comp_transaction *transaction = instruction->transaction;

	if (server.debug.log_txn_timings) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		struct timespec *start = &transaction->commit_time;
		float ms = (now.tv_sec - start->tv_sec) * 1000 +
				   (now.tv_nsec - start->tv_nsec) / 1000000.0;
		wlr_log(WLR_DEBUG, "Transaction %p: %zi/%zi ready in %.1fms (%s)",
				transaction,
				transaction->num_configures - transaction->num_waiting + 1,
				transaction->num_configures, ms,
				comp_toplevel_get_title(instruction->object->data));
	}

	// If the transaction has timed out then its num_waiting will be 0 already.
	if (!instruction->ready && transaction->num_waiting > 0 &&
		--transaction->num_waiting == 0) {
		wlr_log(WLR_DEBUG, "Transaction %p is ready", transaction);
		// Cancel the timer
		wl_event_source_timer_update(transaction->timer, 0);
	}

	instruction->object->instruction = NULL;
	transaction_progress();
}
