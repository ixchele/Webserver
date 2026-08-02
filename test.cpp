#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_LENGTH 1000
#define WHITESPACE " \t\r\n"
#define CRLF "\r\n"

static void sigint_handler (int signum); /* SIGINT handler */
static bool process_request (int);
static int setup_server (uint16_t);

int serverfd = -1; /* Server socket file descriptor */

int
main (int argc, char *argv[])
{
  /* Create a sigaction and link it to the handler */
  struct sigaction sa;
  sa.sa_handler = sigint_handler;
  assert (sigaction (SIGINT, &sa, NULL) != -1);
  assert (sigaction (SIGTERM, &sa, NULL) != -1);

  /* Get the port number from the command line and set up the
     server. If the setup does not work, the socket file
     descriptor will be negative; exit if that happens. */
  if (argc != 2)
    return EXIT_FAILURE;
  uint16_t port = (uint16_t) strtoul (argv[1], NULL, 10);
  if ( (serverfd = setup_server (port)) < 0)
    {
      perror ("Failed to set up web server");
      return EXIT_FAILURE;
    }

  /* Enter a loop for processing web connections */
  bool running = true;
  while (running)
    {
      struct sockaddr_in address;
      memset (&address, 0, sizeof (address));
      socklen_t addrlen = 0;

      int connection = accept (serverfd, (struct sockaddr *)&address, &addrlen);
      if (connect < 0)
        break;

      running = process_request (connection);
    }

  /* After receiving a shutdown request, close the server
     socket and exit. Note that it would be good to include a
     signal handler (Chapter 2) to close the socket if the
     process is killed with SIGINT or another signal. */
  shutdown (serverfd, SHUT_RDWR);
  close (serverfd);
  serverfd = -1;
  return EXIT_SUCCESS;
}

/* Parse the incoming HTTP request and look for the requested
   file. It must be of the form /cgi-bin/foo.cgi or it must be
   shutdown. All other requests are ignored. Returning true
   from this function keeps the server running, while returning
   false will shut the server down. */
static bool
process_request (int connection)
{
  /* Read the HTTP request from the socket into a local buffer */
  char buffer[BUFFER_LENGTH];
  memset (buffer, 0, BUFFER_LENGTH);
  size_t bytes = read (connection, buffer, BUFFER_LENGTH - 1);
  if (bytes <= 0)
    {
      perror ("No data read from socket");
      shutdown (connection, SHUT_RDWR);
      close (connection);
      return true;
    }

  /* This minimal web server only supports requests of the
     following formats:

       GET /cgi-bin/hello.cgi HTTP/1.1 ...
       GET /cgi-bin/hello.cgi?username=me HTTP/1.1 ...
       GET /shutdown ...

   */

  /* Reject any request that doesn't start with "GET" */
  char *token = strtok (buffer, WHITESPACE);
  if (strncmp (token, "GET", 4))
    {
      perror ("Invalid HTTP request received");
      shutdown (connection, SHUT_RDWR);
      close (connection);
      return true;
    }

  /* Check for a shutdown request. Note that all URIs passed from
     the web browser will begin with a '/' character, so we are
     looking for "/shutdown". */
  token = strtok (NULL, WHITESPACE);
  if (!strncmp (token, "/shutdown", 10))
    {
      /* Send a message confirming the shutdown request, then
         return false to shut the server down */
      char *message = "HTTP/1.1 200 OK" CRLF
        "Connection: close" CRLF
        "Content-Type: text/html; charset=UTF-8" CRLF CRLF
        "<html><body><h2>Shutdown received</h2>"
        "<p>Goodbye</p></body></html>\n";

      write (connection, message, strlen (message));
      shutdown (connection, SHUT_RDWR);
      close (connection);
      return false;
    }

  /* For this example, we are only supporting CGI executable
     files and they must exist in a "cgi-bin" subdirectory of the
     server's working directory. Reject any request that does not
     begin with "/cgi-bin/". */
  if (strncmp (token, "/cgi-bin/", 9))
    {
      shutdown (connection, SHUT_RDWR);
      close (connection);
      return true;
    }

  /* Remove the leading '/' character, as we are looking for
     files based on an relative path, not an absolute path */
  char *cgi_file = token + 1;

  /* Search for a query string, which begins immediately following
     the '?' character if it exists. If one is found, keep track
     of where the query string starts and replace the '?' with a 
     null byte. This will ensure the file name is properly
     terminated. For example, "cgi-bin/hello.cgi?user=me" will be
     converted to the cgi_file "cgi-bin/hello.cgi" while the 
     question pointer will point to "user=me". */
  char *question = strchr (cgi_file, '?');
  if (question != NULL)
    {
      *question = '\0';
      question++;
    }
  /* Check for file read and execute permissions */
  if (access (cgi_file, R_OK | X_OK) != 0)
    {
      shutdown (connection, SHUT_RDWR);
      close (connection);
      return true;
    }

  /* Now we are ready to respond. Write back an HTTP/1.1 header
     to the web browser then run the CGI program. */
  char *message = "HTTP/1.1 200 OK" CRLF "Connection: close" CRLF
    "Content-Type: text/html; charset=UTF-8" CRLF CRLF;
  write (connection, message, strlen (message));

  pid_t child_pid = fork ();
  if (child_pid < 0)
    {
      shutdown (connection, SHUT_RDWR);
      close (connection);
      return false;
    }

  if (child_pid == 0)
    {
      /* If query string passed, set the environment variable */
      if (question != NULL)
        setenv ("QUERY_STRING", question, 1);

      /* Redirect the child process's STDOUT to write into the
         socket and execute the CGI program */
      dup2 (connection, STDOUT_FILENO);
      execlp (cgi_file, cgi_file, NULL);
      return true;
    }

  /* The parent waits until the child process runs (writing to the
     client over the socket), then closes the socket and continues
     with the next request */
  wait (NULL);
  shutdown (connection, SHUT_RDWR);
  close (connection);
  return true;
}

