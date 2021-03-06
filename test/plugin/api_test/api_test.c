/* 
 * Copyright 2013 anthony cantor
 * This file is part of aug.
 *
 * aug is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * aug is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with aug.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "aug_plugin.h"
#include "api_test_vars.h"
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <ccan/tap/tap.h>
#include <ccan/array_size/array_size.h>

const char aug_plugin_name[] = "api_test";

void input_char(uint32_t *ch, aug_action *action, struct aug_inject *inject, void *user);
void cell_update(int rows, int cols, int *row, int *col, wchar_t *wch, 
					attr_t *attr, int *color_pair, aug_action *action, void *user);
void cursor_move(int rows, int cols, int old_row, int old_col, 
					int *new_row, int *new_col, aug_action *action, void *user);
void screen_dims_change(int rows, int cols, void *user);

static const struct aug_api *g_api;
static struct aug_plugin *g_plugin;

#define API_TEST_LOCK(mtx_ptr) \
	do { \
		if(pthread_mutex_lock(mtx_ptr) != 0) \
			(*g_api->log)(g_plugin, "failed to lock mutex at line %d\n", __LINE__); \
	} while(0)
#define API_TEST_UNLOCK(mtx_ptr) \
	do { \
		if(pthread_mutex_unlock(mtx_ptr) != 0) \
			(*g_api->log)(g_plugin, "failed to unlock mutex at line %d\n", __LINE__); \
	} while(0)

static char *g_user_data = "on_r secret stuff...";
static struct aug_plugin_cb g_callbacks;
static uint32_t g_callback_key;
static bool g_got_callback = false;
static bool g_got_expected_input = false;
static bool g_got_cell_update = false;
static bool g_got_pre_scroll = false;
static bool g_got_post_scroll = false;
static bool g_got_cursor_move = false;
static bool g_on_r_interaction = false;
pthread_mutex_t g_on_r_interaction_mtx = PTHREAD_MUTEX_INITIALIZER;
static PANEL *g_pan1, *g_pan2, *g_pan3;
static struct aug_terminal_win g_pan_twin, g_top_twin;
static WINDOW *g_pan2_dwin, *g_pan3_dwin;
static pthread_t g_thread1, g_thread2, g_thread3, g_thread4;

static void *g_pan_term;
static int g_pan_term_freed;
static char g_vi_test_file[] = "/tmp/api_test_file";
static char *g_pan_term_argv[] = {"vi", g_vi_test_file, NULL};
static const char g_vi_msg[] = "zoidberg is a crafty consumer! (\\/)(,;;,)(\\/)";

static void *g_top_term;
static char *g_top_term_argv[] = {"./build/toysh", NULL};

/* if called from the main thread (a callback from aug
 * or the init and free functions), this will deadlock
 * if aug called the plugin while it had a lock on the
 * screen. if the lock is held by another plugin, then
 * this should finish eventually.
 */
static void check_screen_lock() {
	int size;

	diag("make sure screen is unlocked");
	(*g_api->screen_panel_size)(g_plugin, &size);
	pass("screen is unlocked");
}

static int test_sigs(const char *caller) {
	sigset_t sigset;
	int winch_is_blocked;
	int chld_is_blocked;

	sigemptyset(&sigset);
	diag("pthread_sigmask state test");
	if(pthread_sigmask(SIG_SETMASK, NULL, &sigset) != 0) {
		fail("expected to be able to get current sigset.");
		return -1;
	}

	if( (winch_is_blocked = sigismember(&sigset, SIGWINCH) ) == -1 ) {
		fail("expected to be able test current sigset.");
		return -1;
	}
	
	ok( (winch_is_blocked != 0), "(%s) confirm that SIGWINCH is blocked", caller);

	if( (chld_is_blocked = sigismember(&sigset, SIGCHLD) ) == -1 ) {
		fail("expected to be able test current sigset.");
		return -1;
	}
	
	ok( (chld_is_blocked != 0), "(%s) confirm that SIGCHLD is blocked", caller);

	return 0;
}

