#include "animate.h"

#include <stdio.h>
#include <stdlib.h>

struct sprite {
    size_t width;
    size_t height;
    color_t* pixels;
};

struct sprite_placement {
    struct sprite* sprite;
    struct canvas* canvas;

    ssize_t x;
    ssize_t y;
    ssize_t vx;
    ssize_t vy;
    ssize_t ax;
    ssize_t ay;

    struct sprite_placement* next;  // for linked list
    struct sprite_placement* previous;
};

struct canvas {
    size_t width;
    size_t height;
    color_t background_color;

    struct sprite_placement* head;  // store top sprite in linked list format
    struct sprite_placement* tail;
};


/* This is the header structure that we can expect to find at position 0 in a bitmap file. */
struct bitmap_header {
    uint8_t  magic[2];          // Expect {'B', 'M'}
    uint32_t size_bytes;        // Size of the file in bytes
    uint16_t reserved[2];
    uint32_t pixel_offset;      // Starting address of pixel data
// Don't pad this struct for alignment
} __attribute__((packed));

/*
 * This header immediately follows bitmap_header in the file.
 * You may ignore all fields except bV5Width and bV5Height, unless you'd like to validate
 * the image format
 */
struct bitmapv5_header {
                               // Offset     Size Description
    uint32_t bV5Size         ; // 0x00          4 Size of this header (124 bytes)
    uint32_t bV5Width        ; // 0x04          4 Width of the bitmap in pixels
    uint32_t bV5Height       ; // 0x08          4 Height of the bitmap in pixels
    uint16_t bV5Planes       ; // 0x0C          2 Number of planes (must be 1)
    uint16_t bV5BitCount     ; // 0x0E          2 Bits per pixel (e.g., 32)
    uint32_t bV5Compression  ; // 0x10          4 BI_RGB (0), BI_BITFIELDS (3)
    uint32_t bV5SizeImage    ; // 0x14          4 Size of image data (0 if uncompressed)
    uint32_t bV5XPelsPerMeter; // 0x18          4 Horizontal pixels per meter
    uint32_t bV5YPelsPerMeter; // 0x1C          4 Vertical pixels per meter
    uint32_t bV5ClrUsed      ; // 0x20          4 Number of color indices used
    uint32_t bV5ClrImportant ; // 0x24          4 Number of important colors
    uint32_t bV5RedMask      ; // 0x28          4 Color mask for red component
    uint32_t bV5GreenMask    ; // 0x2C          4 Color mask for green component
    uint32_t bV5BlueMask     ; // 0x30          4 Color mask for blue component
    uint32_t bV5AlphaMask    ; // 0x34          4 Color mask for alpha channel
    uint32_t bV5CSType       ; // 0x38          4 Color space type (e.g., LCS_CALIBRATED_RGB)
    uint8_t  bV5Endpoints[36]; // 0x3C-0x5B    36 CIE XYZ color space endpoints
    uint32_t bV5GammaRed     ; // 0x5C          4 Gamma red component
    uint32_t bV5GammaGreen   ; // 0x60          4 Gamma green component
    uint32_t bV5GammaBlue    ; // 0x64          4 Gamma blue component
    uint32_t bV5Intent       ; // 0x68          4 Rendering intent
    uint32_t bV5ProfileData  ; // 0x6C          4 Offset to ICC profile data
    uint32_t bV5ProfileSize  ; // 0x70          4 Size of embedded profile data
    uint32_t bV5Reserved     ; // 0x74          4 Reserved (must be 0)
};


