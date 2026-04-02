#include "types.h"

struct canvas* animate_create_canvas(size_t height, size_t width,
                                     color_t background_color){
    
    struct canvas* new_canvas = malloc(sizeof(struct canvas));
    
    if (new_canvas == NULL){
        return NULL;
    }

    new_canvas->height = height;
    new_canvas->width = width;
    new_canvas->background_color = background_color;

    new_canvas->head = NULL;
    new_canvas->tail = NULL;
    return new_canvas;
}

struct sprite_placement* animate_place_sprite(struct canvas* canvas,
                                              struct sprite* sprite,
                                              ssize_t x, ssize_t y) {
    if (canvas == NULL || sprite == NULL){
        return NULL;
    }
    
    struct sprite_placement* placement = 
        malloc(sizeof(struct sprite_placement));

    if (placement == NULL){
        return NULL;
    }

    placement->sprite = sprite;
    sprite->placement_count++;
    placement->canvas = canvas;
    placement->x = x;
    placement->y = y;
    placement->vx = 0;
    placement->vy = 0;
    placement->ax = 0;
    placement->ay = 0;

    placement->previous = NULL;

    // inserts sprite in top layer of canvas using linked list
    if (canvas->head == NULL){
        canvas->head = placement;
        canvas->tail = placement;
        placement->next = NULL;
    } else {
        struct sprite_placement* previous_head_sprite = canvas->head;
        canvas->head = placement;
        placement->next = previous_head_sprite;
        previous_head_sprite->previous = placement;
    }

    return placement;
}

void animate_placement_up(struct sprite_placement* sprite_placement){
    if (sprite_placement == NULL){
        return;
    }

    struct canvas* canvas = sprite_placement->canvas;

    struct sprite_placement* previous_node = sprite_placement->previous;
    struct sprite_placement* next_node = sprite_placement->next;

    if (previous_node == NULL){
        // current node was head
        return;
    } else if (next_node == NULL){
        // current node was tail, updating pointers correctly
        previous_node->next = NULL;
        canvas->tail = previous_node;
    } else{
        // ensure nodes infront and behind current are connected
        previous_node->next = next_node;
        next_node->previous = previous_node;
    }

    sprite_placement->next = previous_node;
    struct sprite_placement* new_previous_node = previous_node->previous;
    previous_node->previous = sprite_placement;

    if (new_previous_node == NULL){
        // sprite_placement was second node in list
        sprite_placement->previous = NULL;
        canvas->head = sprite_placement;
    } else {
        sprite_placement->previous = new_previous_node;
        new_previous_node->next = sprite_placement;
    }
}

void animate_placement_down(struct sprite_placement* sprite_placement){
    if (sprite_placement == NULL){
        return;
    }

    struct canvas* canvas = sprite_placement->canvas;

    struct sprite_placement* previous_node = sprite_placement->previous;
    struct sprite_placement* next_node = sprite_placement->next;

    if (previous_node == NULL){
        // current node was head, updating pointers correctly
        next_node->previous = NULL;
        canvas->head = next_node;
    } else if (next_node == NULL){
        // current node was tail
        return;
    } else{
        // ensure nodes infront and behind current are connected
        previous_node->next = next_node;
        next_node->previous = previous_node;
    }

    sprite_placement->previous = next_node;
    struct sprite_placement* new_next_node = next_node->next;
    next_node->next = sprite_placement;

    if (new_next_node == NULL){
        // sprite_placement was second last node in list
        sprite_placement->next = NULL;
        canvas->tail = sprite_placement;
    } else {
        sprite_placement->next = new_next_node;
        new_next_node->previous = sprite_placement;
    }
}

void animate_placement_top(struct sprite_placement* sprite_placement){
    if (sprite_placement == NULL){
        return;
    }

    struct canvas* canvas = sprite_placement->canvas;
    struct sprite_placement* previous_head = canvas->head;

    if (sprite_placement == previous_head){
        return; // event where placement is already at top
    }

    struct sprite_placement* previous_node = sprite_placement->previous;
    struct sprite_placement* next_node = sprite_placement->next;

    if (next_node == NULL){
        // current node was tail
        previous_node->next = NULL;
        canvas->tail = previous_node;
    } else{
        // ensure nodes infront and behind current are connected
        previous_node->next = next_node;
        next_node->previous = previous_node;
    }

    canvas->head = sprite_placement;
    sprite_placement->next = previous_head;
    sprite_placement->previous = NULL;
    previous_head->previous = sprite_placement;
}

void animate_placement_bottom(struct sprite_placement* sprite_placement){
    if (sprite_placement == NULL){
        return;
    }

    struct canvas* canvas = sprite_placement->canvas;
    struct sprite_placement* previous_tail = canvas->tail;

    if (sprite_placement == previous_tail){
        return; // in event where placement is already at bottom
    }

    struct sprite_placement* previous_node = sprite_placement->previous;
    struct sprite_placement* next_node = sprite_placement->next;

    if (previous_node == NULL){
        // current node was head
        next_node->previous = NULL;
        canvas->head = next_node;
    } else{
        // ensure nodes infront and behind current are connected
        previous_node->next = next_node;
        next_node->previous = previous_node;
    }

    canvas->tail = sprite_placement;
    sprite_placement->previous = previous_tail;
    sprite_placement->next = NULL;
    previous_tail->next = sprite_placement;
}