/* Set up a basic HTTP server on the localhost, using the port
   number passed on the command line. In this example, we are
   manually configuring the server to use 127.0.0.1 (localhost
   loopback) as the IP address. */
static int
setup_server (uint16_t port)
{
  /* Manually configure socket address and protocol information */
  struct sockaddr_in localhost;
  memset (&localhost, 0, sizeof (localhost));
  localhost.sin_family = AF_INET;                 /* IPv4 address */
  localhost.sin_port = htons (port);              /* port number from user */
  localhost.sin_addr.s_addr = htonl (0x7f000001); /* localhost loopback */

  struct addrinfo server;
  memset (&server, 0, sizeof (server));
  server.ai_family = AF_INET;         /* grab IPv4 only */
  server.ai_socktype = SOCK_STREAM;   /* specify byte-streaming */
  server.ai_flags = AI_PASSIVE;       /* use default IP address */
  server.ai_protocol = IPPROTO_TCP;   /* create as a TCP socket */
  server.ai_addrlen = INET_ADDRSTRLEN; /* IPv4 address length */
  server.ai_addr = (struct sockaddr *) &localhost;

  /* Create a TCP socket, then configure it to ignore bind reuse
     false errors and set a 5-second timeout for receiving */
  int socketfd = socket (server.ai_family, server.ai_socktype, 0);
  assert (socketfd >= 0);
  int sockopt = 1;
  setsockopt (socketfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &sockopt,
              sizeof (int));
  struct timeval timeout = { 5, 0 };
  setsockopt (socketfd, SOL_SOCKET, SO_RCVTIMEO, (const void *) &timeout,
              sizeof (timeout));

  /* Bind the socket to the port number and convert it to a
     server socket */
  if (bind (socketfd, server.ai_addr, server.ai_addrlen) != 0) 
    {
      close (socketfd);
      return -1;
    }
  listen (socketfd, 10);
  return socketfd;
}

/* Interrupt handler in case we need to shut down with Ctrl-c.
   This helps to ensure the server socket is shutdown cleanly. */
static void
sigint_handler (int signum)
{
  if (serverfd >= 0)
    {
      shutdown (serverfd, SHUT_RDWR);
      close (serverfd);
    }
  exit (EXIT_SUCCESS);
}