void input_char(uint32_t *ch, aug_action *action, struct aug_inject *inject, void *user) {
	static unsigned int total_chars = 0;
#	define CUTOFF (ARRAY_SIZE(api_test_user_input) - 1 )
	static char firstn[CUTOFF+1];
#	define CUTOFF_INTERN ( ARRAY_SIZE(api_test_on_r_response) - 1 - 1)
	static char intern[CUTOFF_INTERN];
	static unsigned int total_inter_chars = 0;
#	define HLEN 8
	static uint32_t history[HLEN];
	static size_t h_sz = 0;
	static size_t h_idx = 0;
	(void)(action);

	/*diag("========> %d/%d: '%c' (0x%02x)", total_chars+1, CUTOFF, (*ch > 0x20 && *ch <= 0x7e)? *ch : ' ', *ch);*/
	if(total_chars < CUTOFF ) {
		firstn[total_chars] = *ch;
	}
	
	total_chars++;

	if(total_chars == CUTOFF ) {
		diag("++++input_char++++");
		firstn[total_chars] = '\0';	
		/*diag("user input = '%s'", firstn);*/
		if(strcmp(firstn, api_test_user_input) == 0)
			g_got_expected_input = true;

		ok(user == g_user_data, "(input char) check that user ptr is correct");
		test_sigs("input_char");
		diag("----input_char----\n#");
	}

	API_TEST_LOCK(&g_on_r_interaction_mtx);
	if(g_on_r_interaction == true) {

		/*diag("========> '%c' (0x%02x)", (*ch > 0x20 && *ch <= 0x7e)? *ch : ' ', *ch);*/
		if(*ch == '\n') {
			if(strncmp(intern, api_test_on_r_response, CUTOFF_INTERN) != 0) 
				fail("check the on_r interactive input data: %s", intern);
			else
				pass("check the on_r interactive input data");

			g_on_r_interaction = false;

			ok( hide_panel(g_pan2) != ERR, "hide on_r panel");
			(*g_api->screen_panel_update)(g_plugin);
		}
		else {
			waddch(g_pan2_dwin, *ch);
			wsyncup(g_pan2_dwin);
			wcursyncup(g_pan2_dwin);

			(*g_api->screen_panel_update)(g_plugin);
			(*g_api->screen_doupdate)(g_plugin);

			if(total_inter_chars < CUTOFF_INTERN)
				intern[total_inter_chars++] = *ch;				
		}

		*action = AUG_ACT_CANCEL;
	}
	API_TEST_UNLOCK(&g_on_r_interaction_mtx);

	if(*action != AUG_ACT_CANCEL) {
		const uint32_t *to_match = (uint32_t *) L"scroll";

		history[h_idx] = *ch;
		h_idx = (h_idx+1) % HLEN;
		if(h_sz < HLEN)
			h_sz++;

		int i;
		for(i = 0; i < 6; i++) {
			if(history[(h_idx + HLEN - 1 - i) % HLEN] != to_match[5-i])
				return;
		}

		*action = AUG_ACT_CANCEL;
		inject->len = 6;
		inject->chars = (uint32_t *) L"ltroll";
	}
#undef CUTOFF	
}

void cell_update(int rows, int cols, int *row, int *col, wchar_t *wch, attr_t *attr, 
					int *color_pair, aug_action *action, void *user) {
	(void)(rows);
	(void)(cols);
	(void)(row);
	(void)(col);
	(void)(wch);
	(void)(attr);
	(void)(color_pair);
	(void)(action);
	static bool checked_winch_and_screen_lock = false;

	/*diag("cell_update: %d,%d", *row, *col);*/
	g_got_cell_update = true;

	if(checked_winch_and_screen_lock == false) {
		diag("++++cell_update++++");
		ok(user == g_user_data, "(cell_update) check that user ptr is correct");
		test_sigs("cell_update");
		checked_winch_and_screen_lock = true;
		diag("----cell_update----\n#");
	}
}

void pre_scroll(int rows, int cols, int direction, aug_action *action, void *user) {
	(void)(rows);
	(void)(cols);
	(void)(direction);
	(void)(action);
	static bool checked_winch_and_screen_lock = false;

	g_got_pre_scroll = true;

	if(checked_winch_and_screen_lock == false) {
		diag("++++pre_scroll++++");
		ok(user == g_user_data, "(pre_scroll) check that user ptr is correct");
		test_sigs("pre_scroll"); /* +2 tests */
		checked_winch_and_screen_lock = true;
		diag("----pre_scroll----\n#");
	}
}

void post_scroll(int rows, int cols, int direction, aug_action *action, void *user) {
	(void)(rows);
	(void)(cols);
	(void)(direction);
	(void)(action);
	static bool checked_winch_and_screen_lock = false;

	g_got_post_scroll = true;

	if(checked_winch_and_screen_lock == false) {
		diag("++++post_scroll++++");
		ok(user == g_user_data, "(post_scroll) check that user ptr is correct");
		test_sigs("post_scroll"); /* +2 tests */
		checked_winch_and_screen_lock = true;
		
		/* nothing to test with this, but if we do it maybe *grind will 
		 * catch an issue. */
		(*g_api->primary_term_damage)(g_plugin, 0, 10, 0, 4);
		diag("----post_scroll----\n#");
	}
}

