#ifndef FX_COMP_OBJECT_H
#define FX_COMP_OBJECT_H

#include <scenefx/types/wlr_scene.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_subcompositor.h>

#include "comp/server.h"

enum comp_object_type {
	COMP_OBJECT_TYPE_OUTPUT,
	COMP_OBJECT_TYPE_WORKSPACE,
	COMP_OBJECT_TYPE_TOPLEVEL,
	COMP_OBJECT_TYPE_UNMANAGED,
	COMP_OBJECT_TYPE_XDG_POPUP,
	COMP_OBJECT_TYPE_LAYER_SURFACE,
	COMP_OBJECT_TYPE_WIDGET,
	COMP_OBJECT_TYPE_LOCK_OUTPUT, // ext-session-lock-v1 output
	COMP_OBJECT_TYPE_DND_ICON,
	COMP_OBJECT_TYPE_SAVED_OBJECT,
};

struct comp_object {
	// The root of the toplevel/layer_surface/widget
	struct wlr_scene_tree *scene_tree;
	// Used to display the actual content
	struct wlr_scene_tree *content_tree;
	// Used for saved scene buffers
	struct wlr_scene_tree *saved_tree;

	enum comp_object_type type;
	// The pointer to the ancestor which is type of `comp_object_type`
	void *data;

	bool destroying;

	struct wl_list transaction_link;
	struct comp_transaction_instruction *instruction;
	size_t num_txn_refs;
	struct wl_list dirty_link;
	bool dirty;
};

struct comp_object *comp_object_at(struct comp_server *server, double lx,
								   double ly, double *sx, double *sy,
								   struct wlr_scene_buffer **scene_buffer,
								   struct wlr_surface **surface);

void comp_object_save_buffer(struct comp_object *object);
void comp_object_remove_buffer(struct comp_object *object);

void comp_object_mark_dirty(struct comp_object *object);

#endif // !FX_COMP_OBJECT_H
