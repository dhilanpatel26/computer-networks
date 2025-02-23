#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/wait.h>

#define QUEUE_LENGTH 10
#define RECV_BUFFER_SIZE 2048

int chat_with_client(int clientfd);
int find_server_socket_binding(struct addrinfo *servinfo);
int find_client_socket_binding(struct addrinfo *servinfo);
int proxy_server_socket_setup(char *proxy_port);
int proxy_client_socket_setup(char *server_ip, char *server_port);
int parse_request(char *buffer, int buffer_size);
int receive_from_socket(int sockfd, char *buffer, int buffer_size);


int find_server_socket_binding(struct addrinfo *servinfo) {
  struct addrinfo *p;
  int sockfd;
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

  if (p == NULL) {
    fprintf(stderr, "Failed to bind socket\n");
    return -1;
  }

  return sockfd;
}

int proxy_server_socket_setup(char *proxy_port) {
  struct addrinfo hints;
  struct addrinfo *servinfo;
  int sockfd;
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if (getaddrinfo(NULL, proxy_port, &hints, &servinfo) != 0) {
    printf("getaddrinfo error\n");
    return -1;
  }

  sockfd = find_server_socket_binding(servinfo);

  freeaddrinfo(servinfo); // free head of linked list

  if (sockfd < 0) {
    return -1;
  }

  if (listen(sockfd, QUEUE_LENGTH) < 0) {
    perror("listen error");
    close(sockfd);
    return -1;
  }

  return sockfd;
}

int find_client_socket_binding(struct addrinfo *servinfo) {
  struct addrinfo *p;
  int sockfd;
  // iterate through linked list and find acceptable connection
  for (p = servinfo; p != NULL; p = p->ai_next) {
    // client's socket following server protocol
    sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sockfd < 0) {
      perror("socket error");
      continue;
    }

    // no need to bind our client to a specific port
    // connect our client socket to the server
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) < 0) {
      perror("connect error");
      close(sockfd);
      continue;
    }

    break; // found a good one
  }

  if (p == NULL) {
    fprintf(stderr, "Failed to connect socket\n");
    return -1;
  }

  return sockfd;
}

int proxy_client_socket_setup(char *server_ip, char *server_port) {
  struct addrinfo hints, *servinfo;
    int sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(server_ip, server_port, &hints, &servinfo) != 0) {
      perror("getaddrinfo error");
      return -1;
    }

    sockfd = find_client_socket_binding(servinfo);

    freeaddrinfo(servinfo);

    if (sockfd < 0) {
      return -1;
    }

    return sockfd;
}

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

/* TODO: proxy()
 * Establish a socket connection to listen for incoming connections.
 * Accept each client request in a new process.
 * Parse header of request and get requested URL.
 * Get data from requested remote server.
 * Send data to the client
 * Return 0 on success, non-zero on failure
*/
int proxy(char *proxy_port) {
  int sockfd = proxy_server_socket_setup(proxy_port);
  if (sockfd < 0) {
    return -1;
  }

  struct sigaction sa;

  sa.sa_handler = sigchld_handler; // reap all dead processes
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
      perror("sigaction");
      return -1;
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
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork error");
      close(clientfd);
      continue;
    } else if (pid == 0) { // child process
      close(sockfd); // child doesn't need listener socket
      chat_with_client(clientfd);
      close(clientfd);
      return -1;
    } else { // parent process
      close(clientfd); // parent doesn't need client socket
    }

  }

  // parent process
  close(sockfd);

  return 0;

}

int parse_request(char *buffer, int buffer_size) {
  struct ParsedRequest *req = ParsedRequest_create();
  if (ParsedRequest_parse(req, buffer, buffer_size) < 0) {
    printf("parse failed\n");
    return -1;
  }

  printf("Method:%s\n", req->method);
  printf("Host:%s\n", req->host);

  int rlen = ParsedRequest_totalLen(req);
  char *b = (char *)malloc(rlen+1);
  if (ParsedRequest_unparse(req, b, rlen) < 0) {
    printf("unparse failed\n");
    return -1;
  }
  b[rlen]='\0';

  struct ParsedHeader *r = ParsedHeader_get(req, "If-Modified-Since");
  printf("Modified value: %s\n", r->value);

  ParsedRequest_destroy(req);
  free(b);

  return 0;
}

