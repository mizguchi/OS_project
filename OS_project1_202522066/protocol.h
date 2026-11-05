#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define SERVER_FIFO "/tmp/os_file_server.fifo"
#define MAGIC 0x4f534653u

#define MAX_FILENAME 128
#define MAX_DATA 256
#define MAX_MESSAGE 128
#define MAX_FIFO_PATH 64

#define OP_READ 'r'
#define OP_WRITE 'w'

typedef struct {
    uint32_t magic;
    int32_t client_pid;
    char operation;
    char padding[3];
    uint32_t byte_count;
    char filename[MAX_FILENAME];
    char data[MAX_DATA];
    char response_fifo[MAX_FIFO_PATH];
} Request;

typedef struct {
    uint32_t magic;
    int32_t status;
    uint32_t byte_count;
    char data[MAX_DATA];
    char message[MAX_MESSAGE];
} Response;

/* POSIX guarantees atomic FIFO writes up to at least 512 bytes. */
_Static_assert(sizeof(Request) <= 512, "Request must be written atomically");
_Static_assert(sizeof(Response) <= 512, "Response must be written atomically");

#endif
