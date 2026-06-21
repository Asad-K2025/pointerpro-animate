// This file contain all hnadlers for each RPC call to animate library

#include "shared.h"

int handle_login(char* response_out, client_session_t* session, char* saveptr){
    // handle login verification and sending back errors to client 
    char* username = strtok_r(NULL, " ", &saveptr);
    if (username == NULL) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    if (strlen(username) > MAX_USERNAME_LEN){
        strcpy(response_out, "-2\n");
        return 0;
    }

    strncpy(session->username, username, MAX_USERNAME_LEN);
    session->username[MAX_USERNAME_LEN] = '\0';

    FILE* file = fopen("users.txt", "r");
    if (file == NULL) {
        strcpy(response_out, "-3\n");
        return 1;
    }

    char line[256];
    int found = 0;
    int balance = 0;

    while (fgets(line, sizeof(line), file)) {
        char current_user[FIFO_NAME_LEN];
        int current_balance;
        if (sscanf(line, "%s %d", current_user, &current_balance) == 2) {
            if (strcmp(current_user, username) == 0) {
                balance = current_balance;
                found = 1;
                break;
            }
        }
    }
    
    fclose(file);

    char response[RESPONSE_LEN];

    if (found) {
        if (balance > 0) {
            sprintf(response, "%d\n", balance);
            strcpy(response_out, response);
            return 0;
        } else {
            sprintf(response, "Reject BALANCE\n");
            strcpy(response_out, response);
            return 1;
        }
    } else {
        sprintf(response, "Reject UNAUTHORISED\n");
        strcpy(response_out, response);
        return 1;
    }
}

