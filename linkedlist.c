/*-
 * Copyright (c) 2013 Abhinav Upadhyay <er.abhinav.upadhyay@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <util.h>

#include "linkedlist.h"

mandb_list *
mandb_list_init(void (*callback)(void *))
{
    mandb_list *list = emalloc(sizeof(struct mandb_list));
    list->head = NULL;
    list->tail = NULL;
    if (callback == NULL)
        list->free_callback = free;
    else
        list->free_callback = callback;
    return list;
}

void
mandb_list_add_node(mandb_list *list, void **data)
{
    if (list == NULL) {
        warnx("list is NULL\n");
        return;
    }
    /* list is empty */
    if (list->head == NULL) {
        list->head = emalloc(sizeof(mandb_list_node));
        list->head->data = *data;
        list->head->next = NULL;
        list->tail = list->head;
        return;
    }


    mandb_list_node *n = emalloc(sizeof(mandb_list_node));
    n->data = *data;
    n->next = NULL;
    list->tail->next = n;
    list->tail = n;
}

void
mandb_list_remove_node(mandb_list *list, mandb_list_node *node)
{
    /* if the list is empty, do nothing */
    if (list->head == NULL)
        return;

    /* if the list has one element */
    if (list->head == list->tail) {
        free(list->head);
        list->head = NULL;
        list->tail = NULL;
        return;
    }

    /* The normal case */
    mandb_list_node *n = list->head;
    mandb_list_node *prev_node;
    while (n->next != NULL) {
        prev_node = n;
        n = n->next;
        if (n == node) {
            prev_node->next = n->next;
            free(n);
        }
    }
            

}

void
mandb_list_free(mandb_list *list)
{
    if (list == NULL)
        return;

    if (list->head == NULL)
        free(list);
        return;

    if (list->head == list->tail) {
        list->free_callback(list->head->data);
        free(list->head);
        free(list);
        return;
    }

    mandb_list_node *n = list->head;
    mandb_list_node *next;
    while (n->next != NULL) {
        next = n->next;
        list->free_callback(n->data);
        free(n);
        n = next;
    }
    free(list);
    return;
}

