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


/* TODO: server()
 * Open socket and wait for client to connect
 * Print received message to stdout
 * Return 0 on success, non-zero on failure
*/
int server(char *server_port) {
  struct addrinfo hints;
  struct addrinfo *servinfo;
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo("127.0.0.1", server_port, &hints, &servinfo) != 0) {
    printf("error\n");
  }

  // servinfo is a linked list of addrinfo structs.
  // TODO: walk the linked list to find a good entry (some may be bad!)
  // see examples of what to look for
  
  // server socket's job is to just listen for incoming connections
  int sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
  if (sockfd < 0) {
    perror("socket error");
    exit(1);
  }

  int yes=1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
    perror("setsockopt error");
    close(sockfd);
    exit(1); // bad port or actively in use
  }

  if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
    perror("bind error");
    close(sockfd);
    exit(1);
  }

  freeaddrinfo(servinfo);

  if (listen(sockfd, QUEUE_LENGTH) < 0) {
    perror("listen error");
    close(sockfd);
    exit(1);
  }

  // printf("Here!\n");

  while (1) {
    struct sockaddr_storage client_addr;
    socklen_t addr_size = sizeof client_addr;
    // printf("about to accept!\n");
    // client socket on the server, used to communicate with client socket on the client
    int clientfd;
    while ((clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size)) < 0) {
      if (errno != EINTR) {
        perror("accept error");
      }
      continue;
    }
    // printf("Client %d accepted!\n", clientfd);
    chat_with_client(clientfd);
    printf("\n");
    close(clientfd);
  }

  close(sockfd);

  return 0;

}

void chat_with_client(int clientfd) {
  char buffer[RECV_BUFFER_SIZE];
  ssize_t bytes_read;
  while ((bytes_read = recv(clientfd, buffer, RECV_BUFFER_SIZE, 0)) > 0) {
    buffer[bytes_read] = '\0'; // in-range because client sent omitting \0
    printf("%s", buffer);
    fflush(stdout); // immediate output
  }
  if (bytes_read < 0) {
    perror("recv error");
    // do not exit program
  }
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
