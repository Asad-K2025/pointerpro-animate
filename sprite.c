#include "types.h"

struct sprite* animate_create_sprite(const char* file) {
    FILE* file_pointer = fopen(file, "rb");
    if (file_pointer == NULL){
        return NULL;
    }

    struct bitmap_header header;
    if (fread(&header, sizeof(header), 1, file_pointer) != 1) {
        fclose(file_pointer);
        return NULL;
    }

    if (header.magic[0] != 'B' || header.magic[1] !='M'){
        fclose(file_pointer);
        return NULL;
    }

    struct bitmapv5_header header_v5;
    if (fread(&header_v5, sizeof(header_v5), 1, file_pointer) != 1) {
        fclose(file_pointer);
        return NULL;
    }

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
    sprite->placement_count = 0;
    sprite->pixels = malloc(sprite->width * sprite->height * sizeof(color_t));

    // perform overflow check by rearranging multiplication order
    if (sprite->width != 0 && 
        sprite->height > SIZE_MAX / sprite->width) {
        fclose(file_pointer);
        free(sprite);
        return NULL;
    }

    if (sprite->pixels == NULL){
        fclose(file_pointer);
        free(sprite);
        return NULL;
    }

    fseek(file_pointer, header.pixel_offset, SEEK_SET);  // jump to pixel data
    if (fseek(file_pointer, header.pixel_offset, SEEK_SET) != 0) {
        fclose(file_pointer);
        free(sprite->pixels);
        free(sprite);
        return NULL;
    }

    for (size_t y = 0; y < sprite->height; y++){
        for (size_t x = 0; x < sprite->width; x++){

            // store as int to detect EOF correctly
            int blue = fgetc(file_pointer);
            int green = fgetc(file_pointer);
            int red = fgetc(file_pointer);
            int alpha = fgetc(file_pointer);

            if (blue == EOF || green == EOF || red == EOF || alpha == EOF) {
                fclose(file_pointer);
                free(sprite->pixels);
                free(sprite);
                return NULL;
            }

            // flip vertically as bitmap rows stored in reverse
            size_t flipped_y = sprite->height - 1 - y;

            sprite->pixels[flipped_y * 
                    sprite->width + x] = ((uint8_t)alpha << ALPHA_SHIFT) | 
                                         ((uint8_t)red << 16) | 
                                         ((uint8_t)green << 8) | 
                                         (uint8_t)blue;
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
    circle->placement_count = 0;
    circle->pixels = calloc(diameter * diameter, sizeof(color_t));

    if (circle->pixels == NULL){
        free(circle);
        return NULL;
    }
    
    for (size_t row = 0; row < diameter; row++){
        for (size_t col = 0; col < diameter; col++){
            ssize_t y = (ssize_t)row - (ssize_t)radius;
            ssize_t x = (ssize_t)col - (ssize_t)radius;

            if (x * x + y * y <= (ssize_t)(radius * radius)){
                circle->pixels[row * diameter + col] = c;
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
    rectangle->placement_count = 0;

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
                rectangle->pixels[index] = c;
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
        return 0;
    }

    if (sprite->placement_count != 0){
        return 1; // sprite is still in use
    }

    free(sprite->pixels);
    free(sprite);
    return 0;
}