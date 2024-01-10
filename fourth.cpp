#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include <ctime>
#include <fstream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

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