void cursor_move(int rows, int cols, int old_row, int old_col, int *new_row, int *new_col, 
					aug_action *action, void *user) {
	(void)(rows);
	(void)(cols);
	(void)(old_row);
	(void)(old_col);
	(void)(new_row);
	(void)(new_col);
	(void)(action);
	static bool checked_winch_and_screen_lock = false;

	/*diag("cursor_move: %d,%d", *new_row, *new_col);*/
	g_got_cursor_move = true;

	if(checked_winch_and_screen_lock == false) {
		diag("++++cursor_move++++");
		ok(user == g_user_data, "(cursor_move) check that user ptr is correct");
		test_sigs("cursor_move");
		checked_winch_and_screen_lock = true;
		diag("----cursor_move----\n#");
	}
}

void screen_dims_change(int rows, int cols, void *user) {
	static bool checked_winch_and_screen_lock = false;

	if(checked_winch_and_screen_lock == false) {
		diag("++++screen_dims_change++++");
		diag("change to %d,%d", rows, cols);
		ok(user == g_user_data, "(screen_dims_change) check that user ptr is correct");
		test_sigs("screen_dims_change");
		checked_winch_and_screen_lock = true;
		diag("----screen_dims_change----\n#");
	}
}

void primary_term_dims_change(int rows, int cols, void *user) {
	static bool checked_winch_and_screen_lock = false;

	if(checked_winch_and_screen_lock == false) {
		diag("++++primary_term_dims_change++++");
		diag("change to %d,%d", rows, cols);
		ok(user == g_user_data, "(primary_term_dims_change) check that user ptr is correct");
		test_sigs("primary_term_dims_change");
		checked_winch_and_screen_lock = true;
		diag("----primary_term_dims_change----\n#");
	}
}

static int box_and_print(WINDOW *win, const char *str) {
	if(box(win, 0, 0) == ERR) {
		diag("expected to be able modify the window. abort...");
		return -1;
	}
		
	if(mvwprintw(win, 1, 1, str) == ERR) {
		diag("expected to be able to print to the window. abort...");
		return -1;
	}

	return 0;
}

/* screen is locked already */
void status_bar_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x;
	(void)(user);

	if(ran_once == 0) {
		pass("got callback for status bar window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(rows == 3);
		ok1(cols == COLS);
		getparyx(win, y, x);
		ok1(y == 0);
		ok1(x == 0);
		ran_once = 1;
	}

	if(win != NULL) {
		if(box_and_print(win, "status bar!") != 0)
			diag("warning: box_and_print on status window failed.");

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh status window");
	}
}

void bottom_bar1_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for bottom bar1 window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(rows == 4);
		ok1(cols == COLS);
		getparyx(win, y, x);
		ok1(y == LINES-5);
		ok1(x == 0);
		ran_once = 1;
	}

	if(win != NULL) {
		if(box_and_print(win, "bottom bar 1!") != 0)
			diag("warning: box_and_print on status window failed.");

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh status window");
	}
}

void bottom_bar0_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for bottom bar0 window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(rows == 1);
		ok1(cols == COLS);
		getparyx(win, y, x);
		ok1(y == LINES-1);
		ok1(x == 0);
		ran_once = 1;
	}

	if(win != NULL) {
		
		if(mvwprintw(win, 0, 1, "bottom bar 0") == ERR)
			diag("warning: print on window failed.");

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh window");
	}
}

void left_bar0_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x, i, j;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for left bar0 window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(cols == 1);
		ok1(rows == LINES-8);
		getparyx(win, y, x);
		ok1(y == 3);
		ok1(x == 0);
		ran_once = 1;
	}

	if(win != NULL) {
		getmaxyx(win, rows, cols);		
		diag("left window: %dx%d", rows, cols);
		for(i = 0; i < rows; i++)
			for(j = 0; j < cols; j++)
				mvwaddch(win, i, j, '|');
				/*if(mvwaddch(win, i, j, '|') == ERR)
					diag("warning: print on window failed at %d/%d, %d/%d", i, rows, j, cols);*/

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh window");
	}
}

void left_bar1_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x, i, j;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for left bar1 window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(cols == 10);
		ok1(rows == LINES-8);
		getparyx(win, y, x);
		ok1(y == 3);
		ok1(x == 1);
		ran_once = 1;
	}

	if(win != NULL) {
		getmaxyx(win, rows, cols);		
		for(i = 0; i < rows; i++)
			for(j = 0; j < cols; j++)
				mvwaddch(win, i, j, '*');
				/*if(mvwaddch(win, i, j, '*') == ERR)
					diag("warning: print on window failed at %d/%d, %d/%d", i, rows, j, cols);*/

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh window");
	}
}

