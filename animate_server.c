
#include <animate/animate.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <poll.h>

volatile sig_atomic_t client_requested = 0;
volatile pid_t pending_client_pid = 0;


typedef struct {  // tracks every authenticated and active client
    pid_t client_pid;
    int fd_c2s;
    int fd_s2c;
    char username[33];
    int active;

    char read_buf[512];
    size_t read_len;

    // tickets ensure order of requests for each client
    uint32_t next_ticket;
    uint32_t expected_ticket; // currently allowed to write to pipe
    pthread_mutex_t order_lock;
    pthread_cond_t order_cond;
} client_session_t;

client_session_t* active_sessions = NULL;
int session_count = 0;
pthread_mutex_t sessions_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct rpc_task {  // thread pool task structure
    char command_line[512];
    client_session_t* session;
    uint32_t ticket;
    struct rpc_task* next;
} rpc_task_t;

typedef struct {
    rpc_task_t* head;
    rpc_task_t* tail;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} task_queue_t;

int process_rpc_command(int fd_s2c, char* command_line);

task_queue_t work_queue = {NULL, NULL, PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER};

void queue_push(const char* command, client_session_t* session, uint32_t ticket) {
    rpc_task_t* new_task = malloc(sizeof(rpc_task_t));
    if (new_task == NULL) {
        return;
    }

    strncpy(new_task->command_line, command, sizeof(new_task->command_line) - 1);
    new_task->command_line[sizeof(new_task->command_line) - 1] = '\0';
    new_task->session = session;
    new_task->ticket = ticket;
    new_task->next = NULL;

    pthread_mutex_lock(&work_queue.lock);
    if (work_queue.tail == NULL) {
        work_queue.head = new_task;
        work_queue.tail = new_task;
    } else {
        work_queue.tail->next = new_task;
        work_queue.tail = new_task;
    }
    pthread_cond_signal(&work_queue.cond);
    pthread_mutex_unlock(&work_queue.lock);
}

rpc_task_t* queue_pop(void) {
    pthread_mutex_lock(&work_queue.lock);

    while (work_queue.head == NULL) {
        pthread_cond_wait(&work_queue.cond, &work_queue.lock);
    }

    rpc_task_t* task = work_queue.head;
    work_queue.head = work_queue.head->next;

    if (work_queue.head == NULL) {
        work_queue.tail = NULL;
    }

    pthread_mutex_unlock(&work_queue.lock);
    return task;
}

void* worker_thread_routine(void* arg) {
    while (1) {
        rpc_task_t* task = queue_pop();
        if (task != NULL) {
            pthread_mutex_lock(&task->session->order_lock);
            while (task->session->expected_ticket != task->ticket) {
                pthread_cond_wait(&task->session->order_cond, &task->session->order_lock);
            }

            int should_disconnect = process_rpc_command(task->session->fd_s2c, task->command_line);

            task->session->expected_ticket++;
            pthread_cond_broadcast(&task->session->order_cond);
            pthread_mutex_unlock(&task->session->order_lock);
            
            if (should_disconnect) { // find and close the client
                pthread_mutex_lock(&sessions_lock);
                sleep(1);
                task->session->active = 0;

                close(task->session->fd_c2s);
                close(task->session->fd_s2c);
                
                char fifo_c2s[64];
                char fifo_s2c[64];
                sprintf(fifo_c2s, "FIFO_C2S_%ld", (long)task->session->client_pid);
                sprintf(fifo_s2c, "FIFO_S2C_%ld", (long)task->session->client_pid);
                unlink(fifo_c2s);
                unlink(fifo_s2c);
                pthread_mutex_unlock(&sessions_lock);
                pthread_mutex_destroy(&task->session->order_lock);
                pthread_cond_destroy(&task->session->order_cond);
            }
            free(task);
        }
    }
    return NULL;
}

void handle_sigusr1(int singal_number, siginfo_t* info, void* context){
    pending_client_pid = info->si_pid;
    client_requested = 1;
}

