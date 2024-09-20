#ifndef FX_COMP_TRANSACTION_H
#define FX_COMP_TRANSACTION_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/util/box.h>

/*
 * Transaction Manager
 */

#define TRANSACTION_TIME_MS 200

struct comp_transaction_mgr {
	struct wl_event_source *tick;

	struct wl_list clients;
};

struct comp_transaction_mgr *comp_transaction_mgr_init(void);

void comp_transaction_mgr_destroy(struct comp_transaction_mgr *mgr);

/*
 * Transaction
 */

struct comp_transaction {
	struct wl_list link;

	bool inited;
	bool ready;

	void *data;

	const struct comp_transaction_impl *impl;
};

struct comp_transaction_impl {
	/** Run the transaction, return true to remove from transaction queue */
	bool (*run)(struct comp_transaction_mgr *mgr,
				struct comp_transaction *client);
};

void comp_transaction_init(struct comp_transaction_mgr *mgr,
						   struct comp_transaction *client,
						   const struct comp_transaction_impl *impl,
						   void *data);
void comp_transaction_add(struct comp_transaction_mgr *mgr,
						  struct comp_transaction *client);
void comp_transaction_remove(struct comp_transaction *client);
void comp_transaction_run_now(struct comp_transaction_mgr *mgr,
							  struct comp_transaction *client);

#endif // !FX_COMP_TRANSACTION_H
