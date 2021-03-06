#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>
#include "libhttp.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;
#define LIMIT_HANDLE_MAX_SIZE 8192 
/* 
 * Reads an HTTP request from stream (fd), and writes an HTTP response * containing: * 
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory, 
 *      send the index.html file.  
 *   3) If user requested a directory and index.html doesn't exist, send a list 
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
static char buf[LIMIT_HANDLE_MAX_SIZE];
void handle_files_request(int fd) {

  /* YOUR CODE HERE (Feel free to delete/modify the existing code below) */
 int sendsize;
  struct http_request *request = http_request_parse(fd);
  if(chdir( server_files_directory) < 0)
	  return;
  struct stat *fstat = malloc(sizeof(struct stat));
  if(fstat == NULL)
	  fprintf(stderr,"%s\n","malloc failed");
  if(request == NULL)
	  return;
  buf[0] = '.', buf[1] = '\0';
  strcat(buf, request->path);
  stat(buf, fstat);
  if(S_ISREG(fstat->st_mode)){
	  FILE* file = fopen(buf, "rb");
	  if(file == NULL) return;
	  fseek(file, 0, SEEK_END);
	  int filesize = ftell(file);
	  rewind(file);
	  char filesize_str[16];
	  sprintf(filesize_str, "%d", filesize);
	  http_start_response(fd, 200);
	  http_send_header(fd, "Content-type", http_get_mime_type(request->path+1));
	  http_send_header(fd, "Content-length",filesize_str);
	  http_end_headers(fd);
	  while(!feof(file)){
		  sendsize = fread(buf, 1, LIMIT_HANDLE_MAX_SIZE, file);
		  http_send_data(fd, buf, sendsize);
	  }	
	  fclose(file);

  }
  else if(S_ISDIR(fstat->st_mode)){
	  DIR* dir = opendir(buf);
	  struct dirent *pdirent; 
	  const char* defaultfile = "/index.html";
	  strcat(buf, defaultfile);
	  FILE* findex = fopen(buf, "rb");

	  if(findex != NULL)
	  {
		  http_start_response(fd, 200);
		  http_send_header(fd, "Content-type", "text/html");
		  http_end_headers(fd);
		  while(!feof(findex)){
			  sendsize = fread(buf, 1, LIMIT_HANDLE_MAX_SIZE, findex);
			  http_send_data(fd, buf, sendsize);
		  }	
		  fclose(findex);
	  }
	  else{
		  http_start_response(fd, 200);
		  http_send_header(fd, "Content-type", "text/html");
		  http_end_headers(fd);
		  http_send_string(fd,
				  "<center>"
				  "<h1>Welcome to httpserver!</h1>"
				  "</center>");
		  http_send_string(fd,"<p>"
				  "<a href=\"../\">");
		  int pathlen = strlen(request->path),i;
		  i = pathlen - 1;
		  // ingonre extra slash in the request path tail
		  while(i > 0 &&request->path[i] == '/')i--;
		  // jump to parent directory path means goback until slash
		  while(i > 0 &&request->path[i] != '/')i--;
		  // if parent directory path is / means homepage otherwise means a real directory
		  if(i != 0)		  
			  http_send_string(fd, "Parent Directory"); 
		  else
			  http_send_string(fd, "Home");

		  http_send_string(fd, "</a>"
				  "</p>");
		  while( (pdirent = readdir(dir)) && pdirent != NULL ){ 
			  /* skip this directory and parent directory which reprented by '.' and '..' respectively */ 
			  if(pdirent->d_name[0] == '.' ||(pdirent->d_type != DT_REG && 
						  pdirent->d_type != DT_DIR))continue;
			  http_send_string(fd,"<p>"
					  "<a href=\"");
			  http_send_string(fd, pdirent->d_name); 
			  if(pdirent->d_type == DT_DIR) 
				  http_send_string(fd,"/\">");
			  else
				  http_send_string(fd, "\">");
			  http_send_string(fd, pdirent->d_name); 
			  http_send_string(fd, "</a>"
					  "</p>");
		  }
	  }
	  closedir(dir);
  }
  else{
	  http_start_response(fd, 404);
  }
}