void left_bar2_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x, i, j;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for left bar2 window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(cols == 3);
		ok1(rows == LINES-8);
		getparyx(win, y, x);
		ok1(y == 3);
		ok1(x == 11);
		ran_once = 1;
	}

	if(win != NULL) {
		getmaxyx(win, rows, cols);		
		for(i = 0; i < rows; i++)
			for(j = 0; j < cols; j++)
				mvwaddch(win, i, j, '+');
				/*if(mvwaddch(win, i, j, '+') == ERR)
					diag("warning: print on window failed at %d/%d, %d/%d", i, rows, j, cols);*/

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh window");
	}
}

void right_bar0_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x, i, j;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for right bar0 window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(cols == 5);
		ok1(rows == LINES-8);
		getparyx(win, y, x);
		ok1(y == 3);
		ok1(x == COLS-5);
		ran_once = 1;
	}

	if(win != NULL) {
		getmaxyx(win, rows, cols);		
		for(i = 0; i < rows; i++)
			for(j = 0; j < cols; j++)
				mvwaddch(win, i, j, '!');
				/*if(mvwaddch(win, i, j, '!') == ERR)
					diag("warning: print on window failed at %d/%d, %d/%d", i, rows, j, cols);*/

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh window");
	}
}

void right_bar1_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x, i, j;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for right bar1 window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(cols == 2);
		ok1(rows == LINES-8);
		getparyx(win, y, x);
		ok1(y == 3);
		ok1(x == COLS-7);
		ran_once = 1;
	}

	if(win != NULL) {
		getmaxyx(win, rows, cols);		
		for(i = 0; i < rows; i++)
			for(j = 0; j < cols; j++)
				mvwaddch(win, i, j, '?');
				/*if(mvwaddch(win, i, j, '?') == ERR)
					diag("warning: print on window failed at %d/%d, %d/%d", i, rows, j, cols);*/

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh window");
	}
}

void right_bar2_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x, i, j;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for right bar2 window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(cols == 2);
		ok1(rows == LINES-8);
		getparyx(win, y, x);
		ok1(y == 3);
		ok1(x == COLS-9);
		ran_once = 1;
	}

	if(win != NULL) {
		getmaxyx(win, rows, cols);		
		for(i = 0; i < rows; i++)
			for(j = 0; j < cols; j++)
				mvwaddch(win, i, j, '@');
				/*if(mvwaddch(win, i, j, '@') == ERR)
					diag("warning: print on window failed at %d/%d, %d/%d", i, rows, j, cols);*/

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh window");
	}
}

void right_bar3_cb(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x, i, j;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for right bar3 window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(cols == 1);
		ok1(rows == LINES-8);
		getparyx(win, y, x);
		ok1(y == 3);
		ok1(x == COLS-10);
		ran_once = 1;
	}

	if(win != NULL) {
		getmaxyx(win, rows, cols);		
		for(i = 0; i < rows; i++)
			for(j = 0; j < cols; j++)
				mvwaddch(win, i, j, '|');
				/*if(mvwaddch(win, i, j, '|') == ERR)
					diag("warning: print on window failed at %d/%d, %d/%d", i, rows, j, cols);*/

		wsyncup(win);
		wcursyncup(win);
		if(wnoutrefresh(win) == ERR)
			diag("warning: expected to be able to refresh window");
	}
}

