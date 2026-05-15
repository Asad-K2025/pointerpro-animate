#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

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

    printf("Connected\n");

    close(fd_c2s);
    close(fd_s2c);

    return 0;
}
