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

#define QUEUE_LENGTH 10
#define RECV_BUFFER_SIZE 2048

void chat_with_client(int clientfd);


/* 
 * Open socket and wait for client to connect
 * Print received message to stdout
 * Return 0 on success, non-zero on failure
*/
int server(char *server_port) {
  struct addrinfo hints;
  struct addrinfo *servinfo, *p;
  int sockfd;
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo(NULL, server_port, &hints, &servinfo) != 0) {
    perror("getaddrinfo error");
    exit(1);
  }

  // iterate through linked list and find acceptable binding
  for (p = servinfo; p != NULL; p = p->ai_next) {
    // server socket's job is to just listen for incoming connections
    sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sockfd < 0) {
      perror("socket error");
      continue;
    }

    int yes=1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
      perror("setsockopt error"); // bad port or actively in use
      close(sockfd);
      continue;
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) < 0) {
      perror("bind error");
      close(sockfd);
      continue;
    }

    break; // found a good one
  }

  freeaddrinfo(servinfo); // free head of linked list

  if (p == NULL) {
    fprintf(stderr, "Failed to bind socket\n");
    exit(1);
  }

  if (listen(sockfd, QUEUE_LENGTH) < 0) {
    perror("listen error");
    close(sockfd);
    exit(1);
  }

  while (1) {
    struct sockaddr_storage client_addr;
    socklen_t addr_size = sizeof client_addr;
    // client socket on the server, used to communicate with client socket on the client
    int clientfd;
    while ((clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size)) < 0) {
      if (errno != EINTR) {
        perror("accept error");
      }
      continue;
    }
    chat_with_client(clientfd);
    printf("\n");
    close(clientfd);
  }

  close(sockfd);

  return 0;

}

void chat_with_client(int clientfd) {
  char buffer[RECV_BUFFER_SIZE];
  ssize_t bytes_read, bytes_written;
  while (
    (bytes_read = recv(clientfd, buffer, RECV_BUFFER_SIZE, 0)) > 0 
    && errno != EINTR // keep receiving on system interrupt
    ) {
    bytes_written = write(STDOUT_FILENO, buffer, bytes_read);
    if (bytes_written < 0 && errno != EINTR) {
      perror("write error");
      return; // do not exit program but terminate client connection
    }
  }
  if (bytes_read < 0) { // exited not on eof
    perror("recv error");
    return; // do not exit program but terminate client conenction
  }
  return;
}

/*
 * main():
 * Parse command-line arguments and call server function
*/
int main(int argc, char **argv) {
  char *server_port;

  if (argc != 2) {
    fprintf(stderr, "Usage: ./server-c [server port]\n");
    exit(EXIT_FAILURE);
  }

  server_port = argv[1];
  return server(server_port);
}
