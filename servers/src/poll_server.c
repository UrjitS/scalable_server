#include "server.h"
#include "util.h"
#include <arpa/inet.h>
#include <dc_c/dc_stdio.h>
#include <dc_c/dc_stdlib.h>
#include <dc_c/dc_string.h>
#include <dc_env/env.h>
#include <dc_error/error.h>
#include <dc_posix/arpa/dc_inet.h>
#include <dc_posix/dc_poll.h>
#include <dc_posix/dc_unistd.h>
#include <dc_posix/dc_semaphore.h>
#include <dc_posix/dc_signal.h>
#include <dc_posix/dc_string.h>
#include <dc_posix/sys/dc_socket.h>
#include <dc_posix/sys/dc_wait.h>
#include <dc_util/io.h>
#include <dc_util/networking.h>
#include <dc_util/system.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/poll.h>
#include <sys/semaphore.h>

#define VERBOSE_SERVER false
#define VERBOSE_HANDLER false
#define DEBUG_SERVER false
#define DEBUG_HANDLER false

typedef ssize_t (*read_message_func)(const struct dc_env *env, struct dc_error *err, uint8_t **raw_data, int client_socket);
typedef size_t (*process_message_func)(const struct dc_env *env, struct dc_error *err, const uint8_t *raw_data, uint8_t **processed_data, ssize_t count);
typedef void (*send_message_func)(const struct dc_env *env, struct dc_error *err, uint8_t *buffer, size_t count, int client_socket, bool *closed);

struct server_info
{
    sem_t *domain_sem;
    int domain_socket;
    int pipe_fd;
    int num_workers;
    pid_t *workers;
    int listening_socket;
    int num_fds;
    struct pollfd *poll_fds;
};

struct worker_info
{
    sem_t *select_sem;
    sem_t *domain_sem;
    int domain_socket;
    int pipe_fd;
};

struct revive_message
{
    int fd;
    bool closed;
};

static void sigint_handler(int signal);
static bool create_workers(struct dc_env *env, struct dc_error *err, const uint8_t *jobs, pid_t *workers, sem_t *select_sem, sem_t *domain_sem, const int domain_sockets[2], const int pipe_fds[2]);
static void initialize_server(const struct dc_env *env, struct dc_error *err, struct server_info *server, const uint8_t *jobs, sem_t *domain_sem, int domain_socket, int pipe_fd, pid_t *workers, struct options *opts);
static void destroy_server(const struct dc_env *env, struct dc_error *err, struct server_info *server);
static void run_server(const struct dc_env *env, struct dc_error *err, struct options *opts, struct server_info *server);
static void server_loop(const struct dc_env *env, struct dc_error *err, struct options *opts, struct server_info *server);
static bool handle_change(const struct dc_env *env, struct dc_error *err, struct options *opts, struct server_info *server, struct pollfd *poll_fd);
static void accept_connection(const struct dc_env *env, struct dc_error *err, struct server_info *server);
static void write_socket_to_domain_socket(const struct dc_env *env, struct dc_error *err, const struct server_info *server, int client_socket);
static void revive_socket(const struct dc_env *env, struct dc_error *err, const struct server_info *server, struct revive_message *message);
static void close_connection(const struct dc_env *env, struct dc_error *err, struct server_info *server, int client_socket);
static void wait_for_workers(const struct dc_env *env, struct dc_error *err, struct server_info *server);
static void worker_process(struct dc_env *env, struct dc_error *err, struct worker_info *worker);
static int handle_client_data(struct dc_env *env, struct dc_error *err, int clients);
static bool extract_message_parameters(const struct dc_env *env, struct dc_error *err, struct worker_info *worker, int *client_socket, int *value);
static void process_message(const struct dc_env *env, struct dc_error *err, struct worker_info *worker);
static void send_revive(const struct dc_env *env, struct dc_error *err, struct worker_info *worker, int client_socket, int fd, bool closed);
static void print_fd(const struct dc_env *env, const char *message, int fd, bool display);
static void print_socket(const struct dc_env *env, struct dc_error *err, const char *message, int socket, bool display);

static const int DEFAULT_N_PROCESSES = 2;
static volatile sig_atomic_t running;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

