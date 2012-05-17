#ifndef AUG_AUG_H
#define AUG_AUG_H

#include <wchar.h>
#include <stdint.h>

/* defined below */
struct aug_api_t;
struct aug_plugin_t;

typedef enum aug_action_t = { AUG_ACT_OK = 0, AUG_ACT_CANCEL };

/* callbacks which a plugin can register
 * interest in. unless otherwise specified
 * the output parameters of all the callbacks
 * will be initialized to current/default 
 * values, so a simple { return; } function
 * pointer will be the same as a NULL 
 * callback. 
 */
struct aug_plugin_cb_t {
	/* called when a unit of input
	 * is received from stdin. the plugin can
	 * update the ch variable and set action
	 * to AUG_ACT_OK in order to change the
	 * data being sent to the terminal. or 
	 * the plugin can set action to 
	 * AUG_ACT_CANCEL to cause the input
	 * to be filtered from the terminal. */
	void (*input_char)(const struct aug_api_t *api, struct aug_plugin_t *plugin, 
			aug_action_t *action, int *ch);
	
	/* called when a cell on the screen is
	 * about to be updated. the plugin can
	 * alter (or not) any of the output 
	 * variables and then
	 * set action to AUG_ACTION_OK to modify
	 * the cell getting updated. or it can
	 * set action to AUG_ACTION_CANCEL and 
	 * and prevent the cell from being 
	 * updated. */
	void (*cell_update)(const struct aug_api_t *api, struct aug_plugin_t *plugin, 
			aug_action_t *action, int *row, int *col, wchar_t *wch, 
			attr_t *attr, int *color_pair);

	/* called when the cursor is about to be
	 * moved to some location on the screen.
	 * the plugin can change (or not) the 
	 * new_row, new_col output parameters
	 * and set action to AUG_ACT_OK to modify
	 * where the cursor is moved to. */
	void (*cursor_move)(const struct aug_api_t *api, struct aug_plugin_t *plugin,
			aug_action_t *action, int old_row, int old_col, int *new_row, int *new_col);

	/* called when the screen dimensions change.
	 * old_* hold the old dimensions and new_*
	 * hold the new dimensions. note that the
	 * dimensions are for the entire screen, not
	 * the size of the main window. */
	void (*screen_dims_change)(const struct aug_api_t *api, struct aug_plugin_t *plugin,
			int old_height, int old_width, int new_height, int new_width);
};

/* this structure represents the 
 * application's view of the plugin
 */
struct aug_plugin_t {
	/* name of the plugin. this name
	 * will be used for option parsing
	 * and configuration file parsing
	 * to automatically provide this 
	 * plugin with user specified
	 * configuration values. 
	 * this should ONLY be modified
	 * in the plugin_init function */
	char *name;
	
	/* callback subscriptions for this
	 * plugin. this structure will be
	 * initialized with null function
	 * ptrs and the plugin_init function
	 * should override the pointers for 
	 * which the plugin wants to receive
	 * callbacks.
	 * this should ONLY be modifed in
	 * the plugin_init function */
	struct aug_plugin_cb_t callbacks;
	
	/* a pointer to keep a private plugin
	 * data structure. the core application
	 * doesnt touch this, so the plugin can 
	 * modify it at any time. 	*/
	void *user;
};

/* passed to the plugin to provide 
 * an interface to the core application.
 * these functions are thread safe.
 */
struct aug_api_t {

	/* call this function to query for a
	 * user specified configuration value
	 * parsed from the command line or the
	 * configuration file. name is the name
	 * of the plugin, key is name of the 
	 * configuration variable, and val is 
	 * a pointer to the location of the
	 * variable value. if the return value
	 * is zero the key was successfully
	 * found. */
	int (*conf_val)(const char *name, const char *key, const void **val);
	
	/* query for the number of plugins
	 * on the plugin stack */
	void (*stack_size)(int *size);

	/* query for the position in the 
	 * plugin stack of a plugin by
	 * name. the plugin at position
	 *  zero gets the first callbacks
	 * and the plugin at the highest
	 * position gets callbacks last. 
	 * a non-zero return value means
	 * a plugin with name was not found */
	int (*stack_pos)(const char *name, int *pos);

	/* get the dimensions of the main terminal
	 * window */
	void (*term_win_dims)(int *rows, int *cols);

	/* refresh the main terminal window */
	void (*term_win_refresh)();

	/* write a cell in the main terminal window. this may
	 * not accomplish all that much, as this cell will be
	 * most likely rewritten when the terminal updates. a
	 * call to term_win_refresh() should follow a call or
	 * series of calls to this function. if a plugin wants
	 * to present meaningful data to a user it should use
	 * the screen_*_alloc api calls to reserve space for
	 * its output.      */
	void (*term_win_mvaddwch)(int x, int y, const wchar_t *wch, attr_t attr, int color_pair);

	/* the user can configure a global command key
	 * to use as a prefix and plugins can bind to
	 * this key + some extension. for example, the 
	 * user might set ^A as the command key, and a 
	 * plugin might bind to ^A n by passing ch = 0x6e. 
	 * if the return value is non-zero, the key is
	 * reserved or already bound. */
	int (*key_bind)(const aug_plugin_t *plugin, int ch, void (*on_key_fn)(int chr, void *user), void *user );


	/* ======== screen windows/panels ======================== 
	 * for this api there are two types of screen real estate
	 * a plugin can request the aug core to allocate: panels and 
	 * windows. panels correspond to the ncurses panels library
	 * and will stack on top of the main terminal window in the 
	 * order they are allocated (like a popup window for some
	 * sort of transient interaction with the user). windows
	 * correspond to ncurses windows and will not overlap the 
	 * main terminal, but will only be allocated in ways such
	 * that they occupy a number of lines at the top, bottom, left
	 * or right of the screen (this is for peristent existence of 
	 * plugin output on the screen, like a status bar or something).
	 */

	/* as described above, this allocates an ncurses window object
	 * with a height of *nlines* on the top, bottom, or with a width 
	 * of *ncols* on the left or right of the screen. the width/height
	 * of this window will be whatever the screen width/height is,
	 * so the plugin must register for screen_dims_change or call
	 * api->screen_dims to know how much space it actually has in
	 * its window(s). call the function with suffix top, bot, left
	 * and right for a window on the top, bottom, left and right respectively. 
	 * the *win* parameter is the output parameter that will point to
	 * the ncurses window structure.            */
	void (*screen_win_alloc_top)(int nlines, WINDOW **win); 
	void (*screen_win_alloc_bot)(int nlines, WINDOW **win); 
	void (*screen_win_alloc_left)(int ncols, WINDOW **win); 
	void (*screen_win_alloc_right)(int ncols, WINDOW **win); 

	/* allocate a panel on top of the main terminal window and
	 * on top of all previously allocated (by this plugin or others)
	 * panels.           */
	void (*screen_panel_alloc)(int nlines, int ncols, int begin_y, int begin_x, PANEL **panel);
	
	/* for concurrency reasons, call this function instead of
	 * calling update_panels() from the panels library. */
	void (*screen_panels_update)();
};

#endif /* AUG_AUG_H */