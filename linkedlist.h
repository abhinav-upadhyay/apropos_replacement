#ifndef LINKEDLIST_H
#define LINKSEDLIST_H

typedef struct mandb_list_node {
    void *data;
    struct mandb_list_node *next;
} mandb_list_node;

typedef struct mandb_list {
    struct mandb_list_node *head;
    struct mandb_list_node *tail;
    void (*free_callback)(void *);
} mandb_list;

mandb_list *mandb_list_init(void (*free_callback)(void *));
void mandb_list_add_node(mandb_list *, void **);
void mandb_list_remove_node(mandb_list *, mandb_list_node *);
void mandb_list_free(mandb_list *);
#endif
