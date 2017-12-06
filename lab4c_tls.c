/*
NAME: Nathan Smith
EMAIL: nathan.smith@ucla.edu
ID: 704787554
*/

#include <errno.h>
#include <getopt.h>
#include <math.h>
// #include <mraa/aio.h>
// #include <mraa/gpio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

const int B = 4275; // B value of the thermistor
const int R0 = 100000; // R0 = 100k

int samplingInterval = 1;
char scale = 'F';
FILE* logFile;
bool paused = false;
int run_flag = 1;

int socketFileDescriptor;

/** System Calls **/
ssize_t readAndCheck(int fd, void* buf, size_t count)
{
  ssize_t status = read(fd, buf, count);
  if (status == -1) {
    fprintf(stderr, "[Read Error] Error Number: %d\nMessage: %s\n", errno, strerror(errno));
    exit(2);
  }
  return status;
}

int socketAndCheck(int socket_family, int socket_type, int protocol)
{
  int status = socket(socket_family, socket_type, protocol);
  if (status == -1) {
    fprintf(stderr, "[Socket Error] Error Number: %d Message: %s\n", errno, strerror(errno));
    exit(2);
  }
  return status;
}

int connectAndCheck(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
  int status = connect(sockfd, addr, addrlen);
  if (status == -1) {
    fprintf(stderr, "[Connect Error] Error Number: %d Message: %s\n", errno, strerror(errno));
    exit(2);
  }
  return status;
}

int pollAndCheck(struct pollfd* fds, nfds_t nfds, int timeout)
{
  int status = poll(fds, nfds, timeout);
  if (status == -1) {
    fprintf(stderr, "[Poll Error] Error Number: %d\nMessage: %s\n", errno, strerror(errno));
    exit(2);
  }
  return status;
}

int main(int argc, char** argv)
{
  struct option options[] = {
    { "period", required_argument, 0, 'p' },
    { "scale", required_argument, 0, 's' },
    { "log", required_argument, 0, 'l' },
    { "id", required_argument, 0, 'i' },
    { "host", required_argument, 0, 'h' },
    { 0, 0, 0, 0 }
  };

  int id;
  char* hostname = "lever.cs.ucla.edu";
  int portNumber;

  while (optind < argc) {
    int option;
    if ((option = getopt_long(argc, argv, "", options, 0)) != -1) {
      switch (option) {
      case 'p': // Period
        samplingInterval = atoi(optarg);
        break;
      case 's': // Scale
        if (strcmp(optarg, "F") != 0 && strcmp(optarg, "C") != 0) {
          exit(1);
        }
        scale = optarg[0];
        break;
      case 'l': // Log
        logFile = fopen(optarg, "w");
        break;
      case 'i': // Id
        id = atoi(optarg);
        printf("ID: %d\n", id);
        break;
      case 'h': // Host
        hostname = optarg;
        printf("Host: %s\n", hostname);
        break;
      default:
        fprintf(stderr, "[Error] Unsupported argument.\n");
        exit(0);
      }
    } else {
      // Non-switch parameter, port number
      portNumber = atoi(argv[optind]);
      printf("Port Number: %d\n", portNumber);
      optind++;
    }
  }

  // // Initialize Temperature Sensor
  // mraa_aio_context tempSensor = mraa_aio_init(1); // AIN0 mapped to MRAA pin 1

  // Establish connection
  struct sockaddr_in serverAddress;
  struct hostent* server = gethostbyname(hostname);
  socketFileDescriptor = socketAndCheck(AF_INET, SOCK_STREAM, 0);

  bzero((char*)&serverAddress, sizeof(serverAddress));
  serverAddress.sin_family = AF_INET;
  bcopy((char*)server->h_addr, (char*)&serverAddress.sin_addr.s_addr, server->h_length);
  serverAddress.sin_port = htons(portNumber);

  connectAndCheck(socketFileDescriptor, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

  printf("Connected.\n");

  // Set up TLS Session
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  const SSL_METHOD* method = TLSv1_2_client_method();
  SSL_CTX* sslContext = SSL_CTX_new(method);
  if (SSL_new(sslContext) < 0) {
    exit(2);
  }
  SSL* sslStructure = SSL_new(sslContext);
  if (SSL_set_fd(sslStructure, socketFileDescriptor) < 0) {
    exit(2);
  }
  if (SSL_connect(sslStructure) < 0) {
    exit(2);
  }

  printf("Set up TLS.\n");

  char idString[20];
  sprintf(idString, "ID=%d\n", id);
  if (SSL_write(sslStructure, idString, strlen(idString)) < 0) {
    exit(2);
  }

  printf("Wrote ID!\n");

  exit(0);
}