struct canvas* animate_create_canvas(size_t height, size_t width,
                                     color_t background_color){
    
    struct canvas* new_canvas = (struct canvas*) malloc(sizeof(struct canvas));
    
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

struct sprite* animate_create_sprite(const char* file) {
    FILE* file_pointer = fopen(file, "rb");
    if (file_pointer == NULL){
        return NULL;
    }

    struct bitmap_header header;
    fread(&header, sizeof(header), 1, file_pointer);

    if (header.magic[0] != 'B' || header.magic[1] !='M'){
        fclose(file_pointer);
        return NULL;
    }

    struct bitmapv5_header header_v5;
    fread(&header_v5, sizeof(header_v5), 1, file_pointer);

    if (header_v5.bV5BitCount != 32) {
        fclose(file_pointer);
        return NULL;
    }

    struct sprite* sprite = malloc(sizeof(struct sprite));

    if (sprite == NULL){
        fclose(file_pointer);
        return NULL;
    }

    sprite->width = header_v5.bV5Width;
    sprite->height = header_v5.bV5Height;
    sprite->pixels = malloc(sprite->width * sprite->height * sizeof(color_t));

    if (sprite->pixels == NULL){
        fclose(file_pointer);
        free(sprite);
        return NULL;
    }

    fseek(file_pointer, header.pixel_offset, SEEK_SET);  // jump to pixel data
    for (size_t y = 0; y < sprite->height; y++){
        for (size_t x = 0; x < sprite->width; x++){

            uint8_t b = fgetc(file_pointer);
            uint8_t g = fgetc(file_pointer);
            uint8_t r = fgetc(file_pointer);
            uint8_t a = fgetc(file_pointer);

            size_t flipped_y = sprite->height - 1 - y; // used as y is unsigned, so reversing till 0 means it will wrap to large number

            sprite->pixels[flipped_y * sprite->width + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    fclose(file_pointer);
    return sprite;
}

struct sprite* animate_create_circle(size_t radius, color_t c, bool filled) {
    
    struct sprite* circle = (struct sprite*) malloc(sizeof(struct sprite));

    if (circle == NULL){
        return NULL;
    }

    size_t diameter = radius * 2 + 1;

    circle->width = diameter;
    circle->height = diameter;
    circle->pixels = calloc(diameter * diameter, sizeof(color_t));

    color_t color = c;
    if ((color >> 24) != 0){
        color = (color & 0x00FFFFFF) | (0xFF << 24); // for non-transparent shape, set to max alpha 255
    }

    if (circle->pixels == NULL){
        free(circle);
        return NULL;
    }
    
    if (filled) {
        for (size_t row = 0; row < diameter; row++){
            for (size_t col = 0; col < diameter; col++){
                ssize_t y = (ssize_t)row - (ssize_t)radius;
                ssize_t x = (ssize_t)col - (ssize_t)radius;

                if (x * x + y * y <= (ssize_t)(radius * radius)){
                    circle->pixels[row * diameter + col] = color;
                }
            }
        }
    }

    return circle;
}

struct sprite* animate_create_rectangle(size_t width, size_t height,
                                        color_t c, bool filled){
    
    struct sprite* rectangle = (struct sprite*) malloc(sizeof(struct sprite));
    
    if (rectangle == NULL){
        return NULL;
    }

    rectangle->width = width;
    rectangle->height = height;

    color_t color = c;
    if ((color >> 24) != 0){
        color = (color & 0x00FFFFFF) | (0xFF << 24); // for non-transparent shape, set to max alpha 255
    }

    // using calloc to set memeory to 0, important for non-filled rectangle
    rectangle->pixels = calloc(width * height, sizeof(color_t));
    if (rectangle->pixels == NULL) {
        free(rectangle);
        return NULL;
    }

    if (filled){
        for (size_t row = 0; row < height; row++){
            for (size_t col = 0; col < width; col++){
                size_t index = row * width + col;
                rectangle->pixels[index] = color;
            }
        }
    } else {
        // only fill the borders of the rectangle since filled is false
        for (size_t col = 0; col < width; col++){ // fill top and bottom rows        
            rectangle->pixels[col] = c;
            rectangle->pixels[(height - 1) * width + col] = c;
        }

        for (size_t row = 1; row < height - 1; row++) {
            rectangle->pixels[row * width] = c;  // left column
            rectangle->pixels[row * width + (width - 1)] = c;  // right column
        }
    }

    return rectangle;
}

bool animate_destroy_sprite(struct sprite* sprite) {
    if (sprite == NULL){
        return 1;
    }

    free(sprite->pixels);
    free(sprite);
    return 0;
}

struct sprite_placement* animate_place_sprite(struct canvas* canvas,
                                              struct sprite* sprite,
                                              ssize_t x, ssize_t y) {
    
    struct sprite_placement* placement = malloc(sizeof(struct sprite_placement));

    if (placement == NULL){
        return NULL;
    }

    placement->sprite = sprite;
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
    // TODO COMP9017
}

void animate_placement_down(struct sprite_placement* sprite_placement){
    // TODO COMP9017
}

void animate_placement_top(struct sprite_placement* sprite_placement){
    if (sprite_placement == NULL){
        return;
    }

    struct canvas* canvas = sprite_placement->canvas;
    struct sprite_placement* previous_head = canvas->head;

    if (canvas->head == previous_head){
        return; // event where placement is already at top
    }

    canvas->head = sprite_placement;
    sprite_placement->next = previous_head;
    previous_head->previous = sprite_placement;
}

void animate_placement_bottom(struct sprite_placement* sprite_placement){
    if (sprite_placement == NULL){
        return;
    }

    struct canvas* canvas = sprite_placement->canvas;
    struct sprite_placement* previous_tail = canvas->tail;

    if (canvas->tail == sprite_placement){
        return; // in event where placement is already at bottom
    }
    canvas->tail = sprite_placement;
    sprite_placement->previous = previous_tail;
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
    
    color_t* frame_buf = (color_t*) buf;  // treating buff as array of color_t using casting
    
    size_t canvas_total_size = canvas->width * canvas->height;
    size_t time = frame/frame_rate;

    color_t background_color = canvas->background_color;
    background_color = (background_color & 0x00FFFFFF) | (0xFF << 24);
    
    // fill background
    for (size_t i = 0; i < canvas_total_size; i++){
        frame_buf[i] = background_color;
    }

    struct sprite_placement* placement = canvas->tail;

    while(placement != NULL){
        struct sprite* sprite = placement->sprite;

        ssize_t position_x = placement->x + placement->vx * time + 0.5 * placement->ax * time * time;
        ssize_t position_y = placement->y + placement->vy * time + 0.5 * placement->ay * time * time;

        for (size_t sprite_y = 0; sprite_y < sprite->height; sprite_y++){
            for (size_t sprite_x = 0; sprite_x < sprite->width; sprite_x++){
                ssize_t canvas_x = position_x + sprite_x;
                ssize_t canvas_y = position_y + sprite_y;

                if (canvas_x < 0 || canvas_y < 0 || 
                    canvas_x >= canvas->width || 
                    canvas_y >= canvas->height){
                        continue; // ensure pixel in canvas bounds
                }
                
                color_t pixel = sprite->pixels[sprite_y * sprite->width + sprite_x];

                if ((pixel >> 24) == 0){
                    continue;  // skip transparent pixels
                }

                pixel = (pixel & 0x00FFFFFF) | (0xFF << 24); //set alpha value to max 255

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

