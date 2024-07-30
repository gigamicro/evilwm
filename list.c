/* evilwm - minimalist window manager for X11
 * Copyright (C) 1999-2022 Ciaran Anscomb <evilwm@6809.org.uk>
 * see README for license and other details. */

// Basic linked list handling code.  Operations that modify the list
// return the new list head.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "list.h"

// Add new data to head of list
struct list *list_prepend(struct list *list, void *data) {
	struct list *elem = malloc(sizeof(struct list));
	if (!elem) return list; // malloc fail
	*elem = (struct list){ list, data };
	return elem; // prepend to or create list
}

static struct list *tail(struct list *list) {
	while (list->next) list = list->next;
	return list;
}

// Add new data to tail of list
struct list *list_append(struct list *list, void *data) {
	struct list *elem = list_prepend(NULL, data);
	if (!list) return elem; // new list
	tail(list)->next = elem;
	return list;
}

static struct list *del(struct list *list) {
	struct list *elem = list;
	list = elem->next;
	free(elem);
	return list;
}

// Delete list element containing data
struct list *list_delete(struct list *list, void *data) {
	if (!list) return list; // empty
	if (list->data == data) // first element match
		return del(list);
	struct list *prev = list_find_prev(list,data);
	prev->next=del(prev->next);
	return list;
}

// Move existing list element containing data to head of list
struct list *list_to_head(struct list *list, void *data) {
	if (!data) return list;
	if (list->data == data) return list; // already there
	struct list *prev = list_find_prev(list,data);
	if (!prev) return list_prepend(list, data); // wasn't in list
	struct list *elem = prev->next;
	prev->next=elem->next;
	elem->next=list;
	return elem;
}

// static struct list *f(struct list *list) {}
// struct list *p = (struct list *)(((struct list *)&(list->next))-list);

// Move existing list element containing data to tail of list
struct list *list_to_tail(struct list *list, void *data) {
	if (!data) return list;
	if (!list) return list_append(list, data); // no list, new list
	struct list *elem;
	if (list->data == data) { // head to tail
		elem = list;
		list = list->next; // advance list by one
		if (!list) return elem; // single element list
		tail(list)->next = elem;
		elem->next = NULL;
	} else {
		struct list *prev = list_find_prev(list,data);
		if (!prev) return list_append(list, data); // wasn't in list
		elem = prev->next;
		prev->next = elem->next; // unthread elem
		tail(prev)->next = elem;
		elem->next = NULL;
	}
	return list;
}

// Find list element containing data
struct list *list_find(struct list *list, void *data) {
	while (list && list->data != data) list=list->next;
	return list;
}
// find the previous element to that, as ling as it isn't the head
struct list *list_find_prev(struct list *list, void *data) {
	// list should not be null, list->data will not be checked
	while (list->next && list->next->data != data) list=list->next;
	if (!list->next) return NULL;
	return list;
}
