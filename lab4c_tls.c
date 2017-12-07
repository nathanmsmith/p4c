/*
NAME: Nathan Smith
EMAIL: nathan.smith@ucla.edu
ID: 704787554
*/

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <mraa/aio.h>
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

#define SUCCESS 0
#define INVALID_ARGUMENT 1
#define OTHER_FAILURE 2

const int B = 4275; // B value of the thermistor
const int R0 = 100000; // R0 = 100k

// Use values to track whether variables were set or not
int id = 0;
char* hostname = ""; // Should be lever.cs.ucla.edu
FILE* logFile = NULL;
int portNumber = 0;

int samplingInterval = 1;
char scale = 'F';
bool paused = false;
int run_flag = 1;

int socketFileDescriptor;

SSL* sslStructure;

/** System Calls **/
ssize_t
readAndCheck(int fd, void* buf, size_t count)
{
  ssize_t status = read(fd, buf, count);
  if (status == -1) {
    exit(OTHER_FAILURE);
  }
  return status;
}

int socketAndCheck(int socket_family, int socket_type, int protocol)
{
  int status = socket(socket_family, socket_type, protocol);
  if (status == -1) {
    exit(OTHER_FAILURE);
  }
  return status;
}

int connectAndCheck(int sockfd, const struct sockaddr* addr, socklen_t addrlen)
{
  int status = connect(sockfd, addr, addrlen);
  if (status == -1) {
    exit(OTHER_FAILURE);
  }
  return status;
}

int pollAndCheck(struct pollfd* fds, nfds_t nfds, int timeout)
{
  int status = poll(fds, nfds, timeout);
  if (status == -1) {
    exit(OTHER_FAILURE);
  }
  return status;
}

void getCurrentTime(char* timeString)
{
  time_t timer;
  struct tm* timeInfo;

  time(&timer);
  timeInfo = localtime(&timer);
  strftime(timeString, 10, "%H:%M:%S", timeInfo);
}

double convertTemperature(int rawTemp, char scale)
{
  double convertedTemp = (1023.0 / ((double)rawTemp) - 1.0) * R0;
  double celsius = 1.0 / (log(convertedTemp / 100000.0) / B + 1 / 298.15) - 273.15;

  if (scale == 'C') {
    return celsius;
  }
  double farenheit = celsius * 9 / 5 + 32;
  return farenheit;
}

void startProgram()
{
  fprintf(logFile, "START\n");
  fflush(logFile);
  paused = false;
}

void stopProgram()
{
  fprintf(logFile, "STOP\n");
  fflush(logFile);
  paused = true;
}

void shutdownProgram()
{
  char timeString[10];
  getCurrentTime(timeString);

  dprintf(socketFileDescriptor, "%s SHUTDOWN\n", timeString);
  fprintf(logFile, "%s SHUTDOWN\n", timeString);
  fflush(logFile);

  run_flag = 0;
}

void changeScale(char newScale)
{
  scale = newScale;

  char command[10];
  if (scale == 'C') {
    strcpy(command, "SCALE=C");
  } else {
    strcpy(command, "SCALE=F");
  }
  fprintf(logFile, "%s\n", command);
  fflush(logFile);
}

void changePeriod(char* newPeriod)
{
  samplingInterval = atoi(newPeriod);

  char command[10] = "PERIOD=";
  strcat(command, newPeriod);
  fprintf(logFile, "%s\n", command);
  fflush(logFile);
}

void logLine(char* line)
{
  fprintf(logFile, line);
}

void processCommand(char* input)
{
  if (strcmp(input, "STOP") == 0) {
    stopProgram();
  } else if (strcmp(input, "START") == 0) {
    startProgram();
  } else if (strcmp(input, "OFF") == 0) {
    fprintf(logFile, "OFF\n");
    fflush(logFile);

    shutdownProgram();
  } else if (strcmp(input, "SCALE=F") == 0) {
    changeScale('F');
  } else if (strcmp(input, "SCALE=C") == 0) {
    changeScale('C');
  } else if (strncmp(input, "PERIOD=", 7) == 0) {
    changePeriod(input + 7 * sizeof(char));
  } else if (strncmp(input, "LOG ", 4) == 0) {
    logLine(input);
  }
}

