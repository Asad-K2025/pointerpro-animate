
#include <animate/animate.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

volatile sig_atomic_t client_requested = 0;
volatile pid_t pending_client_pid = 0;

void handle_sigusr1(int singal_number, siginfo_t* info, void* context){
    pending_client_pid = info->si_pid;
    client_requested = 1;
}

void strip_newline(char* str){
    size_t len = strlen(str);
    if (len > 0 && str[len - 1] == '\n'){
        str[len - 1] = '\0';
    }
}

int process_rpc_command(int fd_s2c, char* command_line){
    // after processing, return 1 for should_disconnect, 0 otherwise
    strip_newline(command_line);

    char* saveptr;
    char* cmd = strtok_r(command_line, " ", &saveptr);

    if (cmd == NULL) {
        write(fd_s2c, "-1\n", 3);
        return 0;
    }

    if (strcmp(cmd, "Login") == 0){
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
    
    } else if (strcmp(cmd, "create_canvas") == 0) {
        char* width_str = strtok_r(NULL, " ", &saveptr);
        char* height_str = strtok_r(NULL, " ", &saveptr);
        char* color_str = strtok_r(NULL, " ", &saveptr);

        if (width_str == NULL || height_str == NULL || color_str == NULL) {
            write(fd_s2c, "-1\n", 3);
            return 0;
        }

        int width = atoi(width_str);
        int height = atoi(height_str);
        int color = atoi(color_str);

        if (width <= 0 || height <= 0 || color < 0) {
            write(fd_s2c, "-2\n", 3);
            return 0;
        }

        struct canvas* new_canvas = animate_create_canvas(width, height, color);

        if (new_canvas == NULL) {
            write(fd_s2c, "-3\n", 3);
            return 0;
        }

        uint64_t canvas_handle = (uint64_t)new_canvas;

        char response[128];
        sprintf(response, "0 %lu\n", canvas_handle);
        write(fd_s2c, response, strlen(response));
        return 0;

    } else if (strcmp(cmd, "Disconnect") == 0){
        write(fd_s2c, "0\n", 2);
        return 1;
    }
    // elifs for other commands
}

int main(int argc, char** argv, char** envp) {

    pid_t process_id = getpid();
    printf("Server PID: %ld\n", (long)process_id);

    struct sigaction sa;
    sa.sa_sigaction = handle_sigusr1;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    while (1){
        pause();

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

            int fd_c2s = open(fifo_c2s, O_RDONLY);
            int fd_s2c = open(fifo_s2c, O_WRONLY);

            char buffer[128];
            ssize_t bytes_read;

            // while used for continuous command handling
            while ((bytes_read = read(fd_c2s, buffer, sizeof(buffer) - 1)) > 0) {
                if (bytes_read > 0){
                    buffer[bytes_read] = '\0';
                }

                int should_disconnect = process_rpc_command(fd_s2c, buffer);
                if (should_disconnect){
                    break;
                }
            }
            
            // disconnection of client
            sleep(1);
            close(fd_c2s);
            close(fd_s2c);
            unlink(fifo_c2s);
            unlink(fifo_s2c);
        }
    }
    
    return 0;
}