static void *thread1(void *user) {
	int amt;
	FILE *fp;
	const char closevi[] = "\x03:wq\n";
	char buf[64];
	const char sh_cmd1[] = "echo 'i am an edge window terminal created by a plugin ^_^'\r";
	const char sh_cmd2[] = "echo 'some test text. blah blah blah qwer' > /tmp/api_test_sh_test\r";
	const char sh_cmd3[] = "exit\r";
	(void)(user);

	diag("++++thread1++++");
	check_screen_lock();
	test_sigs("thread1");

	sleep(1);
	diag("write into top terminal");
	amt = 0;
	while(amt < (int) (sizeof(sh_cmd1)-1) ) {
		amt += (*g_api->terminal_input_chars)(g_plugin, g_top_term, sh_cmd1+amt, 1 );
		usleep(5000);
	}

	amt = 0;
	while(amt < (int) (sizeof(sh_cmd2)-1) ) {
		amt += (*g_api->terminal_input_chars)(g_plugin, g_top_term, sh_cmd2+amt, 1 );
		usleep(5000);
	}

	amt = 0;
	while(amt < (int) (sizeof(sh_cmd3)-1) ) {
		amt += (*g_api->terminal_input_chars)(g_plugin, g_top_term, sh_cmd3+amt, 1 );
		usleep(5000);
	}

	diag("write into panel terminal");
	while((*g_api->terminal_input_chars)(g_plugin, g_pan_term, "i", 1 ) != 1) 
		usleep(10000);
		
	amt = 0;
	while(amt < (int) (sizeof(g_vi_msg)-1) ) {
		amt += (*g_api->terminal_input_chars)(g_plugin, g_pan_term, g_vi_msg+amt, 1 );
		usleep(50000);
	}
	pass("wrote message into vi");
	diag("now cause terminal to refresh (not necessary, just for code coverage).");
	(*g_api->terminal_refresh)(g_plugin, g_pan_term);

	sleep(1);
	diag("close vi");

	amt = 0;
	while(amt < (int) (sizeof(closevi)-1) ) {
		amt += (*g_api->terminal_input_chars)(g_plugin, g_pan_term, closevi+amt, 1 );
		usleep(50000);
	}
	if( (fp = fopen(g_vi_test_file, "r") ) == NULL) {
		fail("vi test file does not exist");
	}
	else {
		if( fread(buf, sizeof(char), 63, fp) < (sizeof(g_vi_msg) - 1) )
			fail("vi test file does not have the correct amount of bytes");
		else {
			if(strncmp(buf, g_vi_msg, sizeof(g_vi_msg)-1) != 0) {
				fail("vi test file contents do not match g_vi_msg");
				buf[sizeof(g_vi_msg)-1] = '\0';		
				diag("test file: %s", buf);
				diag("test msg : %s", g_vi_msg);
			}
			else
				pass("vi test file contents match g_vi_msg");
		}
		
		unlink(g_vi_test_file);
	}

	sleep(1);
	diag("move panel a bit");
	
	(*g_api->lock_screen)(g_plugin);
	ok1(move_panel(g_pan1, 4, 30) != ERR);
	(*g_api->screen_panel_update)(g_plugin);
	(*g_api->screen_doupdate)(g_plugin);
	(*g_api->unlock_screen)(g_plugin);

	sleep(1);

	diag("also remove some edge windows");
	ok1( (*g_api->screen_win_dealloc)(g_plugin, bottom_bar0_cb) == 0);
	ok1( (*g_api->screen_win_dealloc)(g_plugin, left_bar1_cb) == 0);
	ok1( (*g_api->screen_win_dealloc)(g_plugin, right_bar1_cb) == 0);

	usleep(500000);
	diag("also write some stuff into the primary terminal");
	const char primary_echo[] = "echo 'hooray im a plugin!'\recho 'utf-8 doesnt work with primary_input_chars: \xE2\x97\xB0'\r";
	size_t echoed = 0;

	while(echoed < ARRAY_SIZE(primary_echo)) {
		echoed += (*g_api->primary_input_chars)(g_plugin, primary_echo+echoed, 1);
		usleep(20000);
	}

	diag("now write some utf-32 into the primary terminal");
	const wchar_t primary_echo32[] = L"echo 'hooray for unicode: ∃∑∞≘≥⊠⊙⋙ ⋃⋏⨌  ⪤'\r";
	echoed = 0;
	usleep(100000);
	while(echoed < ARRAY_SIZE(primary_echo32)) {
		echoed += (*g_api->primary_input)(g_plugin, (uint32_t *) primary_echo32+echoed, 1);
		usleep(20000);
	}
	diag("now cause the primary terminal to refresh (not necessary, just for code coverage).");
	(*g_api->primary_refresh)(g_plugin);
	
	diag("sleep for a while and then hide bottom panel");
	sleep(3);

	(*g_api->lock_screen)(g_plugin);
	ok1(hide_panel(g_pan1) != ERR);
	(*g_api->screen_panel_update)(g_plugin);
	(*g_api->screen_doupdate)(g_plugin);
	(*g_api->unlock_screen)(g_plugin);

	diag("----thread1----\n#");
	return NULL;
}