int handle_login(int fd_s2c, char* saveptr){
    char* username = strtok_r(NULL, " ", &saveptr);
    if (username == NULL) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    if (strlen(username) > 32){
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    FILE* file = fopen("users.txt", "r");
    if (file == NULL) {
        write(fd_s2c, "-3\n", 3);
        return 1;
    }

    char line[256];
    int found = 0;
    int balance = 0;

    while (fgets(line, sizeof(line), file)) {
        char current_user[64];
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

    char response[128];

    if (found) {
        if (balance > 0) {
            sprintf(response, "%d\n", balance);
            write(fd_s2c, response, strlen(response));
            return 0;
        } else {
            sprintf(response, "Reject BALANCE\n");
            write(fd_s2c, response, strlen(response));
            return 1;
        }
    } else {
        sprintf(response, "Reject UNAUTHORISED\n");
        write(fd_s2c, response, strlen(response));
        return 1;
    }
}

int handle_create_canvas(int fd_s2c, char* saveptr) {
    char* width_str = strtok_r(NULL, " ", &saveptr);
    char* height_str = strtok_r(NULL, " ", &saveptr);
    char* color_str = strtok_r(NULL, " ", &saveptr);

    if (width_str == NULL || height_str == NULL || color_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* width_end_pointer;
    char* height_end_pointer;
    char* color_end_pointer;

    long width = strtol(width_str, &width_end_pointer, 10);
    long height = strtol(height_str, &height_end_pointer, 10);
    long color = strtol(color_str, &color_end_pointer, 10);

    if (*width_end_pointer != '\0' || *height_end_pointer != '\0' || *color_end_pointer != '\0' ||
        width <= 0 || height <= 0 || color < 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct canvas* new_canvas = animate_create_canvas((size_t)height, (size_t)width, (size_t)color);

    if (new_canvas == NULL) {
        write(fd_s2c, "-3\n", 3);
        return 0;
    }

    uint64_t canvas_handle = (uint64_t)new_canvas;

    char response[128];
    sprintf(response, "0 %lu\n", canvas_handle);
    write(fd_s2c, response, strlen(response));
    return 0;
}

int handle_create_rectangle(int fd_s2c, char* saveptr) {
    char* width_str = strtok_r(NULL, " ", &saveptr);
    char* height_str = strtok_r(NULL, " ", &saveptr);
    char* color_str = strtok_r(NULL, " ", &saveptr);
    char* filled_str = strtok_r(NULL, " ", &saveptr);

    if (width_str == NULL || height_str == NULL || color_str == NULL || filled_str == NULL) {
        write(fd_s2c, "-1\n", 3);
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

    if (*width_endptr != '\0' || *height_endptr != '\0' || *color_endptr != '\0' || *filled_endptr != '\0' ||
        width <= 0 || height <= 0 || color < 0 || (filled != 0 && filled != 1)) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct sprite* new_rectangle_sprite = animate_create_rectangle((size_t)width, (size_t)height, (color_t)color, (bool)filled);
    if (new_rectangle_sprite == NULL) {
        write(fd_s2c, "-3\n", 3);
        return 0;
    }

    uint64_t sprite_handle = (uint64_t)new_rectangle_sprite;
    char response[128];
    sprintf(response, "0 %lu\n", sprite_handle);
    write(fd_s2c, response, strlen(response));
    return 0;
}

int handle_create_circle(int fd_s2c, char* saveptr) {
    char* radius_str = strtok_r(NULL, " ", &saveptr);
    char* color_str = strtok_r(NULL, " ", &saveptr);
    char* filled_str = strtok_r(NULL, " ", &saveptr);

    if (radius_str == NULL || color_str == NULL || filled_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* radius_endptr;
    char* color_endptr;
    char* filled_endptr;

    long radius = strtol(radius_str, &radius_endptr, 10);
    long color = strtol(color_str, &color_endptr, 10);
    long filled = strtol(filled_str, &filled_endptr, 10);

    if (*radius_endptr != '\0' || *color_endptr != '\0' || *filled_endptr != '\0' ||
        radius <= 0 || color < 0 || (filled != 0 && filled != 1)) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct sprite* new_circle_sprite = animate_create_circle((size_t)radius, (color_t)color, (bool)filled);
    if (new_circle_sprite == NULL) {
        write(fd_s2c, "-3\n", 3);
        return 0;
    }

    uint64_t sprite_handle = (uint64_t)new_circle_sprite;
    char response[128];
    sprintf(response, "0 %lu\n", sprite_handle);
    write(fd_s2c, response, strlen(response));
    return 0;
}

int handle_set_animation_params(int fd_s2c, char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    char* velocity_x_str = strtok_r(NULL, " ", &saveptr);
    char* velocity_y_str = strtok_r(NULL, " ", &saveptr);
    char* acceleration_x_str = strtok_r(NULL, " ", &saveptr);
    char* acceleration_y_str = strtok_r(NULL, " ", &saveptr);

    if (placement_handle_str == NULL || velocity_x_str == NULL || velocity_y_str == NULL || acceleration_x_str == NULL || acceleration_y_str == NULL) {
        write(fd_s2c, "-1\n", 3);
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

    if (*placement_endptr != '\0' || *velocity_x_endptr != '\0' || *velocity_y_endptr != '\0' || *acceleration_x_endptr != '\0' || *acceleration_y_endptr != '\0' ||
        placement_address <= 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct sprite_placement* target_placement = (struct sprite_placement*)placement_address;
    
    animate_set_animation_params(target_placement, (ssize_t)velocity_x, (ssize_t)velocity_y, (ssize_t)acceleration_x, (ssize_t)acceleration_y);

    write(fd_s2c, "0\n", 2);
    return 0;
}

int handle_create_sprite(int fd_s2c, char* saveptr) {
    char* file_str = strtok_r(NULL, " ", &saveptr);

    if (file_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    struct sprite* new_sprite = animate_create_sprite(file_str);
    if (new_sprite == NULL) {
        write(fd_s2c, "-3\n", 3);
        return 0;
    }

    uint64_t sprite_handle = (uint64_t)new_sprite;
    char response[128];
    sprintf(response, "0 %lu\n", sprite_handle);
    write(fd_s2c, response, strlen(response));
    return 0;
}

int handle_destroy_sprite(int fd_s2c, char* saveptr) {
    char* sprite_handle_str = strtok_r(NULL, " ", &saveptr);

    if (sprite_handle_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* sprite_endptr;
    unsigned long long sprite_address = strtoull(sprite_handle_str, &sprite_endptr, 10);

    if (*sprite_endptr != '\0' || sprite_address == 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct sprite* target_sprite = (struct sprite*)sprite_address;
    bool failed = animate_destroy_sprite(target_sprite);

    if (failed) {
        write(fd_s2c, "0 1\n", 4);
    } else {
        write(fd_s2c, "0 0\n", 4);
    }
    return 0;
}

int handle_place_sprite(int fd_s2c, char* saveptr) {
    char* canvas_handle_str = strtok_r(NULL, " ", &saveptr);
    char* sprite_handle_str = strtok_r(NULL, " ", &saveptr);
    char* x_str = strtok_r(NULL, " ", &saveptr);
    char* y_str = strtok_r(NULL, " ", &saveptr);

    if (canvas_handle_str == NULL || sprite_handle_str == NULL || x_str == NULL || y_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* canvas_endptr;
    char* sprite_endptr;
    char* x_endptr;
    char* y_endptr;

    unsigned long long canvas_address = strtoull(canvas_handle_str, &canvas_endptr, 10);
    unsigned long long sprite_address = strtoull(sprite_handle_str, &sprite_endptr, 10);
    long x = strtol(x_str, &x_endptr, 10);
    long y = strtol(y_str, &y_endptr, 10);

    if (*canvas_endptr != '\0' || *sprite_endptr != '\0' || *x_endptr != '\0' || *y_endptr != '\0' ||
        canvas_address == 0 || sprite_address == 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct canvas* target_canvas = (struct canvas*)canvas_address;
    struct sprite* target_sprite = (struct sprite*)sprite_address;

    struct sprite_placement* new_placement = animate_place_sprite(target_canvas, target_sprite, (ssize_t)x, (ssize_t)y);
    if (new_placement == NULL) {
        write(fd_s2c, "-3\n", 3);
        return 0;
    }

    uint64_t placement_handle = (uint64_t)new_placement;
    char response[128];
    sprintf(response, "0 %lu\n", placement_handle);
    write(fd_s2c, response, strlen(response));
    return 0;
}

int handle_destroy_canvas(int fd_s2c, char* saveptr) {
    char* canvas_handle_str = strtok_r(NULL, " ", &saveptr);

    if (canvas_handle_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* canvas_endptr;
    unsigned long long canvas_address = strtoull(canvas_handle_str, &canvas_endptr, 10);

    if (*canvas_endptr != '\0' || canvas_address == 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct canvas* target_canvas = (struct canvas*)canvas_address;
    animate_destroy_canvas(target_canvas);

    write(fd_s2c, "0\n", 2);
    return 0;
}

int handle_placement_up(int fd_s2c, char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct sprite_placement* target_placement = (struct sprite_placement*)placement_address;
    animate_placement_up(target_placement);

    write(fd_s2c, "0\n", 2);
    return 0;
}

int handle_placement_down(int fd_s2c, char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct sprite_placement* target_placement = (struct sprite_placement*)placement_address;
    animate_placement_down(target_placement);

    write(fd_s2c, "0\n", 2);
    return 0;
}

int handle_placement_top(int fd_s2c, char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct sprite_placement* target_placement = (struct sprite_placement*)placement_address;
    animate_placement_top(target_placement);

    write(fd_s2c, "0\n", 2);
    return 0;
}

int handle_placement_bottom(int fd_s2c, char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct sprite_placement* target_placement = (struct sprite_placement*)placement_address;
    animate_placement_bottom(target_placement);

    write(fd_s2c, "0\n", 2);
    return 0;
}

int handle_destroy_placement(int fd_s2c, char* saveptr) {
    char* placement_handle_str = strtok_r(NULL, " ", &saveptr);
    if (placement_handle_str == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    char* placement_endptr;
    unsigned long long placement_address = strtoull(placement_handle_str, &placement_endptr, 10);

    if (*placement_endptr != '\0' || placement_address == 0) {
        write(fd_s2c, "-2\n", 3);
        return 0;
    }

    struct sprite_placement* target_placement = (struct sprite_placement*)placement_address;
    animate_destroy_placement(target_placement);

    write(fd_s2c, "0\n", 2);
    return 0;
}

// after processing, return 1 for should_disconnect, 0 otherwise
int process_rpc_command(int fd_s2c, char* command_line){
    size_t len = strlen(command_line);
    if (len > 0 && command_line[len - 1] == '\n'){
        command_line[len - 1] = '\0';
    }

    char* saveptr;
    char* cmd = strtok_r(command_line, " ", &saveptr);

    if (cmd == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    // forbidden function calls
    if (strcmp(cmd, "set_animation_function") == 0 ||
        strcmp(cmd, "frame_size_bytes") == 0 ||
        strcmp(cmd, "generate_frame") == 0) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    if (strcmp(cmd, "Login") == 0){
        return handle_login(fd_s2c, saveptr);
    } else if (strcmp(cmd, "create_canvas") == 0) {
        return handle_create_canvas(fd_s2c, saveptr);
    } else if (strcmp(cmd, "create_rectangle") == 0){
        return handle_create_rectangle(fd_s2c, saveptr);
    } else if (strcmp(cmd, "create_circle") == 0){
        return handle_create_circle(fd_s2c, saveptr);
    } else if (strcmp(cmd, "set_animation_params") == 0) {
        return handle_set_animation_params(fd_s2c, saveptr);
    } else if (strcmp(cmd, "create_sprite") == 0) {
        return handle_create_sprite(fd_s2c, saveptr);
    } else if (strcmp(cmd, "destroy_sprite") == 0) {
        return handle_destroy_sprite(fd_s2c, saveptr);
    } else if (strcmp(cmd, "place_sprite") == 0) {
        return handle_place_sprite(fd_s2c, saveptr);
    } else if (strcmp(cmd, "destroy_canvas")  == 0){
        return handle_destroy_canvas(fd_s2c, saveptr);
    } else if (strcmp(cmd, "placement_up") == 0) {
        return handle_placement_up(fd_s2c, saveptr);
    } else if (strcmp(cmd, "placement_down") == 0) {
        return handle_placement_down(fd_s2c, saveptr);
    } else if (strcmp(cmd, "placement_top") == 0) {
        return handle_placement_top(fd_s2c, saveptr);
    } else if (strcmp(cmd, "placement_bottom") == 0) {
        return handle_placement_bottom(fd_s2c, saveptr);
    } else if (strcmp(cmd, "destroy_placement") == 0) {
        return handle_destroy_placement(fd_s2c, saveptr);
    } else if (strcmp(cmd, "Disconnect") == 0){
        write(fd_s2c, "0\n", 2);
        return 1;
    }

    write(fd_s2c, "-1\n", 3);
    return 0;
}

int main(int argc, char** argv, char** envp) {

    int thread_pool_size = atoi(argv[1]);
    pid_t process_id = getpid();
    printf("Server PID: %ld\n", (long)process_id);
    fflush(stdout);

    struct sigaction sa;
    sa.sa_sigaction = handle_sigusr1;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    pthread_t* threads = malloc(sizeof(pthread_t) * thread_pool_size);
    for (int i = 0; i < thread_pool_size; i++){
        pthread_create(&threads[i], NULL, worker_thread_routine, NULL);
    }

    while (1){
        if (client_requested) {
            pid_t client_pid = pending_client_pid;
            client_requested = 0; // singal hanlder reset

            char fifo_c2s[64];
            char fifo_s2c[64];

            // format names of pipes with client PID
            sprintf(fifo_c2s, "FIFO_C2S_%ld", (long) client_pid);
            sprintf(fifo_s2c, "FIFO_S2C_%ld", (long) client_pid);

            unlink(fifo_c2s); // incase fifos already exist
            unlink(fifo_s2c);

            mkfifo(fifo_c2s, 0666);
            mkfifo(fifo_s2c, 0666);

            kill(client_pid, SIGUSR2);  // sends sigusr2 to client

            int fd_c2s = open(fifo_c2s, O_RDONLY | O_NONBLOCK);
            int fd_s2c = open(fifo_s2c, O_WRONLY);

            pthread_mutex_lock(&sessions_lock);
            active_sessions = realloc(active_sessions, sizeof(client_session_t) * (session_count + 1));
            active_sessions[session_count].client_pid = client_pid;
            active_sessions[session_count].fd_c2s = fd_c2s;
            active_sessions[session_count].fd_s2c = fd_s2c;
            active_sessions[session_count].active = 1;
            active_sessions[session_count].read_len = 0;
            active_sessions[session_count].next_ticket = 0;
            active_sessions[session_count].expected_ticket = 0;
            pthread_mutex_init(&active_sessions[session_count].order_lock, NULL);
            pthread_cond_init(&active_sessions[session_count].order_cond, NULL);
            session_count++;
            pthread_mutex_unlock(&sessions_lock);
        }

        pthread_mutex_lock(&sessions_lock);
        int total_monitored = 0;
        for (int i = 0; i < session_count; i++) {
            if (active_sessions[i].active) total_monitored++;
        }

        if (total_monitored == 0) {
            pthread_mutex_unlock(&sessions_lock);
            usleep(10000);  // if no clients are attached
            continue;
        }

        struct pollfd* fds = malloc(sizeof(struct pollfd) * total_monitored);
        int* session_indices = malloc(sizeof(int) * total_monitored);
        
        int current_idx = 0;
        for (int i = 0; i < session_count; i++) {
            if (active_sessions[i].active) {
                fds[current_idx].fd = active_sessions[i].fd_c2s;
                fds[current_idx].events = POLLIN;
                fds[current_idx].revents = 0;
                session_indices[current_idx] = i;
                current_idx++;
            }
        }
        pthread_mutex_unlock(&sessions_lock);
            
        int poll_result = poll(fds, total_monitored, 10);
        if (poll_result > 0) {
            for (int i = 0; i < total_monitored; i++) {
                if (fds[i].revents & POLLIN) {
                    pthread_mutex_lock(&sessions_lock);
                    client_session_t* session = &active_sessions[session_indices[i]];
                    
                    // read into buffer rihgt after any leftover parital data
                    size_t space_left = sizeof(session->read_buf) - session->read_len - 1;
                    ssize_t bytes_read = read(session->fd_c2s, session->read_buf + session->read_len, space_left);
                    
                    if (bytes_read > 0) {
                        session->read_len += bytes_read;
                        session->read_buf[session->read_len] = '\0';

                        // prcoess complete lines
                        char* line_start = session->read_buf;
                        char* newline_ptr;
                        
                        while ((newline_ptr = strchr(line_start, '\n')) != NULL) {
                            *newline_ptr = '\0'; // temporary string split
                            
                            if (strlen(line_start) > 0) {
                                char complete_cmd[512];
                                strncpy(complete_cmd, line_start, sizeof(complete_cmd) - 1);
                                complete_cmd[sizeof(complete_cmd) - 1] = '\0';
                                
                                uint32_t assigned_ticket = session->next_ticket++;
                                
                                queue_push(complete_cmd, session, assigned_ticket);
                            }
                            line_start = newline_ptr + 1;
                        }

                        size_t consumed_bytes = line_start - session->read_buf;
                        if (consumed_bytes > 0) {
                            memmove(session->read_buf, line_start, session->read_len - consumed_bytes);
                            session->read_len -= consumed_bytes;
                            session->read_buf[session->read_len] = '\0';
                        }
                    } else if (bytes_read == 0) {
                        session->active = 0;
                        close(session->fd_c2s);
                        close(session->fd_s2c);
                        char fifo_c2s[64], fifo_s2c[64];
                        sprintf(fifo_c2s, "FIFO_C2S_%ld", (long)session->client_pid);
                        sprintf(fifo_s2c, "FIFO_S2C_%ld", (long)session->client_pid);
                        unlink(fifo_c2s);
                        unlink(fifo_s2c);
                        pthread_mutex_destroy(&session->order_lock);
                        pthread_cond_destroy(&session->order_cond);
                    }
                    pthread_mutex_unlock(&sessions_lock);
                }
            }
            free(fds);
            free(session_indices);
        }
    }    
    return 0;
}
