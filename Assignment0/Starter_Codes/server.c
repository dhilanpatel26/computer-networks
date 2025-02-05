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

/* TODO: server()
 * Open socket and wait for client to connect
 * Print received message to stdout
 * Return 0 on success, non-zero on failure
*/
int server(char *server_port) {
  int status;
  struct addrinfo hints;
  struct addrinfo *servinfo;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((status = getaddrinfo("127.0.0.1", server_port, &hints, &servinfo)) != 0) {
    printf("error\n");
  }

  // servinfo is a linked list of addrinfo structs.
  // TODO: walk the linked list to find a good entry (some may be bad!)
  // see examples of what to look for
  
  int sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
  if (sockfd < 0) {
    perror("socket error: ");
    exit(1);
  }

  int yes=1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
    perror("setsockopt error: ");
    exit(1); // bad port or actively in use
  }

  if (bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
    perror("bind error: ");
    exit(1);
  }

  if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
    perror("connect error: ");
    exit(1);
  }



  // when to close?
  freeaddrinfo(servinfo);
  //  while (1) {
  //  
  // }

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
