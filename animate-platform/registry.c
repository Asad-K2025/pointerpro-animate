// Contains all linked list registry logic helpers

#include "shared.h"

void canvas_node_delete(uint64_t canvas_address){
    // delete node from global linked list
    placement_node_t** p_link = &global_placement_registry;
    while (*p_link != NULL) {
        if ((*p_link)->canvas_handle == canvas_address) {
            placement_node_t* temp = *p_link;
            *p_link = (*p_link)->next;
            free(temp);
            continue;
        }
        p_link = &((*p_link)->next);
    }
}

int validate_placement_access(uint64_t placement_handle, const char* username) {
    // ensure client has access to the placement
    pthread_mutex_lock(&registry_lock);

    placement_node_t* curr = global_placement_registry;

    while (curr != NULL) {
        if (curr->placement_handle == placement_handle) {

            uint64_t canvas_handle = curr->canvas_handle;

            canvas_share_node_t* canvas_curr = global_canvas_registry;

            while (canvas_curr != NULL) {
                if (canvas_curr->canvas_handle == canvas_handle) {

                    if (strcmp(canvas_curr->owner_username, username) == 0) {
                        pthread_mutex_unlock(&registry_lock);
                        return 1;
                    }

                    for (int i = 0; i < canvas_curr->shared_count; i++) {
                        if (strcmp(canvas_curr->shared_usernames[i], 
                                   username) == 0) {
                            pthread_mutex_unlock(&registry_lock);
                            return 1;
                        }
                    }

                    pthread_mutex_unlock(&registry_lock);
                    return 0;
                }
                canvas_curr = canvas_curr->next;
            }

            pthread_mutex_unlock(&registry_lock);
            return 0;
        }

        curr = curr->next;
    }

    pthread_mutex_unlock(&registry_lock);
    return 0;
}

int validate_sprite_access(uint64_t sprite_handle, const char* username) {
    // ensure client has access to given sprite
    pthread_mutex_lock(&registry_lock);

    sprite_node_t* curr = global_sprite_registry;

    while (curr != NULL) {

        if (curr->sprite_handle == sprite_handle) {

            int allowed = strcmp(curr->owner_username, username) == 0;

            pthread_mutex_unlock(&registry_lock);

            return allowed;
        }

        curr = curr->next;
    }

    pthread_mutex_unlock(&registry_lock);

    return 0;
}

canvas_share_node_t* find_canvas_node(uint64_t canvas_handle) {
    canvas_share_node_t* curr = global_canvas_registry;

    while (curr != NULL) {
        if (curr->canvas_handle == canvas_handle) {
            return curr;
        }
        curr = curr->next;
    }

    return NULL;
}

sprite_node_t* find_sprite_node(uint64_t sprite_handle) {
    sprite_node_t* curr = global_sprite_registry;

    while (curr != NULL) {
        if (curr->sprite_handle == sprite_handle) {
            return curr;
        }

        curr = curr->next;
    }

    return NULL;
}

int client_has_canvas_access(canvas_share_node_t* node, const char* username) {
    // ensure client has priveleges for canvas
    if (strcmp(node->owner_username, username) == 0) {
        return 1;
    }

    for (int i = 0; i < node->shared_count; i++) {
        if (strcmp(node->shared_usernames[i], username) == 0) {
            return 1;
        }
    }

    return 0;
}