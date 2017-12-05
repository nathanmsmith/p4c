/*
NAME: Nathan Smith
EMAIL: nathan.smith@ucla.edu
ID: 704787554
*/

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <mraa/aio.h>
#include <mraa/gpio.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

const int B = 4275; // B value of the thermistor
const int R0 = 100000; // R0 = 100k

int samplingInterval = 1;
char scale = 'F';
FILE* logFile;
bool paused = false;
int run_flag = 1;

int pollAndCheck(struct pollfd* fds, nfds_t nfds, int timeout)
{
  int status = poll(fds, nfds, timeout);
  if (status == -1) {
    fprintf(stderr, "[Poll Error] Error Number: %d\nMessage: %s\n", errno, strerror(errno));
    exit(1);
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

void start()
{
  fprintf(logFile, "START\n");
  fflush(logFile);
  paused = false;
}

void stop()
{
  fprintf(logFile, "STOP\n");
  fflush(logFile);
  paused = true;
}

void shutdown()
{
  char timeString[10];
  getCurrentTime(timeString);

  printf("%s SHUTDOWN\n", timeString);
  if (logFile) {
    fprintf(logFile, "%s SHUTDOWN\n", timeString);
    fflush(logFile);
  }

  run_flag = 0;
}

void changeScale(char newScale)
{
  scale = newScale;

  if (logFile) {

    char command[10];
    if (scale == 'C') {
      strcpy(command, "SCALE=C");
    } else {
      strcpy(command, "SCALE=F");
    }
    fprintf(logFile, "%s\n", command);
    fflush(logFile);
  }
}

void changePeriod(char* newPeriod)
{
  samplingInterval = atoi(newPeriod);

  if (logFile) {
    char command[10] = "PERIOD=";
    strcat(command, newPeriod);

    fprintf(logFile, "%s\n", command);
    fflush(logFile);
  }
}

void processCommand(char* input)
{
  if (strcmp(input, "STOP") == 0) {
    stop();
  } else if (strcmp(input, "START") == 0) {
    start();
  } else if (strcmp(input, "OFF") == 0) {
    if (logFile) {
      fprintf(logFile, "OFF\n");
      fflush(logFile);
    }
    shutdown();
  } else if (strcmp(input, "SCALE=F") == 0) {
    changeScale('F');
  } else if (strcmp(input, "SCALE=C") == 0) {
    changeScale('C');
  } else if (strncmp(input, "PERIOD=", 7) == 0) {
    changePeriod(input + 7 * sizeof(char));
  }
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

  char* id;
  char* hostname;
  char* portNumber;

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
        id = optarg;
        printf("ID: %s", id);
        break;
      case 'h': // Host
        hostname = optarg;
        printf("Host: %s", hostname);
        break;
      default:
        fprintf(stderr, "[Error] Unsupported argument.\n");
        exit(0);
      }
    } else {
      // Non-switch parameter, port number
      portNumber = argv[optind];
      printf("Port Number: %s", hostname);
      optind++;
    }
  }

  // Initialize Temperature Sensor
  mraa_aio_context tempSensor;
  tempSensor = mraa_aio_init(1); // AIN0 mapped to MRAA pin 1

  // Initialize button
  mraa_gpio_context button;
  button = mraa_gpio_init(60); // Spec calls for D3 pin, unsure of where that is so this may change
  mraa_gpio_dir(button, MRAA_GPIO_IN);
  mraa_gpio_isr(button, MRAA_GPIO_EDGE_RISING, &shutdown, NULL);

  struct pollfd pollArr[1];
  pollArr[0].fd = STDIN_FILENO;
  pollArr[0].events = POLLIN | POLLHUP | POLLERR;

  while (run_flag) {
    if (!paused) {
      int rawTemp = mraa_aio_read(tempSensor);

      double convertedTemp = convertTemperature(rawTemp, scale);
      char timeString[10];
      getCurrentTime(timeString);

      printf("%s %.1f\n", timeString, convertedTemp);
      if (logFile) {
        fprintf(logFile, "%s %.1f\n", timeString, convertedTemp);
        fflush(logFile);
      }
    }

    pollAndCheck(pollArr, 1, 0);
    if ((pollArr[0].revents & POLLIN)) {
      char input[100];
      scanf("%s", input);
      processCommand(input);
    }

    if (!paused) {
      usleep(samplingInterval * 1000000);
    }
  }

  mraa_aio_close(tempSensor);
  mraa_gpio_close(button);
  exit(0);
}