#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>

volatile sig_atomic_t recieved_signal = 0;

void handle_sigusr2(int signal_number){
    recieved_signal = 1;
}

int main(int argc, char** argv, char** envp) {
    pid_t server_pid = (pid_t) atoi(argv[1]);

    struct sigaction sa;
    sa.sa_handler = handle_sigusr2;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR2, &sa, NULL);

    kill(server_pid, SIGUSR1);

    while (!recieved_signal) {
        pause();
    }

    char fifo_c2s[64];
    char fifo_s2c[64];

    sprintf(fifo_c2s, "FIFO_C2S_%ld", (long) getpid());
    sprintf(fifo_s2c, "FIFO_S2C_%ld", (long) getpid());

    int fd_c2s = open(fifo_c2s, O_WRONLY);
    int fd_s2c = open(fifo_s2c, O_RDONLY);

    int logged_in = 0;
    char input_buffer[100];

    while (fgets(input_buffer, sizeof(input_buffer), stdin)){

        size_t len = strlen(input_buffer);
        if (len > 0 && input_buffer[len - 1] == '\n'){
            input_buffer[len - 1] = '\0';
        }

        if (strlen(input_buffer) == 0){
            continue;
        }

        if (!logged_in && strncmp(input_buffer, "Login ", 6) != 0){
            printf("Not logged in\n");
            fflush(stdout);
            continue;
        }

        char send_buffer[128];

        snprintf(send_buffer, sizeof(send_buffer), "%s\n", input_buffer);
        write(fd_c2s, send_buffer, strlen(send_buffer));

        // check disconnect after the above if statement to first handle server repsonse
        if (strncmp(input_buffer, "Disconnect", 10) == 0) {
            logged_in = 0;

            close(fd_c2s);
            close(fd_s2c);
            return 0;
        } 

        char response_buffer[128];
        ssize_t bytes_read = read(fd_s2c, response_buffer, sizeof(response_buffer));
        
        if (bytes_read <= 0) {
            break;  // server disconnected or closed pipe
        }
        
        response_buffer[bytes_read] = '\0';

        if (bytes_read > 0 && response_buffer[bytes_read - 1] == '\n') {
            response_buffer[bytes_read - 1] = '\0';
        }

        if (strncmp(input_buffer, "Login", 5) == 0){
            if (strncmp(response_buffer, "Reject ", 7) == 0){
                response_buffer[strcspn(response_buffer, "\n")] = '\0';
                printf("%s\n", response_buffer);
                fflush(stdout);
                break;
            } else {
                char username[64];
                sscanf(input_buffer, "Login %s", username);
                int balance = atoi(response_buffer);
                printf("Welcome %s. Your balance is %d\n", username, balance);
                fflush(stdout);
                logged_in = 1;
            }

        } else {
            if (strcmp(response_buffer, "-1") == 0) {
                printf("RPC Failed\n");
            } else if (strcmp(response_buffer, "-2") == 0) {
                printf("Value error\n");
            } else if (strcmp(response_buffer, "-3") == 0) {
                printf("Internal error\n");
            } else if (strncmp(response_buffer, "0 ", 2) == 0) { // writign to file calls
                printf("Success %s\n", response_buffer + 2);
            } else if (strcmp(response_buffer, "0") == 0) {
                printf("Success\n");
            } else {
                printf("%s\n", response_buffer);
            }
            fflush(stdout);

        } 
    }
    
    close(fd_c2s);
    close(fd_s2c);

    return 0;
}
