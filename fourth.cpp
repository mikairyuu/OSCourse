#include <ctime>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#ifdef WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifdef WIN32
const char* portname = "\\\\.\\COM1";
HANDLE hSerial = NULL;

double readTemperature() {
  if (hSerial == NULL) {
    hSerial = CreateFile(portname, GENERIC_READ | GENERIC_WRITE, 0, 0,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (hSerial == INVALID_HANDLE_VALUE) {
      std::cerr << "Error: Unable to open serial port " << portname
                << std::endl;

      return -1.0;
    }

    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
      std::cerr << "Error: Could not get current serial parameters"
                << std::endl;
      CloseHandle(hSerial);
      return -1.0;
    }
    dcbSerialParams.BaudRate = CBR_9600;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) {
      std::cerr << "Error: Could not set serial parameters" << std::endl;
      CloseHandle(hSerial);
      return -1.0;
    }
  }

  char buffer[1];
  DWORD bytesRead;
  std::string line;

  while (ReadFile(hSerial, buffer, 1, &bytesRead, NULL) && bytesRead > 0) {
    if (buffer[0] == '\n') break;
    line += buffer[0];
  }
  
  double temperature = atof(line.c_str());

  return temperature;
}

#else
int fd = 0;
struct termios tty;
const char* portname = "/dev/pts/2";

double readTemperature() {
  if (fd == 0) {
    fd = open(portname, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
      perror("Error opening serial port");
      return errno;
    }
    if (tcgetattr(fd, &tty) != 0) {
      perror("Error from tcgetattr");
      return errno;
    }
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
      perror("Error from tcsetattr");
      return errno;
    }
  }

  char buf[256];
  int n = read(fd, buf, sizeof(buf));
  if (n < 0) {
    perror("Error reading from serial port");
    return errno;
  }

  buf[n] = '\0';

  return atof(buf);
}
#endif
void writeTemperature(std::ofstream& file, double temperature) {
  std::time_t result = std::time(nullptr);
  file << std::asctime(std::localtime(&result)) << " " << temperature
       << std::endl;
}

int main() {
  std::ofstream file1("log_24hours.txt", std::ios_base::app);
  std::ofstream file2("log_1month.txt", std::ios_base::app);
  std::ofstream file3("log_1year.txt", std::ios_base::app);

  std::vector<double> hourlyTemperatures;
  std::vector<double> dailyTemperatures;

  while (true) {
    double temperature = readTemperature();
    printf("Temperature: %f\n", temperature);
    writeTemperature(file1, temperature);

    hourlyTemperatures.push_back(temperature);
    if (hourlyTemperatures.size() == 60) {
      double average = std::accumulate(hourlyTemperatures.begin(),
                                       hourlyTemperatures.end(), 0.0) /
                       hourlyTemperatures.size();
      writeTemperature(file2, average);
      dailyTemperatures.push_back(average);
      hourlyTemperatures.clear();
    }

    if (dailyTemperatures.size() == 24) {
      double average = std::accumulate(dailyTemperatures.begin(),
                                       dailyTemperatures.end(), 0.0) /
                       dailyTemperatures.size();
      writeTemperature(file3, average);
      dailyTemperatures.clear();
    }
  }

  return 0;
}
