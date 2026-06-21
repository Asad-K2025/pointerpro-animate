// Contains client/authentication related logic and managing clients 

#include "shared.h"

void cleanup_inactive_sessions(void) {
    // disconnect inactive client resources
    pthread_mutex_lock(&sessions_lock);

    int write_idx = 0;
    for (int i = 0; i < session_count; i++) {
        client_session_t* session = active_sessions[i];
        if (!session->active) {
            for (size_t j = 0; j < session->pending_capacity; j++) {
                free(session->pending_responses[j]);
            }

            free(session->pending_responses);
            pthread_mutex_destroy(&session->response_lock);
            free(session);
        } else {
            active_sessions[write_idx++] = active_sessions[i];
        }
    }

    session_count = write_idx;
    pthread_mutex_unlock(&sessions_lock);
}

void disconnect_client(client_session_t* session) {
    if (session == NULL || !session->active) {
        return;
    }

    session->active = 0;

    if (session->fd_c2s < 0){  // client already closed
        return;
    }

    close(session->fd_c2s);
    close(session->fd_s2c);

    char fifo_c2s[FIFO_NAME_LEN];
    char fifo_s2c[FIFO_NAME_LEN];

    sprintf(fifo_c2s, "FIFO_C2S_%ld", (long)session->client_pid);
    sprintf(fifo_s2c, "FIFO_S2C_%ld", (long)session->client_pid);

    unlink(fifo_c2s);
    unlink(fifo_s2c);
}

void send_ordered_response(client_session_t* session, 
                           uint32_t ticket, 
                           const char* response) {
    // ensure repsonses are sent in order they arrived within clients
    pthread_mutex_lock(&session->response_lock);

    if (ticket >= session->pending_capacity) {
        size_t new_capacity = ticket + 8;
        char** new_pending = realloc(session->pending_responses,
                                     sizeof(char*) * new_capacity);

        if (new_pending == NULL) {
            pthread_mutex_unlock(&session->response_lock);
            return;
        }

        for (size_t i = session->pending_capacity; i < new_capacity; i++) {
            new_pending[i] = NULL;
        }

        session->pending_responses = new_pending;
        session->pending_capacity = new_capacity;
    }

    session->pending_responses[ticket] = strdup(response);

    while (session->next_response_ticket < session->pending_capacity &&
           session->pending_responses[session->next_response_ticket] != NULL) {

        char* current =
            session->pending_responses[session->next_response_ticket];

        write(session->fd_s2c, current, strlen(current));
        free(current);
        session->pending_responses[session->next_response_ticket] = NULL;
        session->next_response_ticket++;
    }

    pthread_mutex_unlock(&session->response_lock);
}

int valid_authorised_username(const char* username) {
    FILE* file = fopen("users.txt", "r");

    if (file == NULL) {
        return 0;
    }

    char line[256];

    while (fgets(line, sizeof(line), file)) {
        char current_user[FIFO_NAME_LEN];
        int balance;

        if (sscanf(line, "%63s %d", current_user, &balance) == 2) {
            if (strcmp(current_user, username) == 0) {
                fclose(file);

                if (balance > 0) {
                    return 1;
                }

                return 0;
            }
        }
    }

    fclose(file);
    return 0;
}