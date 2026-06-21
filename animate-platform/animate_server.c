#include "shared.h"

volatile sig_atomic_t client_requested = 0;
volatile pid_t pending_client_pid = 0;

client_session_t** active_sessions = NULL;
int session_count = 0;
pthread_mutex_t sessions_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t animate_lock = PTHREAD_MUTEX_INITIALIZER;

canvas_share_node_t* global_canvas_registry = NULL;  // global linked lists
sprite_node_t* global_sprite_registry = NULL;
placement_node_t* global_placement_registry = NULL;

task_queue_t work_queue = {NULL, 
                           NULL, 
                           PTHREAD_MUTEX_INITIALIZER, 
                           PTHREAD_COND_INITIALIZER};

void queue_push(const char* command, 
                client_session_t* session, 
                uint32_t ticket) {
    // adds tasks to the worker queue
    rpc_task_t* new_task = malloc(sizeof(rpc_task_t));
    if (new_task == NULL) {
        return;
    }

    strncpy(new_task->command_line, 
            command, 
            sizeof(new_task->command_line) - 1);
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
    // rmeove tasks form the worker queue
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
    // main wroker thread for client management
    while (1) {
        rpc_task_t* task = queue_pop();
        if (task != NULL) {
            char response[MAX_RPC_LINE];
            int disconnect_status = process_rpc_command(task->session, 
                                                        task->command_line, 
                                                        response);
            send_ordered_response(task->session, task->ticket, response);

            if (disconnect_status > 0) { // find and close the client
                pthread_mutex_lock(&sessions_lock);

                if (disconnect_status == 2){
                    sleep(1);  // 1 secodn wait for rejection message
                }

                disconnect_client(task->session);
                pthread_mutex_unlock(&sessions_lock);
            }
            free(task);
        }
    }
    return NULL;
}

void handle_sigusr1(int singal_number, siginfo_t* info, void* context){
    // hanlde singal for intial conenction with server
    pending_client_pid = info->si_pid;
    client_requested = 1;
}

int process_rpc_command(client_session_t* session, 
                        char* command_line, 
                        char* response_out){
    // call necessary handler for each RPC fucntion call
    // after processing, return 1 for should_disconnect, 0 otherwise
    size_t len = strlen(command_line);
    if (len > 0 && command_line[len - 1] == '\n'){
        command_line[len - 1] = '\0';
    }

    char* saveptr;
    char* cmd = strtok_r(command_line, " ", &saveptr);

    if (cmd == NULL) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    // forbidden function calls
    if (strcmp(cmd, "set_animation_function") == 0 ||
        strcmp(cmd, "frame_size_bytes") == 0 ||
        strcmp(cmd, "generate_frame") == 0) {
        strcpy(response_out, "-1\n");
        return 0;
    }

    if (strcmp(cmd, "Login") == 0){
        int login_result = handle_login(response_out, session, saveptr);

        if (login_result == 1){
            return 2;  // sleep disconnect status as login rejected
        }
        return 0;
    } else if (strcmp(cmd, "create_canvas") == 0) {
        return handle_create_canvas(response_out, session, saveptr);
    } else if (strcmp(cmd, "create_rectangle") == 0){
        return handle_create_rectangle(response_out, session, saveptr);
    } else if (strcmp(cmd, "create_circle") == 0){
        return handle_create_circle(response_out, session, saveptr);
    } else if (strcmp(cmd, "set_animation_params") == 0) {
        return handle_set_animation_params(response_out, session, saveptr);
    } else if (strcmp(cmd, "create_sprite") == 0) {
        return handle_create_sprite(response_out, session, saveptr);
    } else if (strcmp(cmd, "destroy_sprite") == 0) {
        return handle_destroy_sprite(response_out, session, saveptr);
    } else if (strcmp(cmd, "place_sprite") == 0) {
        return handle_place_sprite(response_out, session, saveptr);
    } else if (strcmp(cmd, "destroy_canvas")  == 0){
        return handle_destroy_canvas(response_out, session, saveptr);
    } else if (strcmp(cmd, "placement_up") == 0) {
        return handle_placement_up(response_out, session, saveptr);
    } else if (strcmp(cmd, "placement_down") == 0) {
        return handle_placement_down(response_out, session, saveptr);
    } else if (strcmp(cmd, "placement_top") == 0) {
        return handle_placement_top(response_out, session, saveptr);
    } else if (strcmp(cmd, "placement_bottom") == 0) {
        return handle_placement_bottom(response_out, session, saveptr);
    } else if (strcmp(cmd, "destroy_placement") == 0) {
        return handle_destroy_placement(response_out, session, saveptr);
    } else if (strcmp(cmd, "generate") == 0) {
        return handle_generate(response_out, session, saveptr);
    } else if (strcmp(cmd, "share_canvas") == 0){
        return handle_share_canvas(response_out, session, saveptr);
    } else if (strcmp(cmd, "barrier") == 0) {
        return handle_barrier(response_out, session, saveptr);
    } else if (strcmp(cmd, "Disconnect") == 0){
        strcpy(response_out, "0\n");
        return 1;
    }

    strcpy(response_out, "-1\n");
    return 0;
}

