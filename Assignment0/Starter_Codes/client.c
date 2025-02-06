#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>

#define SEND_BUFFER_SIZE 2048

void chat_with_server(int sockfd);


/* TODO: client()
 * Open socket and send message from stdin.
 * Return 0 on success, non-zero on failure
*/
int client(char *server_ip, char *server_port) {
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(server_ip, server_port, &hints, &servinfo) != 0) {
      perror("getaddrinfo error");
      exit(1);
    }

    // client's socket following server protocol
    int sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (sockfd < 0) {
      perror("socket error");
      exit(1);
    }

    // no need to bind our client to a specific port

    // connect our client socket to the server
    if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
      perror("connect error");
      close(sockfd);
      exit(1);
    }

    // printf("connected!\n");

    chat_with_server(sockfd);

    close(sockfd);

    return 0;

}

void chat_with_server(int sockfd) {
  size_t bytes_read, bytes_sent, total_bytes_sent;
  char buffer[SEND_BUFFER_SIZE];

  fflush(stdin);
  while ((bytes_read = read(STDERR_FILENO, buffer, SEND_BUFFER_SIZE)) > 0) { // reads size - 1 chars/bytes
    total_bytes_sent = 0;
    while (total_bytes_sent < bytes_read) {
      bytes_sent = send(sockfd, buffer + total_bytes_sent, 
                      bytes_read - total_bytes_sent, 0);
      total_bytes_sent += bytes_sent;
      // printf("Bytes sent: %d\n", bytes_sent);
    }
  }
  if (bytes_read < 0) { // did not reach eof
    perror("read error");
    close(sockfd);
    exit(1);
  }
}

/*
 * main()
 * Parse command-line arguments and call client function
*/
int main(int argc, char **argv) {
  char *server_ip;
  char *server_port;

  if (argc != 3) {
    fprintf(stderr, "Usage: ./client-c [server IP] [server port] < [message]\n");
    exit(EXIT_FAILURE);
  }

  server_ip = argv[1];
  server_port = argv[2];
  return client(server_ip, server_port);
}
