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