int handle_create_canvas(char* response_out, 
                         client_session_t* session, 
                         char* saveptr) {
    char* width_str = strtok_r(NULL, " ", &saveptr);
    char* height_str = strtok_r(NULL, " ", &saveptr);
    char* color_str = strtok_r(NULL, " ", &saveptr);

    if (width_str == NULL || height_str == NULL || color_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* width_end_pointer;
    char* height_end_pointer;
    char* color_end_pointer;

    long width = strtol(width_str, &width_end_pointer, 10);
    long height = strtol(height_str, &height_end_pointer, 10);
    long color = strtol(color_str, &color_end_pointer, 10);

    if (*width_end_pointer != '\0' || *height_end_pointer != '\0' ||
        *color_end_pointer != '\0' || width <= 0 || height <= 0 || color < 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }
    
    pthread_mutex_lock(&animate_lock);
    struct canvas* new_canvas = animate_create_canvas((size_t)height, 
                                                      (size_t)width, 
                                                      (size_t)color);
    pthread_mutex_unlock(&animate_lock);

    if (new_canvas == NULL) {
        strcpy(response_out, "-3\n");
        return 0;
    }

    uint64_t canvas_handle = (uint64_t)new_canvas;

    canvas_share_node_t* new_node = malloc(sizeof(canvas_share_node_t));
    if (new_node == NULL) {
        pthread_mutex_lock(&animate_lock);
        animate_destroy_canvas(new_canvas);
        pthread_mutex_unlock(&animate_lock);

        strcpy(response_out, "-3\n");
        return 0;
    }

    new_node->canvas_handle = canvas_handle;
    strncpy(new_node->owner_username, session->username, MAX_USERNAME_LEN);
    new_node->owner_username[MAX_USERNAME_LEN] = '\0';
    
    new_node->shared_usernames = NULL;
    new_node->shared_count = 0;
    new_node->shared_capacity = 0;
    new_node->ref_count = 1; // held by owner

    new_node->barrier_arrived_count = 0;
    pthread_cond_init(&new_node->barrier_cond, NULL);

    pthread_mutex_lock(&registry_lock);
    new_node->next = global_canvas_registry;
    global_canvas_registry = new_node;
    pthread_mutex_unlock(&registry_lock);

    sprintf(response_out, "0 %lu\n", canvas_handle);
    return 0;
}

int handle_create_rectangle(char* response_out, 
                            client_session_t* session, 
                            char* saveptr) {
    char* width_str = strtok_r(NULL, " ", &saveptr);
    char* height_str = strtok_r(NULL, " ", &saveptr);
    char* color_str = strtok_r(NULL, " ", &saveptr);
    char* filled_str = strtok_r(NULL, " ", &saveptr);

    if (width_str == NULL || height_str == NULL || 
        color_str == NULL || filled_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* width_endptr;
    char* height_endptr;
    char* color_endptr;
    char* filled_endptr;

    long width = strtol(width_str, &width_endptr, 10);
    long height = strtol(height_str, &height_endptr, 10);
    long color = strtol(color_str, &color_endptr, 10);
    long filled = strtol(filled_str, &filled_endptr, 10);

    if (*width_endptr != '\0' || *height_endptr != '\0' || 
        *color_endptr != '\0' || *filled_endptr != '\0' ||
        width <= 0 || height <= 0 || color < 0 || (filled != 0 && filled != 1)) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_lock(&animate_lock);
    struct sprite* new_rectangle_sprite = animate_create_rectangle((size_t)width,
                                                                   (size_t)height, 
                                                                   (color_t)color, 
                                                                   (bool)filled);
    pthread_mutex_unlock(&animate_lock);

    if (new_rectangle_sprite == NULL) {
        strcpy(response_out, "-3\n");
        return 0;
    }

    uint64_t sprite_handle = (uint64_t)new_rectangle_sprite;

    sprite_node_t* new_node = malloc(sizeof(sprite_node_t));

    if (new_node == NULL) {
        pthread_mutex_lock(&animate_lock);
        animate_destroy_sprite(new_rectangle_sprite);
        pthread_mutex_unlock(&animate_lock);
        strcpy(response_out, "-3\n");
        return 0;
    }

    new_node->sprite_handle = sprite_handle;

    strncpy(new_node->owner_username, session->username, MAX_USERNAME_LEN);
    new_node->owner_username[MAX_USERNAME_LEN] = '\0';

    pthread_mutex_lock(&registry_lock);

    new_node->next = global_sprite_registry;
    global_sprite_registry = new_node;

    pthread_mutex_unlock(&registry_lock);

    char response[RESPONSE_LEN];
    sprintf(response, "0 %lu\n", sprite_handle);
    strcpy(response_out, response);
    return 0;
}

int handle_create_circle(char* response_out, 
                         client_session_t* session, 
                         char* saveptr) {
    char* radius_str = strtok_r(NULL, " ", &saveptr);
    char* color_str = strtok_r(NULL, " ", &saveptr);
    char* filled_str = strtok_r(NULL, " ", &saveptr);

    if (radius_str == NULL || color_str == NULL || filled_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* radius_endptr;
    char* color_endptr;
    char* filled_endptr;

    long radius = strtol(radius_str, &radius_endptr, 10);
    long color = strtol(color_str, &color_endptr, 10);
    long filled = strtol(filled_str, &filled_endptr, 10);

    if (*radius_endptr != '\0' || *color_endptr != '\0' || 
        *filled_endptr != '\0' || radius <= 0 || color < 0 || 
        (filled != 0 && filled != 1)) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_lock(&animate_lock);
    struct sprite* new_circle_sprite = animate_create_circle((size_t)radius, 
                                                             (color_t)color, 
                                                             (bool)filled);
    pthread_mutex_unlock(&animate_lock);

    if (new_circle_sprite == NULL) {
        strcpy(response_out, "-3\n");
        return 0;
    }

    uint64_t sprite_handle = (uint64_t)new_circle_sprite;

    sprite_node_t* new_node = malloc(sizeof(sprite_node_t));

    if (new_node == NULL) {
        pthread_mutex_lock(&animate_lock);
        animate_destroy_sprite(new_circle_sprite);
        pthread_mutex_unlock(&animate_lock);
        strcpy(response_out, "-3\n");
        return 0;
    }

    new_node->sprite_handle = sprite_handle;

    strncpy(new_node->owner_username, session->username, MAX_USERNAME_LEN);
    new_node->owner_username[MAX_USERNAME_LEN] = '\0';

    pthread_mutex_lock(&registry_lock);

    new_node->next = global_sprite_registry;
    global_sprite_registry = new_node;

    pthread_mutex_unlock(&registry_lock);

    char response[RESPONSE_LEN];
    sprintf(response, "0 %lu\n", sprite_handle);
    strcpy(response_out, response);
    return 0;
}

int handle_set_animation_params(char* response_out, 
                                client_session_t* session, 
                                char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    char* velocity_x_str = strtok_r(NULL, " ", &saveptr);
    char* velocity_y_str = strtok_r(NULL, " ", &saveptr);
    char* acceleration_x_str = strtok_r(NULL, " ", &saveptr);
    char* acceleration_y_str = strtok_r(NULL, " ", &saveptr);

    if (placement_handle_str == NULL || velocity_x_str == NULL ||
        velocity_y_str == NULL || acceleration_x_str == NULL || 
        acceleration_y_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* placement_endptr;
    char* velocity_x_endptr;
    char* velocity_y_endptr;
    char* acceleration_x_endptr;
    char* acceleration_y_endptr;

    long placement_address = strtol(placement_handle_str, &placement_endptr, 10);
    long velocity_x = strtol(velocity_x_str, &velocity_x_endptr, 10);
    long velocity_y = strtol(velocity_y_str, &velocity_y_endptr, 10);
    long acceleration_x = strtol(acceleration_x_str, &acceleration_x_endptr, 10);
    long acceleration_y = strtol(acceleration_y_str, &acceleration_y_endptr, 10);

    if (*placement_endptr != '\0' || *velocity_x_endptr != '\0' || 
        *velocity_y_endptr != '\0' || *acceleration_x_endptr != '\0' ||
        *acceleration_y_endptr != '\0' || placement_address <= 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    if (!validate_placement_access(placement_address, session->username)) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    struct sprite_placement* target_placement = 
        (struct sprite_placement*)placement_address;
    
    pthread_mutex_lock(&animate_lock);
    animate_set_animation_params(target_placement, 
                                (ssize_t)velocity_x, 
                                (ssize_t)velocity_y, 
                                (ssize_t)acceleration_x, 
                                (ssize_t)acceleration_y);
    pthread_mutex_unlock(&animate_lock);

    strcpy(response_out, "0\n");
    return 0;
}

int handle_create_sprite(char* response_out, 
                         client_session_t* session, 
                        char* saveptr) {
    char* file_str = strtok_r(NULL, " ", &saveptr);

    if (file_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    pthread_mutex_lock(&animate_lock);
    struct sprite* new_sprite = animate_create_sprite(file_str);
    pthread_mutex_unlock(&animate_lock);

    if (new_sprite == NULL) {
        strcpy(response_out, "-3\n");
        return 0;
    }

    uint64_t sprite_handle = (uint64_t)new_sprite;

    sprite_node_t* new_node = malloc(sizeof(sprite_node_t));

    if (new_node == NULL) {
        pthread_mutex_lock(&animate_lock);
        animate_destroy_sprite(new_sprite);
        pthread_mutex_unlock(&animate_lock);
        strcpy(response_out, "-3\n");
        return 0;
    }

    new_node->sprite_handle = sprite_handle;

    strncpy(new_node->owner_username, session->username, MAX_USERNAME_LEN);
    new_node->owner_username[MAX_USERNAME_LEN] = '\0';

    pthread_mutex_lock(&registry_lock);

    new_node->next = global_sprite_registry;
    global_sprite_registry = new_node;

    pthread_mutex_unlock(&registry_lock);

    char response[RESPONSE_LEN];
    sprintf(response, "0 %lu\n", sprite_handle);
    strcpy(response_out, response);
    return 0;
}

int handle_destroy_sprite(char* response_out, 
                          client_session_t* session, 
                          char* saveptr) {
    char* sprite_handle_str = strtok_r(NULL, " ", &saveptr);

    if (sprite_handle_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* sprite_endptr;
    unsigned long long sprite_address = strtoull(sprite_handle_str, 
                                                 &sprite_endptr, 10);

    if (*sprite_endptr != '\0' || sprite_address == 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    if (!validate_sprite_access(sprite_address, session->username)) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    struct sprite* target_sprite = (struct sprite*)sprite_address;
    pthread_mutex_lock(&animate_lock);
    bool failed = animate_destroy_sprite(target_sprite);
    pthread_mutex_unlock(&animate_lock);

    if (!failed) {
        pthread_mutex_lock(&registry_lock);

        sprite_node_t** link = &global_sprite_registry;

        while (*link != NULL) {
            if ((*link)->sprite_handle == sprite_address) {
                sprite_node_t* temp = *link;
                *link = (*link)->next;
                free(temp);
                break;
            }
            link = &((*link)->next);
        }

        pthread_mutex_unlock(&registry_lock);
    }

    if (failed) {
        strcpy(response_out, "0 1\n");
    } else {
        strcpy(response_out, "0 0\n");
    }
    return 0;
}

int handle_place_sprite(char* response_out, 
                        client_session_t* session, 
                        char* saveptr) {
    char* canvas_handle_str = strtok_r(NULL, " ", &saveptr);
    char* sprite_handle_str = strtok_r(NULL, " ", &saveptr);
    char* x_str = strtok_r(NULL, " ", &saveptr);
    char* y_str = strtok_r(NULL, " ", &saveptr);

    if (canvas_handle_str == NULL || sprite_handle_str == NULL || 
        x_str == NULL || y_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* canvas_endptr;
    char* sprite_endptr;
    char* x_endptr;
    char* y_endptr;

    unsigned long long canvas_address = strtoull(canvas_handle_str,
                                                 &canvas_endptr, 10);
    unsigned long long sprite_address = strtoull(sprite_handle_str,
                                                 &sprite_endptr, 10);
    long x = strtol(x_str, &x_endptr, 10);
    long y = strtol(y_str, &y_endptr, 10);

    if (*canvas_endptr != '\0' || *sprite_endptr != '\0' || 
        *x_endptr != '\0' || *y_endptr != '\0' || 
        canvas_address == 0 || sprite_address == 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_lock(&registry_lock);

    canvas_share_node_t* canvas_node = find_canvas_node(canvas_address);

    if (canvas_node == NULL ||
        !client_has_canvas_access(canvas_node, session->username)) {

        pthread_mutex_unlock(&registry_lock);
        strcpy(response_out, "-2\n");
        return 0;
    }

    sprite_node_t* sprite_node = find_sprite_node(sprite_address);

    if (sprite_node == NULL ||
        strcmp(sprite_node->owner_username, session->username) != 0) {

        pthread_mutex_unlock(&registry_lock);
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_unlock(&registry_lock);

    struct canvas* target_canvas = (struct canvas*)canvas_address;
    struct sprite* target_sprite = (struct sprite*)sprite_address;

    pthread_mutex_lock(&animate_lock);
    struct sprite_placement* new_placement = animate_place_sprite(target_canvas,
                                                                  target_sprite, 
                                                                  (ssize_t)x, 
                                                                  (ssize_t)y);
    pthread_mutex_unlock(&animate_lock);

    if (new_placement == NULL) {
        strcpy(response_out, "-3\n");
        return 0;
    }

    uint64_t placement_handle = (uint64_t)new_placement;

    placement_node_t* new_node = malloc(sizeof(placement_node_t));

    if (new_node == NULL) {
        pthread_mutex_lock(&animate_lock);
        animate_destroy_placement(new_placement);
        pthread_mutex_unlock(&animate_lock);
        strcpy(response_out, "-3\n");
        return 0;
    }

    new_node->placement_handle = placement_handle;
    new_node->canvas_handle = canvas_address;

    pthread_mutex_lock(&registry_lock);
    new_node->next = global_placement_registry;
    global_placement_registry = new_node;
    pthread_mutex_unlock(&registry_lock);

    char response[RESPONSE_LEN];
    sprintf(response, "0 %lu\n", placement_handle);
    strcpy(response_out, response);
    return 0;
}

int handle_destroy_canvas(char* response_out, 
                          client_session_t* session, 
                          char* saveptr) {
    char* canvas_handle_str = strtok_r(NULL, " ", &saveptr);
    if (canvas_handle_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* canvas_endptr;
    unsigned long long canvas_address = strtoull(canvas_handle_str, 
                                                 &canvas_endptr, 
                                                 10);
    if (*canvas_endptr != '\0' || canvas_address == 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_lock(&registry_lock);

    canvas_share_node_t** link = &global_canvas_registry;
    canvas_share_node_t* target_node = NULL;

    while (*link != NULL) {
        if ((*link)->canvas_handle == canvas_address) {
            target_node = *link;
            break;
        }
        link = &((*link)->next);
    }

    if (target_node != NULL) {
        if (!client_has_canvas_access(target_node, session->username)) {
            pthread_mutex_unlock(&registry_lock);
            strcpy(response_out, "-2\n");
            return 0;
        }
        target_node->ref_count--;

        int shared_index = -1;
        for (int i = 0; i < target_node->shared_count; i++) {
            if (strcmp(target_node->shared_usernames[i], 
                       session->username) == 0) {
                shared_index = i;
                break;
            }
        }

        if (shared_index != -1) {
            free(target_node->shared_usernames[shared_index]);
            for (int i = shared_index; i < target_node->shared_count - 1; i++) {
                target_node->shared_usernames[i] = 
                    target_node->shared_usernames[i + 1];
            }
            target_node->shared_count--;
        }

        if (target_node->ref_count <= 0) {
            *link = target_node->next;

            for (int i = 0; i < target_node->shared_count; i++) {
                free(target_node->shared_usernames[i]);
            }
            if (target_node->shared_usernames != NULL) {
                free(target_node->shared_usernames);
            }

            pthread_cond_destroy(&target_node->barrier_cond);
            free(target_node);

            pthread_mutex_lock(&animate_lock);
            animate_destroy_canvas((struct canvas*)canvas_address);
            canvas_node_delete(canvas_address);
            pthread_mutex_unlock(&animate_lock);
        } else {
            // other collaborators active, so do not call on this canvas
        }
    } else {
        pthread_mutex_lock(&animate_lock);
        canvas_node_delete(canvas_address);
        animate_destroy_canvas((struct canvas*)canvas_address);
        pthread_mutex_unlock(&animate_lock);
    }

    pthread_mutex_unlock(&registry_lock);

    strcpy(response_out, "0\n");
    return 0;
}

int handle_placement_up(char* response_out, 
                        client_session_t* session, 
                        char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, 
                                                    &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    if (!validate_placement_access(placement_address, session->username)) {
        strcpy(response_out, "-2\n");
        return 0;
    }
    struct sprite_placement* target_placement = 
        (struct sprite_placement*)placement_address;
    pthread_mutex_lock(&animate_lock);
    animate_placement_up(target_placement);
    pthread_mutex_unlock(&animate_lock);

    strcpy(response_out, "0\n");
    return 0;
}

int handle_placement_down(char* response_out, 
                          client_session_t* session, 
                          char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, 
                                                    &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    if (!validate_placement_access(placement_address, session->username)) {
        strcpy(response_out, "-2\n");
        return 0;
    }
    struct sprite_placement* target_placement = 
        (struct sprite_placement*)placement_address;
    pthread_mutex_lock(&animate_lock);
    animate_placement_down(target_placement);
    pthread_mutex_unlock(&animate_lock);

    strcpy(response_out, "0\n");
    return 0;
}

int handle_placement_top(char* response_out,
                         client_session_t* session,
                         char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, 
                                                    &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    if (!validate_placement_access(placement_address, session->username)) {
        strcpy(response_out, "-2\n");
        return 0;
    }
    struct sprite_placement* target_placement = 
        (struct sprite_placement*)placement_address;
    pthread_mutex_lock(&animate_lock);
    animate_placement_top(target_placement);
    pthread_mutex_unlock(&animate_lock);

    strcpy(response_out, "0\n");
    return 0;
}

int handle_placement_bottom(char* response_out, 
                            client_session_t* session, 
                            char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, 
                                                    &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    if (!validate_placement_access(placement_address, session->username)) {
        strcpy(response_out, "-2\n");
        return 0;
    }
    struct sprite_placement* target_placement = 
        (struct sprite_placement*)placement_address;
    pthread_mutex_lock(&animate_lock);
    animate_placement_bottom(target_placement);
    pthread_mutex_unlock(&animate_lock);

    strcpy(response_out, "0\n");
    return 0;
}

int handle_destroy_placement(char* response_out, 
                             client_session_t* session, 
                            char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, 
                                                    &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    if (!validate_placement_access(placement_address, session->username)) {
        strcpy(response_out, "-2\n");
        return 0;
    }
    struct sprite_placement* target_placement = 
        (struct sprite_placement*)placement_address;
    pthread_mutex_lock(&animate_lock);
    animate_destroy_placement(target_placement);
    
    // ensure to delete node for placement
    pthread_mutex_lock(&registry_lock);
    placement_node_t** link = &global_placement_registry;
    while (*link != NULL) {
        if ((*link)->placement_handle == placement_address) {
            placement_node_t* temp = *link;
            *link = (*link)->next;
            free(temp);
            break;
        }
        link = &((*link)->next);
    }
    pthread_mutex_unlock(&registry_lock);
    
    pthread_mutex_unlock(&animate_lock);

    strcpy(response_out, "0\n");
    return 0;
}

int handle_generate(char* response_out, 
                    client_session_t* session, 
                    char* saveptr) {
    char* canvas_str = strtok_r(NULL, " ", &saveptr);
    char* filename = strtok_r(NULL, " ", &saveptr);
    char* start_str = strtok_r(NULL, " ", &saveptr);
    char* end_str = strtok_r(NULL, " ", &saveptr);
    char* framerate_str = strtok_r(NULL, " ", &saveptr);

    if (!canvas_str || !filename || !start_str || !end_str || !framerate_str) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* endptr1;
    char* endptr2;
    char* endptr3;
    char* endptr4;
    unsigned long long canvas_address = strtoull(canvas_str, &endptr1, 10);
    long start_frame = strtol(start_str, &endptr2, 10);
    long end_frame = strtol(end_str, &endptr3, 10);
    long frame_rate = strtol(framerate_str, &endptr4, 10);

    if (*endptr1 != '\0' || *endptr2 != '\0' || *endptr3 != '\0' || 
        *endptr4 != '\0' || canvas_address == 0 || start_frame < 0 || 
        end_frame < start_frame || frame_rate <= 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_lock(&registry_lock);

    canvas_share_node_t* canvas_node = find_canvas_node(canvas_address);

    if (canvas_node == NULL ||
        !client_has_canvas_access(canvas_node, session->username)) {

        pthread_mutex_unlock(&registry_lock);
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_unlock(&registry_lock);

    struct canvas* target_canvas = (struct canvas*)canvas_address;

    char dat_path[MAX_RPC_LINE];
    char mp4_path[MAX_RPC_LINE];
    char log_path[MAX_RPC_LINE];
    snprintf(dat_path, sizeof(dat_path), "%s.dat", filename);
    snprintf(mp4_path, sizeof(mp4_path), "%s.mp4", filename);
    snprintf(log_path, sizeof(log_path), "%s.log", filename);

    FILE* dat_file = fopen(dat_path, "wb");
    if (!dat_file) {
        strcpy(response_out, "0 -1\n");  // data wrtie failed
        return 0;
    }

    pthread_mutex_lock(&animate_lock);
    size_t frame_bytes = animate_frame_size_bytes(target_canvas);
    pthread_mutex_unlock(&animate_lock);

    if (frame_bytes == 0) {
        fclose(dat_file);
        strcpy(response_out, "-3\n");
        return 0;
    }

    void* frame_buffer = malloc(frame_bytes);
    if (!frame_buffer) {
        fclose(dat_file);
        strcpy(response_out, "-3\n");
        return 0;
    }

    for (long frame = start_frame; frame <= end_frame; frame++) {
        pthread_mutex_lock(&animate_lock);
        animate_generate_frame(target_canvas, 
                               (size_t)frame, 
                               (size_t)frame_rate, 
                               frame_buffer);
        pthread_mutex_unlock(&animate_lock);
        
        size_t written = fwrite(frame_buffer, 1, frame_bytes, dat_file);
        if (written != frame_bytes) {
            free(frame_buffer);
            fclose(dat_file);
            strcpy(response_out, "0 -1\n"); // data write failed
            return 0;
        }
    }

    free(frame_buffer);
    fclose(dat_file);

    size_t width = target_canvas->width;
    size_t height = target_canvas->height;

    char ffmpeg_cmd[2048];
    snprintf(ffmpeg_cmd, sizeof(ffmpeg_cmd),
             "ffmpeg -y -r %ld -f rawvideo -pix_fmt argb -s %zux%zu -i %s -c:v libx264 -pix_fmt yuv420p %s > %s 2>&1",
             frame_rate, width, height, dat_path, mp4_path, log_path);

    int sys_status = system(ffmpeg_cmd);
    if (sys_status != 0) {
        strcpy(response_out, "0 0 -1\n"); // .mp4 or .log wrtie failed
        return 0;
    }

    strcpy(response_out, "0 0 0\n");
    return 0;
}

int handle_share_canvas(char* response_out, 
                        client_session_t* session, 
                        char* saveptr) {
    char* canvas_handle_str = strtok_r(NULL, " ", &saveptr);
    char* target_username = strtok_r(NULL, " ", &saveptr);

    if (canvas_handle_str == NULL || target_username == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    if (!valid_authorised_username(target_username)) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    char* canvas_endptr;
    unsigned long long canvas_address = strtoull(canvas_handle_str, 
                                                 &canvas_endptr, 
                                                 10);

    if (*canvas_endptr != '\0' || canvas_address == 0 || 
        strlen(target_username) > MAX_USERNAME_LEN) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_lock(&registry_lock);
    
    canvas_share_node_t* curr = global_canvas_registry;
    canvas_share_node_t* target_node = NULL;
    while (curr != NULL) {
        if (curr->canvas_handle == canvas_address) {
            target_node = curr;
            break;
        }
        curr = curr->next;
    }

    if (target_node == NULL) {  // canvas deosn't exist
        pthread_mutex_unlock(&registry_lock);
        strcpy(response_out, "-2\n");
        return 0;
    }

    int has_access = (strcmp(target_node->owner_username, session->username) == 0);
    for (int i = 0; i < target_node->shared_count; i++) {
        if (strcmp(target_node->shared_usernames[i], session->username) == 0) {
            has_access = 1;
            break;
        }
    }

    if (!has_access) {
        pthread_mutex_unlock(&registry_lock);
        strcpy(response_out, "-2\n"); // client doesn't have right to share it
        return 0;
    }

    int already_shared = 0;
    if (strcmp(target_node->owner_username, target_username) == 0) {
        already_shared = 1;
    }
    for (int i = 0; i < target_node->shared_count; i++) {
        if (strcmp(target_node->shared_usernames[i], target_username) == 0) {
            already_shared = 1;
            break;
        }
    }

    if (!already_shared) {
        if (target_node->shared_count >= target_node->shared_capacity) {
            if (target_node->shared_capacity == 0) {
                target_node->shared_capacity = 4;
            } else {
                target_node->shared_capacity *= 2;
            }
            char** resized = realloc(
                target_node->shared_usernames,
                sizeof(char*) * target_node->shared_capacity
            );

            if (resized == NULL) {
                pthread_mutex_unlock(&registry_lock);
                strcpy(response_out, "-3\n");
                return 0;
            }

            target_node->shared_usernames = resized;
        }
        target_node->shared_usernames[target_node->shared_count] = 
            strdup(target_username);
        target_node->shared_count++;
        target_node->ref_count++;
    }

    pthread_mutex_unlock(&registry_lock);

    strcpy(response_out, "0\n");
    return 0;
}

int handle_barrier(char* response_out, 
                   client_session_t* session, 
                   char* saveptr) {
    char* canvas_handle_str = strtok_r(NULL, " ", &saveptr);

    if (canvas_handle_str == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    char* canvas_endptr;
    unsigned long long canvas_address = strtoull(canvas_handle_str, 
                                                 &canvas_endptr, 10);

    if (*canvas_endptr != '\0' || canvas_address == 0) {
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_lock(&registry_lock);

    canvas_share_node_t* target_node = global_canvas_registry;
    while (target_node != NULL) {
        if (target_node->canvas_handle == canvas_address) {
            break;
        }
        target_node = target_node->next;
    }

    if (target_node == NULL) {
        pthread_mutex_unlock(&registry_lock);
        strcpy(response_out, "-2\n");
        return 0;
    }

    if (!client_has_canvas_access(target_node, session->username)) {
        pthread_mutex_unlock(&registry_lock);
        strcpy(response_out, "-2\n");
        return 0;
    }

    pthread_mutex_lock(&sessions_lock);
    int expected_active_sharers = 0;  // authorised users logged in
    
    for (int i = 0; i < session_count; i++) {
        if (active_sessions[i]->active) {
            int is_authorized = (strcmp(target_node->owner_username, 
                                        active_sessions[i]->username) == 0);
            for (int j = 0; j < target_node->shared_count; j++) {
                if (strcmp(target_node->shared_usernames[j], 
                           active_sessions[i]->username) == 0) {
                    is_authorized = 1;
                    break;
                }
            }
            if (is_authorized) {
                expected_active_sharers++;
            }
        }
    }
    pthread_mutex_unlock(&sessions_lock);

    target_node->barrier_arrived_count++;

    if (target_node->barrier_arrived_count >= expected_active_sharers) {
        target_node->barrier_arrived_count = 0;
        pthread_cond_broadcast(&target_node->barrier_cond);
    } else {
        int dynamic_wait_generation = 1;
        while (dynamic_wait_generation && 
                target_node->barrier_arrived_count > 0) {
            pthread_cond_wait(&target_node->barrier_cond, &registry_lock);
        }
    }

    pthread_mutex_unlock(&registry_lock);

    strcpy(response_out, "0\n");
    return 0;
}
