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

// Wrap data in a new list container
static struct list *list_new(void *data) {
	struct list *new = malloc(sizeof(*new));
	if (!new)
		return NULL;
	new->next = NULL;
	new->data = data;
	return new;
}

// Insert new data before given position
struct list *list_insert_before(struct list *list, struct list *before, void *data) {
	if (!before)
		return list_append(list,data);
	struct list *elem = list_new(data);
	if (!elem)
		return list;
	if (!list)
		return elem;
	elem->next = before;
	if (before == list)
		return elem;
	for (struct list *iter = list; iter; iter = iter->next) {
		if (iter->next == before) {
			iter->next = elem;
			return list;
		}
		if (!iter->next) {
			elem->next = NULL;
			iter->next = elem;
			return list;
		}
	}
	abort();
}

// Add new data to head of list
struct list *list_prepend(struct list *list, void *data) {
	return list_insert_before(list, list, data);
}

// Add new data to tail of list
struct list *list_append(struct list *list, void *data) {
	struct list *elem = list_new(data);
	if (!list) return elem;
	struct list *tail = list;
	while (tail->next) tail=tail->next;
	tail->next=elem;
	return list;
}

// Delete list element containing data
struct list *list_delete(struct list *list, void *data) {
	if (!data)
		return list;
	if (list->data == data) {
		struct list *elem = list;
		list = elem->next;
		free(elem);
		return list;
	}
	struct list *prev = list_find_prev(list,data);
	struct list *elem = prev->next;
	prev->next=elem->next;
	free(elem);
	return list;
}

// Move existing list element containing data to head of list
struct list *list_to_head(struct list *list, void *data) {
	if (!data)
		return list;
	if (list->data == data)
		return list;
	struct list *cont = list_find_prev(list,data);
	if (!cont) return list_prepend(list, data);
	struct list *mvelem = cont->next;
	cont->next=mvelem->next;
	mvelem->next=list;
	return mvelem;
}

// Move existing list element containing data to tail of list
struct list *list_to_tail(struct list *list, void *data) {
	if (!data)
		return list;
	if (list->data == data) {
		struct list *elem = list;
		list = list->next;
		if (!list) return elem; // single element
		struct list *tail = list;
		while (tail->next) tail=tail->next;
		tail->next = elem;
		elem->next = NULL;
		return list;
	}
	struct list *cont = list_find_prev(list,data);
	if (!cont) return list_append(list, data);
	struct list *mvelem = cont->next;
	cont->next=mvelem->next;
	while (cont->next) cont=cont->next;
	cont->next=mvelem;
	mvelem->next=NULL;
	return list;
}

// Find list element containing data
struct list *list_find(struct list *list, void *data) {
	for (struct list *elem = list; elem; elem = elem->next) {
		if (elem->data == data)
			return elem;
	}
	return NULL;
}
// find the previous element to that
struct list *list_find_prev(struct list *list, void *data) {
	if (list->data == data) return list_prepend(list,NULL); // XXX
	for (struct list *elem = list; elem->next; elem = elem->next)
		if (elem->next->data == data)
			return elem;
	return NULL;
}