/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
int http_send_until_host(int fd, char* data, size_t size){
	int send_bytes;
	int i =0;
	char str[10];
	const char* host = "Host:";
	int start, end;
	char found = 0;
	while(!found&&i < size){
		if(data[i] == host[0]){
			start = i;
			end = i;
			while(end < size&&isalpha(data[end]))end++;
			if(end < size) {
				end ++; // for symbol :
				memcpy(str, data+start,end - start);
				str[end - start] = '\0';
				if(strcmp(str, host) == 0){
					send_bytes = i;
					found = 1;
				}
				i = end;
			}
		} 	
		else {
			while(i < size && isalpha(data[i]))i++;
		}
		while(i + 1 < size&&!(data[i] =='\r'&&data[i+1]=='\n'))i ++;
		i+= 2;
	}
	if(found)
	{
		http_send_data(fd, data, send_bytes);
	}
	else{
		http_send_data(fd, data, size);	
	}
	return i;// rest part to be sended
}
void handle_proxy_request(int fd) {

  /* YOUR CODE HERE */
  int proxy_socket_number;
  fd_set read_fd_set,active_fd_set;
  // read from client
  int bytes_read,bytes_send;
  // get ip of proxy target

  struct hostent* hnt = gethostbyname(server_proxy_hostname);
  struct sockaddr_in proxy_address;

  if(hnt->h_addrtype == AF_INET){
	  proxy_socket_number = socket(PF_INET, SOCK_STREAM, 0);
	  memset(&proxy_address, 0, sizeof(struct sockaddr_in));
	  proxy_address.sin_family = AF_INET;
	  proxy_address.sin_addr = *(struct in_addr*)hnt->h_addr;
	  proxy_address.sin_port = htons(server_proxy_port);
  }else{
	 return;
  }
	printf("connect to %s:%d\n", inet_ntoa(proxy_address.sin_addr), server_proxy_port);
  // connect to proxy target
  if(connect( proxy_socket_number, (struct sockaddr*)&proxy_address, sizeof(struct sockaddr)) == -1){
	  fprintf(stderr, "connect");
	  return;
  }
  FD_ZERO(&read_fd_set);
  FD_SET(fd, &read_fd_set);
  FD_SET(proxy_socket_number, &read_fd_set);
  active_fd_set = read_fd_set;
  while(1){
	  read_fd_set = active_fd_set;
	  if(select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0){
		  perror("select");
		  exit(EXIT_FAILURE); 
	  }

	 if(FD_ISSET(fd, &read_fd_set)){
			  bytes_read = read(fd, buf, LIMIT_HANDLE_MAX_SIZE);
			  if(bytes_read <= 0)
				  break;
			  printf("client send %d bytes\n",bytes_read);
			 // send until HOST method
			int offset = http_send_until_host(proxy_socket_number, buf, bytes_read);
			// if there is HOST method, modify the host address to proxy server name  
		        if(offset > 0){
				dprintf(proxy_socket_number, "Host: %s:%d\r\n", server_proxy_hostname, server_proxy_port);
			}	
			printf(offset != bytes_read?"there is host method\n":"host method not found");
			// send the rest part in readed buffer
			bytes_send = write(proxy_socket_number, buf+offset, bytes_read - offset);
			  if(bytes_send < 0)
				  break;
	}
	else if(FD_ISSET(proxy_socket_number, &read_fd_set)){
			  bytes_read = read(proxy_socket_number, buf, LIMIT_HANDLE_MAX_SIZE);
			  if(bytes_read <= 0)
				  break;
			  bytes_send = write(fd, buf, bytes_read);
			  if(bytes_send < 0)
				  break;
	}
  }
	close(proxy_socket_number);
}


/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;
  pid_t pid;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  while (1) {

    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    pid = fork();
    if (pid > 0) {
      close(client_socket_number);
    } else if (pid == 0) {
      // Un-register signal handler (only parent should have it)
      signal(SIGINT, SIG_DFL);
      close(*socket_number);
      request_handler(client_socket_number);
      close(client_socket_number);
      exit(EXIT_SUCCESS);
    } else {
      perror("Failed to fork child");
      exit(errno);
    }
  }

  close(*socket_number);

}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
