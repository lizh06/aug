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

#include "region_map.h"

#include <stdlib.h>
#include <ccan/list/list.h>

#include "err.h"
#include "util.h"

struct edgewin {
	int size;
	const void *key;
	struct list_node node;
};

static struct {
	struct list_head top_edgewins;
	struct list_head bot_edgewins;
} g_map;

static void delete(struct edgewin *);
static int init_region(int, int, int, int, int, struct aug_region *);

void region_map_init() {
	list_head_init(&g_map.top_edgewins);
	list_head_init(&g_map.bot_edgewins);
}

void region_map_free() {
	struct edgewin *next, *i;

	list_for_each_safe(&g_map.top_edgewins, i, next, node) {
		delete(i);
	}
	list_for_each_safe(&g_map.bot_edgewins, i, next, node) {
		delete(i);
	}
}

static void edgewin_list_push(struct list_head *head, const void *key, int size) {
	struct edgewin *item;

	if(size < 1)
		err_exit(0, "size is less than 1");

	item = aug_malloc( sizeof(struct edgewin) );
	item->key = key;
	item->size = size;
	list_add_tail(head, &item->node);
}

void region_map_push_top(const void *key, int nlines) { 
	edgewin_list_push(&g_map.top_edgewins, key, nlines);
}
void region_map_push_bot(const void *key, int nlines) { 
	edgewin_list_push(&g_map.bot_edgewins, key, nlines);
}

static void delete(struct edgewin *item) {
	list_del(&item->node);
	free(item);
}

static int edgewin_list_size(const struct list_head *head) {
	struct edgewin *i;
	int n;

	n = 0;
	list_for_each(head, i, node) 
		n++;

	return n;
}

int region_map_top_size() { return edgewin_list_size(&g_map.top_edgewins); }
int region_map_bot_size() { return edgewin_list_size(&g_map.bot_edgewins); }

int region_map_delete(const void *key) {
	struct edgewin *next, *i;

	list_for_each_safe(&g_map.top_edgewins, i, next, node) {
		if(i->key == key) {
			delete(i);
			return 0;
		}
	}

	return -1;
}

AVL *region_map_key_regs_alloc() {
	return avl_new( (AvlCompare) void_compare );
}

static void free_regions(AVL *key_regs) {
	AvlIter i;
	
	avl_foreach(i, key_regs) {
		free(i.value);
		avl_remove(key_regs, i.key);
	}
}

void region_map_key_regs_clear(AVL *key_regs) {
	free_regions(key_regs);
	avl_free(key_regs);
	key_regs = region_map_key_regs_alloc();
}

void region_map_key_regs_free(AVL *key_regs) {
	avl_free(key_regs);
}

static void apply_horizontal_edgewins(struct list_head *edgewins, AVL *key_regs,
			int lines, int *rows_left, int cols_left, int reverse) {
	struct edgewin *i;
	struct aug_region *region;
	int y;

	list_for_each(edgewins, i, node) {
		region = malloc( sizeof( struct aug_region ) );
		if(region == NULL)
			err_exit(0, "out of memory");
	
		if(reverse == 0)
			y = lines - *rows_left;
		else
			y = lines - (i->size);
	
		if(init_region(*rows_left-(i->size), cols_left, 
				y, 0, i->size, region) == 0)
			*rows_left -= i->size;
		avl_insert(key_regs, i->key, region);
	}
}

/* apply the region map to a rectangle of lines X columns dimension
 * and store the result in key_regs which is a map from keys to regions.
 * the leftover space is described by the primary output parameter.
 */
int region_map_apply(int lines, int columns, AVL *key_regs, struct aug_region *primary) {
	int rows, cols;

	rows = lines;
	cols = columns;

	if(rows < 1 || cols < 1)
		return -1;

	apply_horizontal_edgewins(
		&g_map.top_edgewins, 
		key_regs,
		lines, 
		&rows,
		cols,
		0
	);
	
	apply_horizontal_edgewins(
		&g_map.bot_edgewins, 
		key_regs,
		lines, 
		&rows,
		cols,
		1
	);
	
	init_region(rows, cols, lines-rows, 0, rows, primary);
	return 0;
}


/* rows: 	the number of rows available
 * cols: 	the number of columns available
 * y:		the starting y coord
 * x:		the starting x coord
 * size:	the size requested for the region
 * region:	output paramter
 */
static int init_region(int rows, int cols, int y, int x, int size, struct aug_region *region) {
	if(rows < 0 || size < 1) {
		region->rows = 0;
		region->cols = 0;

		return -1;
	}
	else {
		region->rows = size; 
		region->cols = cols;
		region->y = y;
		region->x = x;

		return 0;
	}
}
