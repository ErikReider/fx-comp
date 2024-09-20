#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/util/log.h>

#include "comp/server.h"
#include "comp/transaction.h"

/*
 * Animation Client
 */

void comp_transaction_init(struct comp_transaction_mgr *mgr,
						   struct comp_transaction *client,
						   const struct comp_transaction_impl *impl,
						   void *data) {
	if (!client) {
		wlr_log(WLR_ERROR, "comp_transaction is NULL!");
		return;
	}

	client->inited = false;
	client->impl = impl;
	client->data = data;
}

void comp_transaction_remove(struct comp_transaction *client) {
	if (client->inited) {
		wl_list_remove(&client->link);
		client->inited = false;
	}
	client->ready = false;
}

void comp_transaction_add(struct comp_transaction_mgr *mgr,
						  struct comp_transaction *client) {
	comp_transaction_remove(client);
	client->inited = true;

	wl_list_insert(&mgr->clients, &client->link);
}

void comp_transaction_run_now(struct comp_transaction_mgr *mgr,
							  struct comp_transaction *client) {
	client->ready = true;
	if (!client->impl || !client->impl->run || client->impl->run(mgr, client)) {
		comp_transaction_remove(client);
	}
}

/*
 * Transaction Manager
 */

static int timer_func(void *data) {
	struct comp_transaction_mgr *mgr = data;
	wl_event_source_timer_update(mgr->tick, TRANSACTION_TIME_MS);

	struct comp_transaction *client, *tmp;
	wl_list_for_each_safe(client, tmp, &mgr->clients, link) {
		comp_transaction_run_now(mgr, client);
	}

	return 0;
}

struct comp_transaction_mgr *comp_transaction_mgr_init(void) {
	struct comp_transaction_mgr *mgr = calloc(1, sizeof(*mgr));
	if (!mgr) {
		wlr_log(WLR_ERROR, "Failed to allocate comp_transaction");
		return NULL;
	}

	mgr->tick = wl_event_loop_add_timer(server.wl_event_loop, timer_func, mgr);

	wl_list_init(&mgr->clients);

	wl_event_source_timer_update(mgr->tick, 1);
	return mgr;
}

void comp_transaction_mgr_destroy(struct comp_transaction_mgr *mgr) {
	wl_event_source_remove(mgr->tick);

	free(mgr);
}