void animate_destroy_placement(struct sprite_placement* sprite_placement){
    if (sprite_placement == NULL){
        return;
    } else if (sprite_placement->canvas == NULL){
        // sprite_placement not allocated to a canvas
        free(sprite_placement);
        return;
    }

    struct canvas* canvas = sprite_placement->canvas;
    struct sprite_placement* previous_placement = sprite_placement->previous;
    struct sprite_placement* next_placement = sprite_placement->next;
    sprite_placement->sprite->placement_count--;

    if (previous_placement == NULL && next_placement == NULL){
        // placement was only node in list
        canvas->head = NULL;
        canvas->tail = NULL;
    } else if (previous_placement == NULL){
        // placement was head
        canvas->head = next_placement;
        next_placement->previous = NULL;
    } else if (next_placement == NULL){
        // placement was tail
        canvas->tail = previous_placement;
        previous_placement->next = NULL;
    } else {
        // placement was in middle of linked list
        previous_placement->next = next_placement;
        next_placement->previous = previous_placement;
    }
    free(sprite_placement);
}

void animate_set_animation_params(struct sprite_placement* sprite_placement,
                                  ssize_t vx, ssize_t vy,
                                  ssize_t ax, ssize_t ay){
    if (sprite_placement == NULL) {
        return;
    }

    sprite_placement->vx = vx;
    sprite_placement->vy = vy;
    sprite_placement->ax = ax;
    sprite_placement->ay = ay;
}

void animate_destroy_canvas(struct canvas* canvas){
    if (canvas == NULL) {
        return;
    }

    struct sprite_placement* current = canvas->head;

    while (current != NULL){
        struct sprite_placement* next = current->next;
        current->sprite->placement_count--;
        free(current);
        current = next;
    }

    free(canvas);
}

size_t animate_frame_size_bytes(struct canvas* canvas){
    if (canvas == NULL){
        return 0;
    } else {
        return canvas->width * canvas->height * sizeof(color_t);
    }
}

void animate_generate_frame(const struct canvas* canvas, size_t frame,
                            size_t frame_rate, void* buf) {
    
    if (canvas == NULL || buf == NULL){
        return;
    }
    
                                // treating buff as array of color_t using casting
    color_t* frame_buf = (color_t*) buf;
    
    size_t canvas_total_size = canvas->width * canvas->height;
    if (frame_rate == 0) {
        return; // handle division by zero error. 
    }
    double time = (double)frame/(double)frame_rate;

    color_t background_color = canvas->background_color;
    background_color = (background_color & RGB_MASK) | 
                       (MAX_ALPHA << ALPHA_SHIFT);
    
    // fill background
    for (size_t i = 0; i < canvas_total_size; i++){
        frame_buf[i] = background_color;
    }

    struct sprite_placement* placement = canvas->tail;

    while(placement != NULL){
        struct sprite* sprite = placement->sprite;

        double displacement_x = (double)placement->vx * time + 
                                0.5 * (double)placement->ax * time * time;

        double displacement_y = (double)placement->vy * time + 
                                0.5 * (double)placement->ay * time * time;
        
        ssize_t rounded_dx;
        ssize_t rounded_dy;

        if (displacement_x >= 0) {
            rounded_dx = (ssize_t)(displacement_x + 0.5);
        } else {
            rounded_dx = (ssize_t)(displacement_x - 0.5);
        }

        if (displacement_y >= 0) {
            rounded_dy = (ssize_t)(displacement_y + 0.5);
        } else {
            rounded_dy = (ssize_t)(displacement_y - 0.5);
        }

        ssize_t position_x = placement->x + rounded_dx;        
        ssize_t position_y = placement->y + rounded_dy;

        for (size_t sprite_y = 0; sprite_y < sprite->height; sprite_y++){
            for (size_t sprite_x = 0; sprite_x < sprite->width; sprite_x++){
                ssize_t canvas_x = position_x + (ssize_t)sprite_x;
                ssize_t canvas_y = position_y + (ssize_t)sprite_y;

                if (canvas_x < 0 || canvas_y < 0 || 
                    canvas_x >= (ssize_t)canvas->width || 
                    canvas_y >= (ssize_t)canvas->height){
                        continue; // ensure pixel in canvas bounds
                }
                
                color_t pixel = 
                    sprite->pixels[sprite_y * sprite->width + sprite_x];

                if ((pixel >> ALPHA_SHIFT) == 0){
                    continue;  // skip transparent pixels
                }

                //set alpha value to max 255
                pixel = (pixel & RGB_MASK) | (MAX_ALPHA << ALPHA_SHIFT);

                frame_buf[canvas_y * canvas->width + canvas_x] = pixel;
            }
        }
        placement = placement->previous;
    }

}

// Optional extension
void animate_set_animation_function(struct sprite_placement* sprite_placement,
                                    animate_fn, void* priv) {
}

