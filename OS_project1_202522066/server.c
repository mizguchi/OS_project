#define _POSIX_C_SOURCE 200809L

#include "protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;

static void stop_server(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static void reap_children(int signal_number)
{
    int saved_errno = errno;

    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    errno = saved_errno;
}

static int write_all(int fd, const void *buffer, size_t size)
{
    const char *p = buffer;

    while (size > 0) {
        ssize_t n = write(fd, p, size);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static int valid_request(const Request *request)
{
    return request->magic == MAGIC
        && request->client_pid > 0
        && (request->operation == OP_READ || request->operation == OP_WRITE)
        && request->byte_count <= MAX_DATA
        && memchr(request->filename, '\0', sizeof(request->filename)) != NULL
        && request->filename[0] != '\0'
        && memchr(request->response_fifo, '\0',
                  sizeof(request->response_fifo)) != NULL
        && strncmp(request->response_fifo, "/tmp/osfs_", 10) == 0;
}

static void set_error(Response *response, const char *action)
{
    response->status = errno;
    snprintf(response->message, sizeof(response->message), "%s: %s",
             action, strerror(errno));
}

static void read_file(const Request *request, Response *response)
{
    size_t total = 0;
    int fd = open(request->filename, O_RDONLY);

    if (fd < 0) {
        set_error(response, "open");
        return;
    }

    while (total < request->byte_count) {
        ssize_t n = read(fd, response->data + total,
                         request->byte_count - total);

        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(response, "read");
            close(fd);
            return;
        }
        total += (size_t)n;
    }

    close(fd);
    response->byte_count = (uint32_t)total;
    snprintf(response->message, sizeof(response->message),
             "read completed: %zu bytes", total);
}

static void write_file(const Request *request, Response *response)
{
    int fd = open(request->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        set_error(response, "open");
        return;
    }
    if (write_all(fd, request->data, request->byte_count) < 0) {
        set_error(response, "write");
        close(fd);
        return;
    }
    if (close(fd) < 0) {
        set_error(response, "close");
        return;
    }

    snprintf(response->message, sizeof(response->message),
             "write completed: %u bytes", request->byte_count);
}

static void handle_request(const Request *request)
{
    Response response;
    int response_fd;

    memset(&response, 0, sizeof(response));
    response.magic = MAGIC;

    if (request->operation == OP_READ) {
        read_file(request, &response);
    } else {
        write_file(request, &response);
    }

    response_fd = open(request->response_fifo, O_WRONLY);
    if (response_fd < 0) {
        perror("open response FIFO");
        return;
    }
    if (write_all(response_fd, &response, sizeof(response)) < 0) {
        perror("write response");
    }
    close(response_fd);
}

static int install_signals(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_server;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0
        || sigaction(SIGTERM, &action, NULL) < 0) {
        return -1;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = reap_children;
    action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGCHLD, &action, NULL) < 0) {
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);
    return 0;
}

int main(void)
{
    int server_fd;

    if (install_signals() < 0) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    unlink(SERVER_FIFO);
    if (mkfifo(SERVER_FIFO, 0660) < 0) {
        perror("mkfifo");
        return EXIT_FAILURE;
    }

    /* O_RDWR prevents EOF when no client currently has the FIFO open. */
    server_fd = open(SERVER_FIFO, O_RDWR);
    if (server_fd < 0) {
        perror("open server FIFO");
        unlink(SERVER_FIFO);
        return EXIT_FAILURE;
    }

    printf("server started: %s (PID %ld)\n", SERVER_FIFO, (long)getpid());
    fflush(stdout);

    while (running) {
        Request request;
        ssize_t n = read(server_fd, &request, sizeof(request));

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read request");
            break;
        }
        if (n != (ssize_t)sizeof(request) || !valid_request(&request)) {
            fprintf(stderr, "invalid request ignored\n");
            continue;
        }

        {
            pid_t child = fork();

            if (child < 0) {
                perror("fork");
                continue;
            }
            if (child == 0) {
                close(server_fd);
                handle_request(&request);
                _exit(EXIT_SUCCESS);
            }

            printf("client %d -> child %ld\n", request.client_pid, (long)child);
            fflush(stdout);
        }
    }

    close(server_fd);
    unlink(SERVER_FIFO);
    puts("server stopped");
    return EXIT_SUCCESS;
}
