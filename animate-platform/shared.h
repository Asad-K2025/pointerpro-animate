// Contains imports, macros and strcut definitions

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
#include <errno.h>

#define MAX_USERNAME_LEN 32
#define MAX_RPC_LINE 512
#define FIFO_NAME_LEN 64
#define RESPONSE_LEN 128

typedef struct {  // tracks every authenticated and active client
    pid_t client_pid;
    int fd_c2s;
    int fd_s2c;
    char username[MAX_USERNAME_LEN + 1];
    int active;

    char read_buf[MAX_RPC_LINE];  // partial command buffering for FIFO
    size_t read_len;

    // tickets ensure order of requests for each client
    uint32_t next_ticket;
    char** pending_responses;
    size_t pending_capacity;
    uint32_t next_response_ticket;
    pthread_mutex_t response_lock;
} client_session_t;

typedef struct rpc_task {  // thread pool task structure
    char command_line[MAX_RPC_LINE];
    client_session_t* session;
    uint32_t ticket;
    struct rpc_task* next;
} rpc_task_t;

typedef struct { // queue used by worker threads
    rpc_task_t* head;
    rpc_task_t* tail;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} task_queue_t;

struct canvas {  // define struct to access it's width and height
    size_t width;
    size_t height;
    color_t background_color;

    struct sprite_placement* head;
    struct sprite_placement* tail;
};

typedef struct canvas_share_node {  // store data to share canvases
    uint64_t canvas_handle;
    char owner_username[MAX_USERNAME_LEN + 1];
    
    char** shared_usernames;
    int shared_count;
    int shared_capacity;

    int ref_count;

    int barrier_arrived_count;
    pthread_cond_t barrier_cond;

    struct canvas_share_node* next;
} canvas_share_node_t;

typedef struct sprite_node {
    uint64_t sprite_handle;
    char owner_username[MAX_USERNAME_LEN + 1];
    struct sprite_node* next;
} sprite_node_t;

typedef struct placement_node {
    uint64_t placement_handle;
    uint64_t canvas_handle;
    struct placement_node* next;
} placement_node_t;

int process_rpc_command(client_session_t* session, 
                        char* command_line, 
                        char* response_out);

int handle_login(char* response_out,
                 client_session_t* session,
                 char* saveptr);

int handle_create_canvas(char* response_out,
                         client_session_t* session,
                         char* saveptr);

int handle_create_rectangle(char* response_out,
                            client_session_t* session,
                            char* saveptr);

int handle_create_circle(char* response_out,
                         client_session_t* session,
                         char* saveptr);

int handle_set_animation_params(char* response_out,
                                client_session_t* session,
                                char* saveptr);

int handle_create_sprite(char* response_out,
                         client_session_t* session,
                         char* saveptr);

int handle_destroy_sprite(char* response_out,
                          client_session_t* session,
                          char* saveptr);

int handle_place_sprite(char* response_out,
                        client_session_t* session,
                        char* saveptr);

int handle_destroy_canvas(char* response_out,
                          client_session_t* session,
                          char* saveptr);

int handle_placement_up(char* response_out,
                        client_session_t* session,
                        char* saveptr);

int handle_placement_down(char* response_out,
                          client_session_t* session,
                          char* saveptr);

int handle_placement_top(char* response_out,
                         client_session_t* session,
                         char* saveptr);

int handle_placement_bottom(char* response_out,
                            client_session_t* session,
                            char* saveptr);

int handle_destroy_placement(char* response_out,
                             client_session_t* session,
                             char* saveptr);

int handle_generate(char* response_out,
                    client_session_t* session,
                    char* saveptr);

int handle_share_canvas(char* response_out,
                        client_session_t* session,
                        char* saveptr);

int handle_barrier(char* response_out,
                   client_session_t* session,
                   char* saveptr);

void cleanup_inactive_sessions(void);

void disconnect_client(client_session_t* session);

void send_ordered_response(client_session_t* session,
                           uint32_t ticket,
                           const char* response);

void canvas_node_delete(uint64_t canvas_address);

int validate_placement_access(uint64_t placement_handle,
                              const char* username);

int validate_sprite_access(uint64_t sprite_handle,
                           const char* username);

canvas_share_node_t* find_canvas_node(uint64_t canvas_handle);

sprite_node_t* find_sprite_node(uint64_t sprite_handle);

int client_has_canvas_access(canvas_share_node_t* node,
                             const char* username);

int valid_authorised_username(const char* username);

extern client_session_t** active_sessions;
extern int session_count;

extern canvas_share_node_t* global_canvas_registry;
extern sprite_node_t* global_sprite_registry;
extern placement_node_t* global_placement_registry;

extern pthread_mutex_t sessions_lock;
extern pthread_mutex_t registry_lock;
extern pthread_mutex_t animate_lock;

extern task_queue_t work_queue;
