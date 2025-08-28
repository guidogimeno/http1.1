#include "../gg_stdlib.h"

typedef struct Foo {
    int bar;
    int baz;
} Foo;

typedef struct Node Node;
struct Node {
    Node *next;
    Node *prev;
    String key;
    Foo foo;
};

typedef struct Slot {
    Node *first;
    Node *last;
} Slot;

typedef struct Manager {
    int slots_count;
    Slot *slots;
} Manager;

void insert(Allocator *allocator, Manager *mgr, String key, Foo foo) {
    u32 hashed_key = hash_string(key);
    int slot_index = hashed_key % mgr->slots_count;
    Slot *slot = &mgr->slots[slot_index];

    bool node_found = false;
    for (Node *node = slot->first; node != NULL; node = node->next) {
        if (string_eq(node->key, key)) {
            node_found = true;
            node->foo = foo;
            break;
        }
    }

    if (!node_found) {
        Node *new_node = alloc(allocator, sizeof(Node));
        new_node->key = key;
        new_node->foo = foo;
        new_node->prev = NULL;
        new_node->next = NULL;

        if (slot->first == NULL) {
            slot->first = new_node;
            slot->last = new_node;
        } else {
            slot->last->next = new_node;
            new_node->prev = slot->last;
            slot->last = new_node;
        }
    }
}

void remove_item(Manager *mgr, String key) {
    u32 hashed_key = hash_string(key);
    int slot_index = hashed_key % mgr->slots_count;
    Slot *slot = &mgr->slots[slot_index];

    for (Node *node = slot->first; node != NULL; node = node->next) {
        if (string_eq(node->key, key)) {
            if (slot->first == node) {
                slot->first = node->next;
            } 

            if (slot->last == node) {
                slot->last = node->prev;
            } 

            if (node->prev != NULL) {
                node->prev->next = node->next;
            }

            if (node->next != NULL) {
                node->next->prev = node->prev;
            }
        }
    }
}

void print(Manager mgr) {
    printf("******************\n");
    for (u32 i = 0; i < mgr.slots_count; i++) {
        Slot *slot = &mgr.slots[i];

        u32 node_number = 0;
        for (Node *node = slot->first; node != NULL; node = node->next) {
            printf("slot=%d node=%d clave=%.*s valor=%d\n", i, node_number, string_print(node->key), node->foo.bar);
            node_number++;
        }
    }
    printf("******************\n\n\n");
}

int main() {
    Allocator *allocator = allocator_make(1 * GB);

    { // remove al vacio
        Manager mgr = {
            .slots_count = 10,
            .slots = alloc(allocator, sizeof(Slot) * 10),
        };

        remove_item(&mgr, string("primero"));

        print(mgr);
    }

    { // insert varios
        Manager mgr = {
            .slots_count = 10,
            .slots = alloc(allocator, sizeof(Slot) * 10),
        };

        insert(allocator, &mgr, string("primero"), (Foo){ .bar = 1 });
        insert(allocator, &mgr, string("segundo"), (Foo){ .bar = 1 });

        remove_item(&mgr, string("primero"));
        remove_item(&mgr, string("segundo"));

        insert(allocator, &mgr, string("sexto"), (Foo){ .bar = 1 });
        insert(allocator, &mgr, string("septimo"), (Foo){ .bar = 1 });

        print(mgr);
    }

    { // insert remove insert
        Manager mgr = {
            .slots_count = 10,
            .slots = alloc(allocator, sizeof(Slot) * 10),
        };

        insert(allocator, &mgr, string("primero"), (Foo){ .bar = 1 });
        remove_item(&mgr, string("primero"));
        insert(allocator, &mgr, string("primero"), (Foo){ .bar = 2 });

        print(mgr);
    }

    return 0;
}