int run_poll_server(struct dc_env * env, struct dc_error * error, struct options *opts) {
    DC_TRACE(env);
    printf("Poll process pool server on %s\n", opts->ip_address);

    sem_t *select_sem;
    sem_t *domain_sem;
    int domain_sockets[2];
    int pipe_fds[2];
    pid_t *workers;
    bool is_server;
    pid_t pid;
    char domain_sem_name[100];  // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    char select_sem_name[100];  // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    uint8_t jobs = dc_get_number_of_processors(env, error, DEFAULT_N_PROCESSES);

    socketpair(AF_UNIX, SOCK_DGRAM, 0, domain_sockets);
    dc_pipe(env, error, pipe_fds);
    printf("Starting server (%d)\n", getpid());
    workers = NULL;
    pid = getpid();
    sprintf(domain_sem_name, "/sem-%d-domain", pid);    // NOLINT(cert-err33-c)
    sprintf(select_sem_name, "/sem-%d-select", pid);    // NOLINT(cert-err33-c)
    select_sem = sem_open(select_sem_name, O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, 1);
    domain_sem = sem_open(domain_sem_name, O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH, 1);
    workers = (pid_t *)dc_malloc(env, error, jobs * sizeof(pid_t));
    is_server = create_workers(env, error, &jobs, workers, select_sem, domain_sem, domain_sockets, pipe_fds);

    if(is_server)
    {
        struct sigaction act;
        struct server_info server;

        act.sa_handler = sigint_handler;
        dc_sigemptyset(env, error, &act.sa_mask);
        act.sa_flags = 0;
        dc_sigaction(env, error, SIGINT, &act, NULL);
        dc_close(env, error, domain_sockets[0]);
        dc_close(env, error, pipe_fds[1]);
        dc_memset(env, &server, 0, sizeof(server));
        initialize_server(env, error, &server, &jobs, domain_sem, domain_sockets[1], pipe_fds[0], workers, opts);
        run_server(env, error, opts, &server);
        destroy_server(env, error, &server);

        sem_close(domain_sem);
        sem_close(select_sem);

        if(is_server)
        {
            sem_unlink(domain_sem_name);
            sem_unlink(select_sem_name);
        }
    }

    printf("Exiting %d\n", getpid());
    free(env);
    dc_error_reset(error);
    free(error);
    return 0;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
static void sigint_handler(int signal)
{
    running = 0;
}
#pragma GCC diagnostic pop


static bool create_workers(struct dc_env *env, struct dc_error *err, const uint8_t *jobs, pid_t *workers, sem_t *select_sem, sem_t *domain_sem, const int domain_sockets[2], const int pipe_fds[2])
{
    DC_TRACE(env);

    for(int i = 0; i < *jobs; i++)
    {
        pid_t pid;

        pid = dc_fork(env, err);

        if(pid == 0)
        {
            struct sigaction act;
            struct worker_info worker;

            act.sa_handler = sigint_handler;
            dc_sigemptyset(env, err, &act.sa_mask);
            act.sa_flags = 0;
            dc_sigaction(env, err, SIGINT, &act, NULL);
            dc_free(env, workers);
            dc_close(env, err, domain_sockets[1]);
            dc_close(env, err, pipe_fds[0]);

            if(dc_error_has_no_error(err))
            {
                worker.select_sem = select_sem;
                worker.domain_sem = domain_sem;
                worker.domain_socket = domain_sockets[0];
                worker.pipe_fd = pipe_fds[1];
                worker_process(env, err, &worker);
            }

            return false;
        }

        workers[i] = pid;
    }

    return true;
}

static void initialize_server(const struct dc_env *env, struct dc_error *err, struct server_info *server, const uint8_t *jobs, sem_t *domain_sem, int domain_socket, int pipe_fd, pid_t *workers, struct options *opts)
{
    static int optval = 1;
    struct sockaddr_in server_address;

    DC_TRACE(env);
    server->domain_sem = domain_sem;
    server->domain_socket = domain_socket;
    server->pipe_fd = pipe_fd;
    server->num_workers = *jobs;
    server->workers = workers;
    server->listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    dc_memset(env, &server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = dc_inet_addr(env, err, opts->ip_address);
    server_address.sin_port = dc_htons(env, opts->port_out);
    dc_setsockopt(env, err, server->listening_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    dc_bind(env, err, server->listening_socket, (struct sockaddr *)&server_address, sizeof(server_address));
    dc_listen(env, err, server->listening_socket, BACKLOG);
    server->poll_fds = (struct pollfd *)dc_malloc(env, err, sizeof(struct pollfd) * 2);
    server->poll_fds[0].fd = server->listening_socket;
    server->poll_fds[0].events = POLLIN;
    server->poll_fds[1].fd = server->pipe_fd;
    server->poll_fds[1].events = POLLIN;
    server->num_fds = 2;
}

static void destroy_server(const struct dc_env *env, struct dc_error *err, struct server_info *server)
{
    if(server->poll_fds)
    {
        dc_free(env, server->poll_fds);
    }

    if(server->workers)
    {
        dc_free(env, server->workers);
    }

    dc_close(env, err, server->domain_socket);
    dc_close(env, err, server->pipe_fd);
}

static void run_server(const struct dc_env *env, struct dc_error *err, struct options *opts, struct server_info *server)
{
    DC_TRACE(env);
    server_loop(env, err, opts, server);

    /*
    for(int i = 0; i < server->num_workers; i++)
    {
        kill(server->workers[i], SIGINT);
    }
    */

    wait_for_workers(env, err, server);
}

static void server_loop(const struct dc_env *env, struct dc_error *err, struct options *opts, struct server_info *server)
{
    DC_TRACE(env);

    running = 1;
    while(running)
    {
        int poll_result;

        poll_result = dc_poll(env, err, server->poll_fds, server->num_fds, -1);

        if(poll_result < 0)
        {
            break;
        }

        if(poll_result == 0)
        {
            continue;
        }

        // the increment only happens if the connection isn't closed.
        // if it is closed everything moves down one spot.
        for(int i = 0; i < server->num_fds; i++)
        {
            struct pollfd *poll_fd;

            poll_fd = &server->poll_fds[i];

            if(poll_fd->revents != 0)
            {
                handle_change(env, err, opts, server, poll_fd);
            }
        }

        if(dc_error_has_error(err))
        {
            running = false;
        }
    }
}

static bool handle_change(const struct dc_env *env, struct dc_error *err, struct options *opts, struct server_info *server, struct pollfd *poll_fd)
{
    int fd;
    short revents;
    int close_fd;

    DC_TRACE(env);
    fd = poll_fd->fd;
    revents = poll_fd->revents;
    close_fd = -1;

    if((unsigned int)revents & (unsigned int)POLLHUP)
    {
        if(fd != server->listening_socket && fd != server->pipe_fd)
        {
            close_fd = fd;
        }
    }
    else if((unsigned int)revents & (unsigned int)POLLIN)
    {
        if(fd == server->listening_socket)
        {
            accept_connection(env, err, server);
        }
        else if(fd == server->pipe_fd)
        {
            struct revive_message message;

            revive_socket(env, err, server, &message);

            if(message.closed)
            {
                close_fd = message.fd;
            }
        }
        else
        {
            poll_fd->events = 0;
            write_socket_to_domain_socket(env, err, server, fd);
        }
    }

    if(close_fd > -1)
    {
        close_connection(env, err, server, close_fd);
    }

    return close_fd != -1;
}

static void accept_connection(const struct dc_env *env, struct dc_error *err, struct server_info *server)
{
    struct sockaddr_in client_address;
    socklen_t client_address_len;
    int client_socket;

    DC_TRACE(env);
    client_address_len = sizeof(client_address);
    client_socket = dc_accept(env, err, server->listening_socket, (struct sockaddr *)&client_address, &client_address_len);
    server->poll_fds = (struct pollfd *)dc_realloc(env, err, server->poll_fds, (server->num_fds + 2) * sizeof(struct pollfd));
    server->poll_fds[server->num_fds].fd = client_socket;
    server->poll_fds[server->num_fds].events = POLLIN | POLLHUP;
    server->poll_fds[server->num_fds].revents = 0;
    server->num_fds++;
    print_socket(env, err, "Accepted connection from", client_socket, VERBOSE_SERVER);
}
static void write_socket_to_domain_socket(const struct dc_env *env, struct dc_error *err, const struct server_info *server, int client_socket)
{
    struct msghdr msg;
    struct iovec iov;
    char control_buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;

    DC_TRACE(env);
    dc_memset(env, &msg, 0, sizeof(msg));
    dc_memset(env, &iov, 0, sizeof(iov));
    dc_memset(env, control_buf, 0, sizeof(control_buf));
    iov.iov_base = &client_socket;
    iov.iov_len = sizeof(client_socket);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_buf;
    msg.msg_controllen = sizeof(control_buf);
    cmsg = CMSG_FIRSTHDR(&msg);

    if(cmsg)
    {
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        *((int *) CMSG_DATA(cmsg)) = client_socket;
        print_fd(env, "Sending to client", client_socket, VERBOSE_SERVER);

        // Send the client listening_socket descriptor to the domain listening_socket
        dc_sendmsg(env, err, server->domain_socket, &msg, 0);
    }
    else
    {
        char *error_message;

        error_message = dc_strerror(env, err, errno);
        DC_ERROR_RAISE_SYSTEM(err, error_message, errno);
    }
}
static void revive_socket(const struct dc_env *env, struct dc_error *err, const struct server_info *server, struct revive_message *message)
{
    DC_TRACE(env);

    dc_sem_wait(env, err, server->domain_sem);
    dc_read(env, err, server->pipe_fd, message, sizeof(*message));

    if(dc_error_has_no_error(err))
    {
        print_fd(env, "Reviving listening_socket", message->fd, VERBOSE_SERVER);
        dc_sem_post(env, err, server->domain_sem);

        for(int i = 2; i < server->num_fds; i++)
        {
            struct pollfd *pfd;

            pfd = &server->poll_fds[i];

            if(pfd->fd == message->fd)
            {
                pfd->events = POLLIN | POLLHUP;
            }
        }
    }
}
static void close_connection(const struct dc_env *env, struct dc_error *err, struct server_info *server, int client_socket)
{
    DC_TRACE(env);
    print_fd(env, "Closing", client_socket, VERBOSE_SERVER);
    dc_close(env, err, client_socket);

    for(int i = 0; i < server->num_fds; i++)
    {
        if(server->poll_fds[i].fd == client_socket)
        {
            for(int j = i; j < server->num_fds - 1; j++)
            {
                server->poll_fds[j] = server->poll_fds[j + 1];
            }

            break;
        }
    }

    server->num_fds--;

    if(server->num_fds == 0)
    {
        free(server->poll_fds);
        server->poll_fds = NULL;
    }
    else
    {
        server->poll_fds = (struct pollfd *)realloc(server->poll_fds, server->num_fds * sizeof(struct pollfd));
    }
}
static void wait_for_workers(const struct dc_env *env, struct dc_error *err, struct server_info *server)
{
    DC_TRACE(env);

    // since the children have the signal handler too they will also be notified, no need to kill them
    for(int i = 0; i < server->num_workers; i++)
    {
        int status;

        do
        {
            dc_waitpid(env, err, server->workers[i], &status, WUNTRACED
                                                              #ifdef WCONTINUED
                                                              | WCONTINUED
#endif
            );
        }
        while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    dc_close(env, err, server->listening_socket);
}

static void worker_process(struct dc_env *env, struct dc_error *err, struct worker_info *worker)
{
    pid_t pid;

    DC_TRACE(env);

    if(DEBUG_HANDLER)
    {
        dc_env_set_tracer(env, dc_env_default_tracer);
    }
    else
    {
        dc_env_set_tracer(env, NULL);
    }

    pid = dc_getpid(env);
    printf("Started worker (%d)\n", pid);

    while(running)
    {
        process_message(env, err, worker);

        if(dc_error_has_error(err))
        {
            printf("%d : %s\n", getpid(), dc_error_get_message(err));
            dc_error_reset(err);
        }
    }

    dc_close(env, err, worker->domain_socket);
    dc_close(env, err, worker->pipe_fd);
}

static int handle_client_data(struct dc_env *env, struct dc_error *err, int clients) {
    DC_TRACE(env);

    if (recv(clients, NULL, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
        return -1;
    }
    ssize_t bytes_read;
    char buffer[READ_BUFFER_SIZE];
    bytes_read = dc_read(env, err, clients, buffer, (READ_BUFFER_SIZE-1));
    buffer[bytes_read] = '\0';

    if(bytes_read <= 0 && recv(clients, NULL, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
    {
        printf("Client disconnected\n");
        close(clients);
        return -1;
    }

    printf("Read from client\n");
    dc_write(env, err, STDOUT_FILENO, buffer, bytes_read);

    printf("Writing to client\n");
    uint16_t write_number = ntohs(bytes_read);
    dc_write(env, err, clients, &write_number, sizeof(write_number));
    return 0;
}

static void process_message(const struct dc_env *env, struct dc_error *err, struct worker_info *worker)
{

}
