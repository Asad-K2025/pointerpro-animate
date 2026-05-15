
#include <animate/animate.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

volatile sig_atomic_t client_requested = 0;
volatile pid_t pending_client_pid = 0;

void handle_sigusr1(int singal_number, siginfo_t* info, void* context){
    pending_client_pid = info->si_pid;
    client_requested = 1;
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

            ssize_t bytes_read = read(fd_c2s, buffer, sizeof(buffer) - 1);

            buffer[bytes_read] = '\0';

            char username[64];
            sscanf(buffer, "Login %s", username);

            FILE* file = fopen("users.txt", "r");
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
            int should_disconnect = 0;

            if (found) {
                if (balance > 0) {
                    sprintf(response, "%d\n", balance);
                    write(fd_s2c, response, strlen(response));
                } else {
                    sprintf(response, "Reject BALANCE\n");
                    write(fd_s2c, response, strlen(response));
                    should_disconnect = 1;
                }
            } else {
                sprintf(response, "Reject UNAUTHORISED\n");
                write(fd_s2c, response, strlen(response));
                should_disconnect = 1;
            }

            if (should_disconnect){
                sleep(1);
                close(fd_c2s);
                close(fd_s2c);
                unlink(fifo_c2s);
                unlink(fifo_s2c);
            }
        }
    }

    struct canvas* canvas = animate_create_canvas(100,100,0);
    animate_destroy_canvas(canvas);
    
    return 0;
}
