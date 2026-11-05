#define _POSIX_C_SOURCE 200809L

#include "protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char response_fifo[MAX_FIFO_PATH];

static void cleanup(void)
{
    if (response_fifo[0] != '\0') {
        unlink(response_fifo);
    }
}

static void interrupted(int signal_number)
{
    (void)signal_number;
    cleanup();
    _exit(130);
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

static int read_all(int fd, void *buffer, size_t size)
{
    char *p = buffer;

    while (size > 0) {
        ssize_t n = read(fd, p, size);

        if (n == 0) {
            return -1;
        }
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

static int input_line(const char *prompt, char *buffer, size_t size)
{
    size_t length;
    int ch;

    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buffer, (int)size, stdin) == NULL) {
        return 0;
    }

    length = strlen(buffer);
    if (length > 0 && buffer[length - 1] == '\n') {
        buffer[length - 1] = '\0';
        return 1;
    }

    while ((ch = getchar()) != '\n' && ch != EOF) {
    }
    fprintf(stderr, "input is too long\n");
    return 0;
}

static int send_request(const Request *request, Response *response)
{
    int server_fd;
    int response_fd;

    server_fd = open(SERVER_FIFO, O_WRONLY | O_NONBLOCK);
    if (server_fd < 0) {
        perror("open server FIFO (is ./server running?)");
        return -1;
    }
    if (write_all(server_fd, request, sizeof(*request)) < 0) {
        perror("write request");
        close(server_fd);
        return -1;
    }
    close(server_fd);

    response_fd = open(response_fifo, O_RDONLY);
    if (response_fd < 0) {
        perror("open response FIFO");
        return -1;
    }
    if (read_all(response_fd, response, sizeof(*response)) < 0) {
        fprintf(stderr, "incomplete response\n");
        close(response_fd);
        return -1;
    }
    close(response_fd);
    return 0;
}

static void run_operation(char operation)
{
    Request request;
    Response response;
    char input[MAX_DATA + 2];

    memset(&request, 0, sizeof(request));
    request.magic = MAGIC;
    request.client_pid = (int32_t)getpid();
    request.operation = operation;
    snprintf(request.response_fifo, sizeof(request.response_fifo), "%s",
             response_fifo);

    if (!input_line("filename: ", request.filename,
                    sizeof(request.filename))
        || request.filename[0] == '\0') {
        fprintf(stderr, "filename is required\n");
        return;
    }

    if (operation == OP_READ) {
        char *end;
        long count;

        if (!input_line("bytes to read (0-256): ", input, sizeof(input))) {
            return;
        }
        errno = 0;
        count = strtol(input, &end, 10);
        if (errno != 0 || *input == '\0' || *end != '\0'
            || count < 0 || count > MAX_DATA) {
            fprintf(stderr, "invalid byte count\n");
            return;
        }
        request.byte_count = (uint32_t)count;
    } else {
        if (!input_line("text to write (max 256 bytes): ", input,
                        sizeof(input))) {
            return;
        }
        request.byte_count = (uint32_t)strlen(input);
        memcpy(request.data, input, request.byte_count);
    }

    if (send_request(&request, &response) < 0) {
        return;
    }
    if (response.magic != MAGIC) {
        fprintf(stderr, "invalid response\n");
    } else if (response.status != 0) {
        fprintf(stderr, "server error: %s\n", response.message);
    } else {
        printf("server: %s\n", response.message);
        if (operation == OP_READ) {
            fwrite(response.data, 1, response.byte_count, stdout);
            putchar('\n');
        }
    }
}

int main(void)
{
    char choice[8];
    struct sigaction action;

    snprintf(response_fifo, sizeof(response_fifo), "/tmp/osfs_%lu_%ld.fifo",
             (unsigned long)getuid(), (long)getpid());
    unlink(response_fifo);
    if (mkfifo(response_fifo, 0600) < 0) {
        perror("mkfifo");
        return EXIT_FAILURE;
    }

    atexit(cleanup);
    memset(&action, 0, sizeof(action));
    action.sa_handler = interrupted;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);

    printf("client started (PID %ld)\n", (long)getpid());
    while (input_line("\noperation [r/w/q]: ", choice, sizeof(choice))) {
        if (strcmp(choice, "q") == 0) {
            break;
        }
        if (strcmp(choice, "r") == 0 || strcmp(choice, "w") == 0) {
            run_operation(choice[0]);
        } else {
            fprintf(stderr, "enter r, w, or q\n");
        }
    }

    puts("client stopped");
    return EXIT_SUCCESS;
}
