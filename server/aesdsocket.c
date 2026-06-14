#include <stdio.h>
#include <stdlib.h>
#include <sys/syslog.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/stat.h>




#define BIND_PORT           (9000)
#define FILE_OUT            ((char *)"/var/tmp/aesdsocketdata")
#define CONS                (5)



volatile sig_atomic_t exit_req = 0;

static void sig_handler(int signo)
{
    if(signo == SIGINT || signo == SIGTERM)
    {
        syslog(LOG_INFO, "Caught signal, exiting");
        exit_req = 1;
    }
}



int main(int argc, char *argv[])
{
    bool daemon_mode = false;
    int opt;
    int result = 0;

    int sock = -1;


    FILE *fd;

    char *packet;
    size_t packet_len;

    while ((opt = getopt(argc, argv, "d")) != -1)
    {
        switch (opt)
        {
            case 'd':
                daemon_mode = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                return -1;
        }
    }

    struct sigaction sa = {0};
    sa.sa_handler = sig_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    openlog("aesdsocket", 0, LOG_USER);

    fprintf(stderr, "aesdsocket process started\n");

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1)
    {
        fprintf(stderr, "Failed to create socket, %s\n", strerror(errno));
        result = -1;
        goto end;
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        fprintf(stderr, "Failed to setsockopt, %s\n", strerror(errno));
        result = -1;
        goto end_socket;
    }


    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(BIND_PORT);

    int ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    if(ret != 0)
    {
        fprintf(stderr, "Failed to bind the socket, %s\n", strerror(errno));
        result = -1;
        goto end_socket;
    }


    if(daemon_mode)
    {
        pid_t pid = fork();

        if(pid < 0)
        {
            fprintf(stderr, "Failed to fork, %s\n", strerror(errno));
            result = -1;
            goto end_socket;
        }

        if(pid > 0)
        {
            exit(0);
        }

        if(setsid() < 0)
        {
            fprintf(stderr, "Failed to setsid, %s\n", strerror(errno));
            result = -1;
            goto end_socket;
        }

        chdir("/");
        umask(0);
        close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
    }

    packet = NULL;
	packet_len = 0;

    fd = fopen(FILE_OUT, "w");
    if(!fd)
    {
        fprintf(stderr, "Failed to fopen, %s\n", strerror(errno));
        result = -1;
        goto end_socket;
    }


    ret = listen(sock, CONS);
    if(ret == -1)
    {
        fprintf(stderr, "Failed to listen on the socket, %s\n", strerror(errno));
        result = -1;
        goto end_file;
    }


    while(!exit_req)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(sock, (struct sockaddr *)&client_addr, &client_len);
        if(client_fd == -1)
        {
            if(errno == EINTR)
            {
                continue;
            }

            fprintf(stderr, "Failed to accept, %s\n", strerror(errno));
            continue;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
        syslog(LOG_INFO, "Accepted connection from %s", ip_str);

        char recv_buf[1024];
        ssize_t n;

        while((n = recv(client_fd, recv_buf, sizeof(recv_buf), 0)) > 0)
        {
            char *new_packet = realloc(packet, packet_len + (size_t)n);
            if(!new_packet)
            {
                fprintf(stderr, "Failed to realloc, %s\n", strerror(errno));
                break;
            }
            packet = new_packet;
            memcpy(packet + packet_len, recv_buf, (size_t)n);
            packet_len += (size_t)n;

            char *start = packet;
            char *newline;

            while((newline = memchr(start, '\n', packet_len - (size_t)(start - packet))) != NULL)
            {
                size_t line_len = (size_t)(newline - start) + 1;

                fwrite(start, 1, line_len, fd);
                fflush(fd);

                FILE *rfd = fopen(FILE_OUT, "r");
                if(rfd)
                {
                    char send_buf[1024];
                    size_t r;

                    while((r = fread(send_buf, 1, sizeof(send_buf), rfd)) > 0)
                    {
                        send(client_fd, send_buf, r, 0);
                    }
                    fclose(rfd);
                }

                start = newline + 1;
            }

            size_t remaining = packet_len - (size_t)(start - packet);
            memmove(packet, start, remaining);
            packet_len = remaining;
        }

        syslog(LOG_INFO, "Closed connection from %s", ip_str);
        close(client_fd);
    }

end_file:
    closelog();
    free(packet);
    remove(FILE_OUT);
    fclose(fd);
end_socket:
    close(sock);
    sock = -1;
end:
    fprintf(stderr, "aesdsocket process end\n");

    return result;
}