int receive_from_socket(int sockfd, char *buffer, int buffer_size) {
  ssize_t bytes_read, total_bytes_read;
  // ssize_t bytes_read, bytes_written, total_bytes_read;
  total_bytes_read = 0;
  while (
    (bytes_read = recv(sockfd, buffer + total_bytes_read, 
      RECV_BUFFER_SIZE - total_bytes_read, 0)) > 0 || 
    (bytes_read == -1 && errno == EINTR) // keep receiving on system interrupt
    ) {
    // bytes_written = write(STDOUT_FILENO, buffer + total_bytes_read, bytes_read);
    // if (bytes_written < 0 && errno != EINTR) {
    //   perror("write error");
    //   return -1; // do not exit program but terminate client connection
    // }
    total_bytes_read += bytes_read;
  }
  if (bytes_read < 0) { // exited not on eof
    perror("recv error");
    return -1; // do not exit program but terminate client conenction
  }
  return total_bytes_read;
}

int send_to_socket(int sockfd, char *buffer, int buffer_size) {
  int total_bytes_sent, bytes_sent;
  total_bytes_sent = 0;
  while (total_bytes_sent < buffer_size) {
    bytes_sent = send(sockfd, buffer + total_bytes_sent, 
                    buffer_size - total_bytes_sent, 0);
    total_bytes_sent += bytes_sent;
    if (bytes_sent < 0 && errno != EINTR) {
      perror("send error");
      close(sockfd);
      return -1;
    }
  }
  return 0;
}

int chat_with_client(int clientfd) {
  char client_buffer[RECV_BUFFER_SIZE]; // HTTP request is now limited to 2048 bytes
  ssize_t total_bytes_read = receive_from_socket(clientfd, client_buffer, RECV_BUFFER_SIZE);
  if (total_bytes_read < 0) {
    return -1;
  }

  // write(STDOUT_FILENO, "TOTAL\n", 6);
  // write(STDOUT_FILENO, buffer, total_bytes_read);

  // if (parse_request(buffer, total_bytes_read) < 0) {
  //   return -1;
  // };

  printf("Here 1\n");

  struct ParsedRequest *req = ParsedRequest_create();
  if (ParsedRequest_parse(req, client_buffer, total_bytes_read) < 0) {
    printf("parse failed\n");
    return -1;
  }
  
  if (!req->port) {
    req->port = "80";
  }

  // printf("Here 2\n");
  // printf("Method:%s\n", req->method);
  // printf("Host:%s\n", req->host);
  // printf("Port:%s\n", req->port);
  // printf("Path:%s\n", req->path);

  int serverfd = proxy_client_socket_setup(req->host, req->port);
  if (serverfd < 0) {
    printf("socket setup failed\n");
    return -1;
  }

  printf("Here 3\n");

  if (send_to_socket(serverfd, client_buffer, total_bytes_read) < 0) {
    printf("server send failed\n");
    return -1;
  }

  printf("Here 4\n");

  char server_buffer[RECV_BUFFER_SIZE];
  total_bytes_read = receive_from_socket(serverfd, server_buffer, RECV_BUFFER_SIZE);
  if (total_bytes_read < 0) {
    return -1;
  }
  
  write(STDOUT_FILENO, server_buffer, total_bytes_read);
  if (send_to_socket(clientfd, server_buffer, total_bytes_read) < 0) {
    printf("client send failed\n");
    return -1;
  }

  printf("Here 5\n");

  ParsedRequest_destroy(req);
  close(serverfd);
  return 0;
}


int main(int argc, char * argv[]) {
  char *proxy_port;

  if (argc != 2) {
    fprintf(stderr, "Usage: ./proxy <port>\n");
    exit(EXIT_FAILURE);
  }

  proxy_port = argv[1];
  return proxy(proxy_port);
}