static void *thread2(void *user) {
	(void)(user);
	int stack_size, brk;
	int rows, cols;
	WINDOW *pan2_win;

	diag("++++thread2++++");
	check_screen_lock();
	test_sigs("thread2");

	diag("allocate a panel");
	rows = 10;
	cols = 30;
	(*g_api->screen_panel_alloc)(g_plugin, rows, cols, 10, 15, &g_pan2);
	pass("panel allocated");

	diag("there should be only 2 panels");

	todo_start("expected to fail. see below.");
	(*g_api->screen_panel_size)(g_plugin, &stack_size);
	ok1(stack_size == 3);
	todo_end();

	diag("write a message into the panel");

	(*g_api->lock_screen)(g_plugin);
	if( (pan2_win = panel_window(g_pan2) ) == NULL) {
		diag("expected to be able to access panel window. abort...");
		goto unlock;
	}
	
	g_pan2_dwin = derwin(pan2_win, rows - 3, cols - 2, 2, 1);

	if(box_and_print(pan2_win, "the ^R panel") != 0) {
		diag("box_and_print failed. abort...");
		goto unlock;
	}
	
	mvwprintw(g_pan2_dwin, 0, 0, "enter a string: ");
	(*g_api->screen_panel_update)(g_plugin);
	(*g_api->screen_doupdate)(g_plugin);
	(*g_api->unlock_screen)(g_plugin);

	API_TEST_LOCK(&g_on_r_interaction_mtx);
	g_on_r_interaction = true;
	API_TEST_UNLOCK(&g_on_r_interaction_mtx);

	brk = 0;
	while(1) {
		usleep(10000);
		API_TEST_LOCK(&g_on_r_interaction_mtx);
		if(g_on_r_interaction != true)
			brk = 1;
		API_TEST_UNLOCK(&g_on_r_interaction_mtx);

		if(brk != 0)
			break;
	}

	diag("----thread2----\n#");
	return NULL;
unlock:
	(*g_api->unlock_screen)(g_plugin);
	return NULL;
}

static void *thread3(void *user) {
	(void)(user);

	diag("++++thread3++++\n#");
	test_sigs("thread3");
	diag("run panel terminal io loop");
	(*g_api->terminal_run)(g_plugin, g_pan_term);
	pass("finished panel terminal io loop");

	diag("free terminal");
	g_pan_term_freed = 1;
	(*g_api->terminal_delete)(g_plugin, g_pan_term);
	
	diag("cleanup window and panel");
	(*g_api->lock_screen)(g_plugin);
	ok(delwin(g_pan3_dwin) != ERR, "delete derived window (panel 3)");
	(*g_api->unlock_screen)(g_plugin);

	(*g_api->screen_panel_dealloc)(g_plugin, g_pan3);

	diag("----thread3----\n#");
	return NULL;
}

static void *thread4(void *user) {
	(void)(user);

	diag("++++thread4++++\n#");
	test_sigs("thread4");

	diag("run top terminal io loop");
	(*g_api->terminal_run)(g_plugin, g_top_term);
	pass("finished top terminal io loop");

	diag("free terminal");
	(*g_api->terminal_delete)(g_plugin, g_top_term);

	diag("----thread4----\n#");
	return NULL;
}

void top_terminal_cb_free(WINDOW *win, void *user) {
	static int ran_once = 0;
	(void)(win);
	(void)(user);

	if(ran_once == 0) {
		pass("free callback for top terminal window");
		ran_once = 1;
	}

	delwin(g_top_twin.win);
	g_top_twin.win = NULL;
}

void top_terminal_cb_new(WINDOW *win, void *user) {
	static int ran_once = 0;
	int rows, cols, y, x;
	WINDOW *dwin;

	(void)(user);

	if(ran_once == 0) {
		pass("got callback for top terminal window");
		ok1(win != NULL);
		getmaxyx(win, rows, cols);
		ok1(rows == 8);
		ok1(cols == COLS);
		getparyx(win, y, x);
		ok1(y == 3);
		ok1(x == 0);
		ran_once = 1;
	}

	if(box(win, 0, 0) == ERR) {
		diag("warning, box failed...");
	}

	getmaxyx(win, rows, cols);
	if( (dwin = derwin(win, rows-2, cols-2, 1, 1) ) == NULL) {
		diag("warning, expected to be able to derive from terminal window");
		g_top_twin.win = win;
		return;
	}

	g_top_twin.win = dwin;
}

static void on_r(uint32_t chr, void *user) {
	diag("++++key callback++++");
	test_sigs("on_r");
	ok( (chr == g_callback_key), "callback on key 0x%02x (got 0x%02x)", g_callback_key, chr);
	ok( (user == g_user_data), "user ptr is correct");
		
	g_got_callback = true;
	
	if(g_on_r_interaction != true) {
		diag("spawn thread for user interaction");
		if(pthread_create(&g_thread2, NULL, thread2, NULL) != 0) {
			diag("failed to create user interaction thread...");
			return;
		}
	}

	diag("----key callback----\n#");
}