int main(int argc, char** argv, char** envp) {

    if (argc != 2){
        return 1;
    }

    int thread_pool_size = atoi(argv[1]);

    if (thread_pool_size < 1){
        return 1;
    }

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
        if (pthread_create(&threads[i], 
                           NULL, 
                           worker_thread_routine, 
                           NULL) != 0){
            return 1;  // error occured with creating threads
        }
    }

    while (1){
        if (client_requested) {
            pid_t client_pid = pending_client_pid;
            client_requested = 0; // singal hanlder reset

            char fifo_c2s[FIFO_NAME_LEN];
            char fifo_s2c[FIFO_NAME_LEN];

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
            client_session_t** resized = realloc(
                active_sessions,
                sizeof(client_session_t*) * (session_count + 1)
            );

            if (resized == NULL) {
                close(fd_c2s);
                close(fd_s2c);
                continue;
            }

            active_sessions = resized;
            // check whether clients still exist preiodically
            for (int i = 0; i < session_count; i++) {  
                client_session_t* session = active_sessions[i];

                if (!session->active) {
                    continue;
                }

                if (kill(session->client_pid, 0) == -1 && errno == ESRCH) {
                    disconnect_client(session);
                }
            }
            client_session_t* new_session = malloc(sizeof(client_session_t));

            if (new_session == NULL) {
                close(fd_c2s);
                close(fd_s2c);
                continue;
            }

            new_session->client_pid = client_pid;
            new_session->fd_c2s = fd_c2s;
            new_session->fd_s2c = fd_s2c;
            new_session->active = 1;
            new_session->read_len = 0;
            new_session->next_ticket = 0;
            new_session->pending_responses = NULL;
            new_session->pending_capacity = 0;
            new_session->next_response_ticket = 0;

            pthread_mutex_init(&new_session->response_lock, NULL);

            active_sessions[session_count] = new_session;
            session_count++;
            pthread_mutex_unlock(&sessions_lock);
        }

        pthread_mutex_lock(&sessions_lock);
        int total_monitored = 0;
        for (int i = 0; i < session_count; i++) {
            if (active_sessions[i]->active) total_monitored++;
        }

        if (total_monitored == 0) {
            pthread_mutex_unlock(&sessions_lock);
            usleep(10000);  // if no clients are attached
            continue;
        }

        struct pollfd* fds = malloc(sizeof(struct pollfd) * total_monitored);
        int* session_indices = malloc(sizeof(int) * total_monitored);
        if (fds == NULL || session_indices == NULL) {
            free(fds);
            free(session_indices);
            pthread_mutex_unlock(&sessions_lock);
            continue;
        }

        int current_idx = 0;
        for (int i = 0; i < session_count; i++) {
            if (active_sessions[i]->active) {
                fds[current_idx].fd = active_sessions[i]->fd_c2s;
                fds[current_idx].events = POLLIN;
                fds[current_idx].revents = 0;
                session_indices[current_idx] = i;
                current_idx++;
            }
        }
        pthread_mutex_unlock(&sessions_lock);
            
        int poll_result = poll(fds, total_monitored, 10);
        cleanup_inactive_sessions();
        if (poll_result > 0) {
            for (int i = 0; i < total_monitored; i++) {
                if (fds[i].revents & POLLIN) {
                    pthread_mutex_lock(&sessions_lock);
                    client_session_t* session = 
                        active_sessions[session_indices[i]];
                    pthread_mutex_unlock(&sessions_lock);
                    
                    // read into buffer rihgt after any leftover parital data
                    size_t space_left = 
                        sizeof(session->read_buf) - session->read_len - 1;
                    ssize_t bytes_read = 
                        read(session->fd_c2s, 
                             session->read_buf + session->read_len, 
                             space_left);
                    
                    if (bytes_read > 0) {
                        session->read_len += bytes_read;
                        session->read_buf[session->read_len] = '\0';

                        // prcoess complete lines
                        char* line_start = session->read_buf;
                        char* newline_ptr;
                        
                        while ((newline_ptr = 
                                strchr(line_start, '\n')) != NULL) {
                            *newline_ptr = '\0'; // temporary string split
                            
                            if (strlen(line_start) > 0) {
                                char complete_cmd[MAX_RPC_LINE];
                                strncpy(complete_cmd, line_start, 
                                        sizeof(complete_cmd) - 1);
                                complete_cmd[sizeof(complete_cmd) - 1] = '\0';
                                
                                uint32_t assigned_ticket = 
                                    session->next_ticket++;
                                
                                queue_push(complete_cmd, 
                                           session, 
                                           assigned_ticket);
                            }
                            line_start = newline_ptr + 1;
                        }

                        size_t consumed_bytes = line_start - session->read_buf;
                        if (consumed_bytes > 0) {
                            memmove(session->read_buf, 
                                    line_start, 
                                    session->read_len - consumed_bytes);
                            session->read_len -= consumed_bytes;
                            session->read_buf[session->read_len] = '\0';
                        }
                    } else if (bytes_read == 0) {
                        disconnect_client(session);
                    }
                }
            }
        }
        free(fds);
        free(session_indices);
    }
    free(threads);    
    return 0;
}
