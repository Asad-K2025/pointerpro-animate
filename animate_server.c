
#include <animate/animate.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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

            char fifo_c2s[64];
            char fifo_s2c[64];

            // format names of pipes with client PID
            sprintf(fifo_c2s, "FIFO_C2S_%ld", (long) client_pid);
            sprintf(fifo_s2c, "FIFO_S2C_%ld", (long) client_pid);

            unlink(fifo_c2s); // incase fifos exist
            unlink(fifo_s2c);

            mkfifo(fifo_c2s, 0666);
            mkfifo(fifo_s2c, 0666);

            kill(client_pid, SIGUSR2);  // sends sigusr2 to client

            int fd_c2s = open(fifo_c2s, O_RDONLY);
            int fd_s2c = open(fifo_s2c, O_WRONLY);
        }
    }

    struct canvas* canvas = animate_create_canvas(100,100,0);
    animate_destroy_canvas(canvas);
    
    return 0;
}