int aug_plugin_init(struct aug_plugin *plugin, const struct aug_api *api) {
	const char *testkey;
	int stack_size = -1;
	WINDOW *pan1_win, *pan3_win;
	int rows, cols, drows, dcols;

	diag("++++plugin_init++++");
	g_plugin = plugin;	
	g_api = api;

	(*g_api->log)(g_plugin, "init %s\n", aug_plugin_name);

	check_screen_lock();
	test_sigs("plugin_init");

	aug_callbacks_init(&g_callbacks);
	g_callbacks.user = g_user_data;
	g_callbacks.input_char = input_char;
	g_callbacks.cell_update = cell_update;
	g_callbacks.pre_scroll = pre_scroll;
	g_callbacks.post_scroll = post_scroll;
	g_callbacks.cursor_move = cursor_move;
	g_callbacks.screen_dims_change = screen_dims_change;
	g_callbacks.primary_term_dims_change = primary_term_dims_change;

	api->callbacks(g_plugin, &g_callbacks, NULL);

	diag("test for the value of testkey in the ini file");	
	if( (*g_api->conf_val)(g_plugin, aug_plugin_name, "testkey", &testkey) == 0) {
		if(strcmp(testkey, "testval") != 0) {
			fail("testkey != testval");
		}			
		else {
			pass("testkey = testval");
		}
	}
	else {
		fail("testkey not found.");
	}	

	if( (*g_api->keyname_to_key)(g_plugin, "^R", &g_callback_key) != 0) {
		diag("expected to be able to find key from keyname ^R. abort...");
		return -1;
	}

	/* register a callback for when the user types command key + 'r' */
	if( (*g_api->key_bind)(g_plugin, g_callback_key, on_r, g_user_data) != 0) {
		diag("expected to be able to bind to extension '^R'. abort...");
		return -1;
	}

	diag("allocate a panel");
	(*g_api->screen_panel_alloc)(g_plugin, 10, 30, 5, 10, &g_pan1);
	pass("panel allocated");

	diag("there should be only 1 panel");
	(*g_api->screen_panel_size)(g_plugin, &stack_size);
	ok1(stack_size == 1);

	diag("write a message into the panel");

	(*g_api->lock_screen)(g_plugin);
	if( (pan1_win = panel_window(g_pan1) ) == NULL) {
		diag("expected to be able to access panel window. abort...");
		goto unlock;
	}
	
	if(box_and_print(pan1_win, "the bottom panel") != 0) {
		diag("box_and_print failed. abort...");
		goto unlock;
	}
	(*g_api->screen_panel_update)(g_plugin);
	(*g_api->screen_doupdate)(g_plugin);
	(*g_api->unlock_screen)(g_plugin);

	diag("create status bar window");
	(*g_api->screen_win_alloc_top)(g_plugin, 3, status_bar_cb, NULL);
	(*g_api->screen_win_alloc_bot)(g_plugin, 1, bottom_bar0_cb, NULL);
	(*g_api->screen_win_alloc_bot)(g_plugin, 4, bottom_bar1_cb, NULL);
	(*g_api->screen_win_alloc_left)(g_plugin, 1, left_bar0_cb, NULL);
	(*g_api->screen_win_alloc_left)(g_plugin, 10, left_bar1_cb, NULL);
	(*g_api->screen_win_alloc_left)(g_plugin, 3, left_bar2_cb, NULL);
	(*g_api->screen_win_alloc_right)(g_plugin, 5, right_bar0_cb, NULL);
	(*g_api->screen_win_alloc_right)(g_plugin, 2, right_bar1_cb, NULL);
	(*g_api->screen_win_alloc_right)(g_plugin, 2, right_bar2_cb, NULL);
	(*g_api->screen_win_alloc_right)(g_plugin, 1, right_bar3_cb, NULL);

	diag("create terminal panel");
	(*g_api->screen_panel_alloc)(g_plugin, 10, 30, 15, 43, &g_pan3);
	pass("terminal panel allocated");

	diag("there should be 2 panels");
	(*g_api->screen_panel_size)(g_plugin, &stack_size);
	ok1(stack_size == 2);

	diag("set up terminal panel");

	(*g_api->lock_screen)(g_plugin);
	if( (pan3_win = panel_window(g_pan3) ) == NULL) {
		diag("expected to be able to access panel window. abort...");
		goto unlock;
	}
	if(box(pan3_win, 0, 0) == ERR) {
		diag("box failed. abort...");
		goto unlock;
	}

	getmaxyx(pan3_win, rows, cols);
	if( (g_pan3_dwin = derwin(pan3_win, rows-2, cols-2, 1, 1) ) == NULL) {
		diag("expected to be able to derive from terminal window");
		goto unlock;
	}
	getmaxyx(g_pan3_dwin, drows, dcols);
	ok1(drows == rows-2);
	ok1(dcols == cols-2);

	(*g_api->screen_panel_update)(g_plugin);
	(*g_api->screen_doupdate)(g_plugin);
	(*g_api->unlock_screen)(g_plugin);

	g_pan_term_freed = 0;
	g_pan_twin.win = g_pan3_dwin;
	(*g_api->terminal_new)(
		g_plugin, 
		&g_pan_twin, 
		g_pan_term_argv,
		&g_pan_term
	);
	
	g_top_twin.win = NULL;
	(*g_api->screen_win_alloc_top)(g_plugin, 8, top_terminal_cb_new, top_terminal_cb_free);
	
	while(g_top_twin.win == NULL)
		usleep(10000);

	(*g_api->terminal_new)(
		g_plugin, 
		&g_top_twin, 
		g_top_term_argv,
		&g_top_term
	);

	diag("create thread for asynchronous tests");
	if(pthread_create(&g_thread1, NULL, thread1, NULL) != 0) {
		diag("expected to be able to create a thread. abort...");
		return -1;
	}

	diag("create thread for panel terminal");
	if(pthread_create(&g_thread3, NULL, thread3, NULL) != 0) {
		diag("expected to be able to create a thread. abort...");
		return -1;
	}

	diag("create thread for top terminal");
	if(pthread_create(&g_thread4, NULL, thread4, NULL) != 0) {
		diag("expected to be able to create a thread. abort...");
		return -1;
	}

	diag("----plugin_init----\n#");

	return 0;
unlock:
	(*g_api->unlock_screen)(g_plugin);
	return -1;
}

