#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdlib> 
#include <ctime>

int main() {
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
    
    srand(time(0));
    
    int temperature = 20;

    while (true) {
        int fluctuation = rand() % 5 - 2;
        
        temperature += fluctuation;
        
        if (temperature < 0) temperature = 0;
        if (temperature > 40) temperature = 40;
        
        std::string message = std::to_string(temperature) + "\n";
        
        write(fd, message.c_str(), message.size());
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    close(fd);

    return 0;
}
