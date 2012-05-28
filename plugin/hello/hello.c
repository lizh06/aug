#include "aug_plugin.h"

const char aug_plugin_name[] = "hello";

static const struct aug_api *g_api;
static struct aug_plugin *g_plugin;

int aug_plugin_init(struct aug_plugin *plugin, const struct aug_api *api) {
	g_plugin = plugin;	
	g_api = api;

	(*g_api->log)(g_plugin, "hello world\n");

	return 0;
}

void aug_plugin_free() {
	(*g_api->log)(g_plugin, "goodbye world\n");
}