void aug_plugin_free() {
	int size;
	int pid;

	diag("++++plugin_free++++");
	(*g_api->log)(g_plugin, "free\n");

	check_screen_lock();

	diag("join thread1 first");
	ok1(pthread_join(g_thread1, NULL) == 0);

	if( g_pan_term_freed == 0 
			&& (*g_api->terminal_terminated)(g_plugin, g_pan_term) == 0) {
		fail("terminal is still running, kill child.");
		pid = (*g_api->terminal_pid)(g_plugin, g_pan_term);
		(*g_api->log)(g_plugin, "kill pid %d\n", pid);
		kill(pid , SIGKILL);
	}

	diag("join other threads");
	ok1(pthread_join(g_thread4, NULL) == 0);	
	ok1(pthread_join(g_thread3, NULL) == 0);
	ok1(pthread_join(g_thread2, NULL) == 0);
	
	diag("all threads finished");

	ok( (g_got_callback == true) , "check to see if the key extension callback happened" );

	ok( ( (*g_api->key_unbind)(g_plugin, g_callback_key) == 0), "check to make sure we can unbind key extension");	
	
	ok( (g_got_expected_input == true), "check to see if input callback got expected user input");
	ok( (g_got_cell_update == true), "check to see if cell_update callback got called");
	ok( (g_got_pre_scroll == true), "check to see if pre_scroll callback got called");
	ok( (g_got_post_scroll == true), "check to see if post_scroll callback got called");
	ok( (g_got_cursor_move == true), "check to see if cursor_move callback got called");

	diag("dealloc windows");

	(*g_api->screen_win_dealloc)(g_plugin, status_bar_cb);
	(*g_api->screen_win_dealloc)(g_plugin, bottom_bar1_cb);
	(*g_api->screen_win_dealloc)(g_plugin, left_bar0_cb);
	(*g_api->screen_win_dealloc)(g_plugin, left_bar2_cb);
	(*g_api->screen_win_dealloc)(g_plugin, right_bar0_cb);
	(*g_api->screen_win_dealloc)(g_plugin, right_bar2_cb);
	(*g_api->screen_win_dealloc)(g_plugin, right_bar3_cb);
	(*g_api->screen_win_dealloc)(g_plugin, top_terminal_cb_new);

	diag("dealloc panels");
	(*g_api->lock_screen)(g_plugin);
	ok(delwin(g_pan2_dwin) != ERR, "delete derived window (panel 2)");
	(*g_api->unlock_screen)(g_plugin);

	todo_start("screen_panel_size fails because "
				"hidden panels are not traversable via "
				"panel_below/above functions, which "
				"messes up panel_stack_size()");

	(*g_api->screen_panel_size)(g_plugin, &size);
	ok1( size == 2 );
	(*g_api->screen_panel_dealloc)(g_plugin, g_pan2);
	(*g_api->screen_panel_size)(g_plugin, &size);
	ok1( size == 1 );
	(*g_api->screen_panel_dealloc)(g_plugin, g_pan1);
	(*g_api->screen_panel_size)(g_plugin, &size);
	todo_end();
	ok1( size == 0 );
	
	diag("----plugin_free----\n#");
}