/* 
 * Copyright 2012 anthony cantor
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
#include "term_win.h"
#include <errno.h>
#include <stdint.h>
#include <assert.h>

#include "ncurses.h"

#include "util.h"
#include "err.h"
#include "attr.h"
#include "ncurses_util.h"

extern int aug_cell_update(
	int rows, int cols, int *row, int *col, 
	wchar_t *wch, attr_t *attr, int *color_pair
);
extern int aug_cursor_move(
	int rows, int cols, int old_row, 
	int old_col, int *new_row, int *new_col
);

static void resize_terminal(struct aug_term_win *);

void term_win_init(struct aug_term_win *tw, WINDOW *win) {
	tw->term = NULL;
	tw->win = win;
}

void term_win_set_term(struct aug_term_win *tw, struct aug_term *term) {
	tw->term = term;
	resize_terminal(tw);
}

void term_win_dims(const struct aug_term_win *tw, int *rows, int *cols) {
	
	if(tw->win == NULL) {
		*rows = 0;
		*cols = 0;
	}
	else {
		win_dims(tw->win, rows, cols);
	}
}

void term_win_update_cell(struct aug_term_win *tw, VTermPos pos, int color_on) {
	VTermScreen *vts;
	VTermScreenCell cell;
	attr_t attr;
	int pair;
	cchar_t cch;
	wchar_t *wch;
	wchar_t erasech = L' ';
	int maxx, maxy;

	if(tw->term == NULL || tw->win == NULL)
		return;

	vts = vterm_obtain_screen(tw->term->vt);
	getmaxyx(tw->win, maxy, maxx);

	/* sometimes this happens when
	 * a window resize recently happened
	 */
	if(!win_contained(tw->win, pos.row, pos.col) ) {
		fprintf(stderr, "tried to update out of bounds cell at %d/%d %d/%d\n", pos.row, maxy-1, pos.col, maxx-1);
		return;
	}

	if( !vterm_screen_get_cell(vts, pos, &cell) )
		err_exit(0, "get_cell returned false status\n");

	/* convert vterm attributes into ncurses attributes (not colors) */
	attr_vterm_attr_to_curses_attr(&cell, &attr);
	if(color_on) /* convert vterm colors into ncurses colors */
		attr_vterm_pair_to_curses_pair(cell.fg, cell.bg, &attr, &pair);
	else
		pair = 0;

	
	wch = (cell.chars[0] == 0)? &erasech : (wchar_t *) &cell.chars[0];

	if(aug_cell_update(maxy, maxx, &pos.row, &pos.col, wch, &attr, &pair) != 0) /* run API callbacks */
		return;
	if(setcchar(&cch, wch, attr, pair, NULL) == ERR)
		err_exit(0, "setcchar failed");
	if(wmove(tw->win, pos.row, pos.col) == ERR)
		err_exit(0, "move failed: %d/%d, %d/%d\n", pos.row, maxy-1, pos.col, maxx-1);

	/* sometimes writing to the last cell fails... but it doesnt matter? */
	if(wadd_wch(tw->win, &cch) == ERR && (pos.row) != (maxy-1) && (pos.col) != (maxx-1) )
		err_exit(0, "add_wch failed at %d/%d, %d/%d: ", pos.row, maxy-1, pos.col, maxx-1);

}

void term_win_refresh(struct aug_term_win *tw) {
	if(tw->win == NULL)
		return;

	wsyncup(tw->win);
	wcursyncup(tw->win);
	if(wnoutrefresh(tw->win) == ERR)
		err_exit(0, "wnoutrefresh failed!");

}

int term_win_damage(struct aug_term_win *tw, VTermRect rect, int color_on) {
	VTermPos pos;

	if(tw->win == NULL)
		goto done;

	for(pos.row = rect.start_row; pos.row < rect.end_row; pos.row++) {
		for(pos.col = rect.start_col; pos.col < rect.end_col; pos.col++) {
			term_win_update_cell(tw, pos, color_on);
		}
	}

	vterm_state_get_cursorpos(vterm_obtain_state(tw->term->vt), &pos);
	/* restore cursor (repainting shouldnt modify cursor) */
	if(wmove(tw->win, pos.row, pos.col) == ERR) 
		err_exit(0, "move failed: %d, %d", pos.row, pos.col);

done:
	return 1;
}

int term_win_movecursor(struct aug_term_win *tw, VTermPos pos, VTermPos oldpos) {
	int maxy, maxx;

	if(tw->win == NULL)
		goto done;

	/* sometimes this happens when
	 * a window resize recently happened. */
	 if(!win_contained(tw->win, pos.row, pos.col) ) {
		fprintf(stderr, "tried to move cursor out of bounds to %d, %d\n", pos.row, pos.col);
		goto done;
	}

	getmaxyx(tw->win, maxy, maxx);
	if(aug_cursor_move(maxy, maxx, oldpos.row, oldpos.col, &pos.row, &pos.col) != 0) /* run API callbacks */
		goto done;

	if(wmove(tw->win, pos.row, pos.col) == ERR)
		err_exit(0, "move failed: %d, %d", pos.row, pos.col);

done:
	return 1;
}

/* syncronizes the aug_term_win structure to the
 * right size according to win 
 */
void term_win_resize(struct aug_term_win *tw, WINDOW *win) {
	tw->win = win;
	resize_terminal(tw);
}

/* synchronizes the aug_term structure to the right
 * size according to tw->win
 */
static void resize_terminal(struct aug_term_win *tw) {
	int rows, cols;

	if(tw->win == NULL)
		return;

	/* get the size of the window */
	win_dims(tw->win, &rows, &cols);

	fprintf(stderr, "term_win: resize to %d, %d\n", rows, cols);
	if(tw->term != NULL)
		if(term_resize(tw->term, rows, cols) != 0)
			err_exit(errno, "error resizing terminal!");
}
