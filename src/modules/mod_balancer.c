
#include <lighttpd/base.h>

typedef enum {
	BE_ALIVE,
	BE_OVERLOADED,
	BE_DOWN,
	BE_DOWN_RETRY
} backend_state;

typedef enum {
	BAL_ALIVE,
	BAL_OVERLOADED,
	BAL_DOWN
} balancer_state;

struct backend {
	action *act;
	guint load;
	backend_state state;
};
typedef struct backend backend;

struct balancer {
	GArray *backends;
	balancer_state state;
};
typedef struct balancer balancer;

static balancer* balancer_new() {
	balancer *b = g_slice_new(balancer);
	b->backends = g_array_new(FALSE, TRUE, sizeof(backend));

	return b;
}

static void balancer_free(server *srv, balancer *b) {
	guint i;
	if (!b) return;
	for (i = 0; i < b->backends->len; i++) {
		backend *be = &g_array_index(b->backends, backend, i);
		action_release(srv, be->act);
	}
	g_array_free(b->backends, TRUE);
	g_slice_free(balancer, b);
}

static gboolean balancer_fill_backends(balancer *b, server *srv, value *val) {
	if (val->type == VALUE_ACTION) {
		backend be = { val->data.val_action.action, 0, BE_ALIVE };
		assert(srv == val->data.val_action.srv);
		action_acquire(be.act);
		g_array_append_val(b->backends, be);
		return TRUE;
	} else if (val->type == VALUE_LIST) {
		guint i;
		if (val->data.list->len == 0) {
			ERROR(srv, "%s", "expected non-empty list");
			return FALSE;
		}
		for (i = 0; i < val->data.list->len; i++) {
			value *oa = g_array_index(val->data.list, value*, i);
			if (oa->type != VALUE_ACTION) {
				ERROR(srv, "expected action at entry %u of list, got %s", i, value_type_string(oa->type));
				return FALSE;
			}
			assert(srv == oa->data.val_action.srv);
			backend be = { oa->data.val_action.action, 0, BE_ALIVE };
			action_acquire(be.act);
			g_array_append_val(b->backends, be);
		}
		return TRUE;
	} else {
		ERROR(srv, "expected list, got %s", value_type_string(val->type));
		return FALSE;
	}
}

static handler_t balancer_act_select(vrequest *vr, gboolean backlog_provided, gpointer param, gpointer *context) {
	balancer *b = (balancer*) param;
	gint be_ndx = 0;
	backend *be = &g_array_index(b->backends, backend, be_ndx);

	UNUSED(backlog_provided);

	/* TODO implement some selection algorithms */

	be->load++;
	action_enter(vr, be->act);
	*context = GINT_TO_POINTER(be_ndx);

	return HANDLER_GO_ON;
}

static handler_t balancer_act_fallback(vrequest *vr, gboolean backlog_provided, gpointer param, gpointer *context, backend_error error) {
	balancer *b = (balancer*) param;
	gint be_ndx = GPOINTER_TO_INT(context);
	backend *be = &g_array_index(b->backends, backend, be_ndx);

	UNUSED(backlog_provided);
	UNUSED(error);

	if (be_ndx < 0) return HANDLER_GO_ON;

	/* TODO implement fallback/backlog */

	be->load--;
	if (vrequest_handle_direct(vr))
		vr->response.http_status = 503;
	return HANDLER_GO_ON;
}

static handler_t balancer_act_finished(vrequest *vr, gpointer param, gpointer context) {
	balancer *b = (balancer*) param;
	gint be_ndx = GPOINTER_TO_INT(context);
	backend *be = &g_array_index(b->backends, backend, be_ndx);

	UNUSED(vr);

	if (be_ndx < 0) return HANDLER_GO_ON;

	/* TODO implement backlog */

	be->load--;
	return HANDLER_GO_ON;
}

static void balancer_act_free(server *srv, gpointer param) {
	balancer_free(srv, (balancer*) param);
}

static action* balancer_rr(server *srv, plugin* p, value *val) {
	balancer *b;
	action *a;
	UNUSED(p);

	if (!val) {
		ERROR(srv, "%s", "need parameter");
		return NULL;
	}

	b = balancer_new();
	if (!balancer_fill_backends(b, srv, val)) {
		balancer_free(srv, b);
		return NULL;
	}

	a = action_new_balancer(balancer_act_select, balancer_act_fallback, balancer_act_finished, balancer_act_free, b, TRUE);
	return a;
}


static const plugin_option options[] = {
	{ NULL, 0, NULL, NULL, NULL }
};

static const plugin_action actions[] = {
	{ "balancer.rr", balancer_rr },
	{ NULL, NULL }
};

static const plugin_setup setups[] = {
	{ NULL, NULL }
};


static void plugin_init(server *srv, plugin *p) {
	UNUSED(srv);

	p->options = options;
	p->actions = actions;
	p->setups = setups;
}


LI_API gboolean mod_balancer_init(modules *mods, module *mod) {
	MODULE_VERSION_CHECK(mods);

	mod->config = plugin_register(mods->main, "mod_balancer", plugin_init);

	return mod->config != NULL;
}

LI_API gboolean mod_balancer_free(modules *mods, module *mod) {
	if (mod->config)
		plugin_free(mods->main, mod->config);

	return TRUE;
}
