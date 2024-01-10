#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <thread>

#ifdef WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

int main() {
#ifdef WIN32
  const char* portname = "\\\\.\\COM2";
  HANDLE hSerial = CreateFile(portname, GENERIC_READ | GENERIC_WRITE, 0, 0,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

  if (hSerial == INVALID_HANDLE_VALUE) {
    std::cerr << "Error: Unable to open serial port " << portname << std::endl;
  }

  DCB dcbSerialParams = {0};
  dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
  if (!GetCommState(hSerial, &dcbSerialParams)) {
    std::cerr << "Error: Could not get current serial parameters" << std::endl;
    CloseHandle(hSerial);
  }
  dcbSerialParams.BaudRate = CBR_9600;
  dcbSerialParams.ByteSize = 8;
  dcbSerialParams.StopBits = ONESTOPBIT;
  dcbSerialParams.Parity = NOPARITY;
  if (!SetCommState(hSerial, &dcbSerialParams)) {
    std::cerr << "Error: Could not set serial parameters" << std::endl;
    CloseHandle(hSerial);
  }
  DWORD dwBytesWritten;
#else
  int fd = open("/dev/pts/3", O_RDWR | O_NOCTTY);

  if (fd == -1) {
    std::cerr << "Unable to open port\n";
    return 1;
  }

  struct termios options;
  tcgetattr(fd, &options);
  cfsetispeed(&options, B9600);
  cfsetospeed(&options, B9600);
  tcsetattr(fd, TCSANOW, &options);

#endif

  srand(time(0));

  int temperature = 20;

  while (true) {
    int fluctuation = rand() % 5 - 2;

    temperature += fluctuation;

    if (temperature < 0) temperature = 0;
    if (temperature > 40) temperature = 40;

    std::string message = std::to_string(temperature) + "\n";
    printf("Temperature: %s", message.c_str());
#ifdef WIN32
    if (!WriteFile(hSerial, message.c_str(), message.size(), &dwBytesWritten,
                   NULL)) {
      std::cerr << "Error: Could not write to serial port" << std::endl;
      CloseHandle(hSerial);
    }
#else
    write(fd, message.c_str(), message.size());
#endif

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}