void writeToServerOverSSLAndLog(char* str)
{
  if (SSL_write(sslStructure, str, strlen(str)) < 0) {
    exit(OTHER_FAILURE);
  }
  fprintf(str, str);
  fflush(logFile);
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

  int option;
  while ((option = getopt_long(argc, argv, "", options, 0)) != -1) {
    switch (option) {
    case 'p': // Period
      samplingInterval = atoi(optarg);
      break;
    case 's': // Scale
      if (strcmp(optarg, "F") != 0 && strcmp(optarg, "C") != 0) {
        exit(INVALID_ARGUMENT);
      }
      scale = optarg[0];
      break;
    case 'l': // Log
      logFile = fopen(optarg, "w");
      if (!logFile) {
        exit(OTHER_FAILURE);
      }
      break;
    case 'i': // Id
      id = atoi(optarg);
      break;
    case 'h': // Host
      hostname = optarg;
      break;
    default:
      exit(INVALID_ARGUMENT);
    }
  }

  if (optind != argc) {
    portNumber = atoi(argv[optind]);
  }

  // Since --id, --host, --log, and port number are mandatory
  if (id <= 0 || strcmp(hostname, "") == 0 || logFile == NULL || portNumber == 0) {
    exit(INVALID_ARGUMENT);
  }

  fprintf(logFile, "hello");

  // Initialize Temperature Sensor
  mraa_aio_context tempSensor = mraa_aio_init(1); // AIN0 mapped to MRAA pin 1

  // Establish connection
  struct sockaddr_in serverAddress;
  struct hostent* server = gethostbyname(hostname);
  socketFileDescriptor = socketAndCheck(AF_INET, SOCK_STREAM, 0);

  bzero((char*)&serverAddress, sizeof(serverAddress));
  serverAddress.sin_family = AF_INET;
  bcopy((char*)server->h_addr, (char*)&serverAddress.sin_addr.s_addr, server->h_length);
  serverAddress.sin_port = htons(portNumber);

  connectAndCheck(socketFileDescriptor, (struct sockaddr*)&serverAddress, sizeof(serverAddress));

  fprintf(logFile, "hi");

  // Set up TLS Session
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  const SSL_METHOD* method = TLSv1_client_method();
  SSL_CTX* sslContext = SSL_CTX_new(method);
  sslStructure = SSL_new(sslContext);
  if (SSL_set_fd(sslStructure, socketFileDescriptor) < 0) {
    exit(OTHER_FAILURE);
  }
  if (SSL_connect(sslStructure) < 0) {
    exit(OTHER_FAILURE);
  }

  fprintf(logFile, "what's up");

  char idString[20];
  sprintf(idString, "ID=%d\n", id);
  writeToServerOverSSLAndLog(idString);

  fprintf(logFile, "nm nm u?");

  struct pollfd pollArr[1];
  pollArr[0].fd = socketFileDescriptor;
  pollArr[0].events = POLLIN;

  while (run_flag) {
    if (!paused) {
      int rawTemp = mraa_aio_read(tempSensor);

      double convertedTemp = convertTemperature(rawTemp, scale);
      char timeString[10];
      getCurrentTime(timeString);

      char tempString[20];
      sprintf(tempString, "%s %.1f\n", timeString, convertedTemp);
      writeToServerOverSSLAndLog(tempString);
    }

    pollAndCheck(pollArr, 1, 0);
    if ((pollArr[0].revents & POLLIN)) {
      char input[100];
      int bytesRead = SSL_read(sslStructure, input, 100);

      int i;
      int start = 0;
      for (i = 0; i < bytesRead; i++) {
        // Commands end with a newline
        if (input[i] == '\n') {
          input[i] = '\0';
          processCommand(input + start);
          start = i + 1;
        }
      }
    }

    if (!paused) {
      usleep(samplingInterval * 1000000);
    }
  }

  exit(0);
}