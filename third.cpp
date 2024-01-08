#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#ifdef WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../../../usr/include/linux/limits.h"
#include "../../../../usr/include/sys/stat.h"
#endif

#ifdef WIN32
const char* SHM_NAME = "my_shm";
const char* SEM_NAME = "my_shm_sem";
const char* MAIN_PROC_SHM_NAME = "main_proc_shm";
#else
const size_t SIZE = 4096;
const char* SHM_NAME = "/my_shm";
const char* SEM_NAME = "/my_shm_sem";
const char* MAIN_PROC_SHM_NAME = "/main_proc_shm";
#endif

enum class op_type { WRITE, READ, INCREMENT, MULTIPLY, DIVIDE };

FILE* fptr;
int first_child_status = -1;
int second_child_status = -1;
bool exited = false;

int get_pid() {
#ifdef WIN32
  return GetCurrentProcessId();
#else
  return getpid();
#endif
}

std::string time_string() {
  auto now = std::chrono::system_clock::now();
  auto now_t = std::chrono::system_clock::to_time_t(now);
  std::tm now_tm = *std::localtime(&now_t);
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) %
                1000;
  std::stringstream ss;
  ss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0')
     << std::setw(3) << now_ms.count();
  return ss.str();
}

void msleep(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int shmem_op(op_type t, const char* shmem_name, int val = 0) {
  int rval = -1;
  int* ptr;

#ifdef WIN32

  HANDLE hSemaphore;
  HANDLE hMapFile;

  hSemaphore = OpenSemaphore(SEMAPHORE_ALL_ACCESS, FALSE, SEM_NAME);
  if (hSemaphore == NULL) {
    hSemaphore = CreateSemaphore(NULL, 1, 1, SEM_NAME);
    if (hSemaphore == NULL) {
      return 1;
    }

    hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                 sizeof(int), shmem_name);
    if (hMapFile == NULL) {
      CloseHandle(hSemaphore);
      return 1;
    }
  } else {
    hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, shmem_name);
    if (hMapFile == NULL) {
      CloseHandle(hSemaphore);
      return 1;
    }
  }

  ptr = (int*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(int));
  if (ptr == NULL) {
    CloseHandle(hMapFile);
    CloseHandle(hSemaphore);
    return 1;
  }

  WaitForSingleObject(hSemaphore, INFINITE);

#else
  int shm_fd = shm_open(shmem_name, O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("shm_open");
    return 1;
  }

  if (ftruncate(shm_fd, SIZE) == -1) {
    perror("ftruncate");
    return 1;
  }

  ptr = (int*)mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (ptr == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  sem_t* sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
  if (sem == SEM_FAILED) {
    perror("sem_open");
    return 1;
  }

  sem_wait(sem);
#endif

  switch (t) {
    case op_type::READ:
      rval = *ptr;
      break;
    case op_type::WRITE:
      *ptr = val;
      break;
    case op_type::INCREMENT:
      *ptr = *ptr + val;
      break;
    case op_type::MULTIPLY:
      *ptr = *ptr * val;
      break;
    case op_type::DIVIDE:
      *ptr = *ptr / val;
      break;
  }

#ifdef WIN32
  UnmapViewOfFile(ptr);
  ReleaseSemaphore(hSemaphore, 1, NULL);
  CloseHandle(hSemaphore);
#else
  sem_post(sem);
  munmap(ptr, SIZE);
  close(shm_fd);
  sem_close(sem);
#endif

  return rval;
}

bool is_main_proc() {
  int val = shmem_op(op_type::READ, MAIN_PROC_SHM_NAME);
  if (val == 0) {
    shmem_op(op_type::WRITE, MAIN_PROC_SHM_NAME, get_pid());
    return true;
  } else {
    return get_pid() == val;
  }
}

void on_exit() {
  if (exited) return;
  exited = true;
#ifndef WIN32
  if (is_main_proc()) {
    shm_unlink(SHM_NAME);
    shm_unlink(MAIN_PROC_SHM_NAME);
    sem_unlink(SEM_NAME);
  }
#endif
  exit(0);
}

void first_fork_main() {
  fprintf(fptr, "FORK 1 LAUNCH PID: %i TIME: %s\n", get_pid(),
          time_string().c_str());
  shmem_op(op_type::INCREMENT, SHM_NAME, 10);
  fprintf(fptr, "FORK 1 FINISH PID: %i TIME: %s\n", get_pid(),
          time_string().c_str());
  exit(0);
}

void second_fork_main() {
  fprintf(fptr, "FORK 2 LAUNCH PID: %i TIME: %s\n", get_pid(),
          time_string().c_str());
  shmem_op(op_type::MULTIPLY, SHM_NAME, 2);
  msleep(2000);
  shmem_op(op_type::DIVIDE, SHM_NAME, 2);
  fprintf(fptr, "FORK 2 FINISH PID: %i TIME: %s\n", get_pid(),
          time_string().c_str());
  exit(0);
}

void create_fork(int num) {
#ifdef WIN32
  STARTUPINFO si = {sizeof(si)};
  PROCESS_INFORMATION pi;
  TCHAR currentProcessPath[MAX_PATH];
  GetModuleFileName(NULL, currentProcessPath, MAX_PATH);
#ifdef _UNICODE
  std::wstring wstr(currentProcessPath);
  std::string str = std::string(wstr.begin(), wstr.end());
#else
  std::string str = std::string(currentProcessPath);
#endif
  std::string fullCommand = str + " fork_" + std::to_string(num);
  if (CreateProcess(NULL, const_cast<char*>(fullCommand.data()), NULL, NULL,
                    FALSE, 0, NULL, NULL, &si, &pi)) {
    std::thread waiting_thread([pi, num] {
      WaitForSingleObject(pi.hProcess, INFINITE);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      if (num == 1) {
        first_child_status = -1;
      } else {
        second_child_status = -1;
      }
    });
    waiting_thread.detach();
  }
#else
  pid_t pid = fork();
  if (pid == 0) {
    if (num == 1) {
      first_fork_main();
    } else {
      second_fork_main();
    }
  } else {
    std::thread waiting_thread([num, pid] {
      int status;
      waitpid(pid, &status, 0);
      if (num == 1) {
        first_child_status = -1;
      } else {
        second_child_status = -1;
      }
    });
    waiting_thread.detach();
  }
#endif
};

int main(int argc, char* argv[]) {
  atexit(on_exit);
#ifndef WIN32
  signal(SIGINT, [](int _) { on_exit(); });
#endif

  fptr = fopen("log.txt", "a");
  setbuf(fptr, NULL);
  fprintf(fptr, "LAUNCH PID: %i TIME: %s\n", get_pid(), time_string().c_str());
  uint64_t ms_elapsed = 0;

  if (argc > 1) {
#ifdef WIN32
    if (strcmp(argv[1], "fork_1") == 0) {
      first_fork_main();
    } else if (strcmp(argv[1], "fork_2") == 0) {
      second_fork_main();
    } else {
      shmem_op(op_type::WRITE, SHM_NAME, atoi(argv[1]));
    }
#else
    shmem_op(op_type::WRITE, SHM_NAME, atoi(argv[1]));
#endif
  }

  while (true) {
    msleep(100);
    ms_elapsed += 100;
    if (ms_elapsed % 300 == 0) {
      shmem_op(op_type::INCREMENT, SHM_NAME, 1);
    }

    if (ms_elapsed % 1000 == 0) {
      fprintf(fptr, "UPDATE PID: %i TIME: %s, CNT: %i\n", get_pid(),
              time_string().c_str(), shmem_op(op_type::READ, SHM_NAME));
    }

    if (ms_elapsed % 3000 == 0 && is_main_proc()) {
      auto print_failure = [](int num) {
        fprintf(fptr, "WAITING ON FORK %i\n", num);
      };
      if (first_child_status == -1) {
        create_fork(1);
      } else {
        print_failure(1);
      }
      if (second_child_status == -1) {
        create_fork(2);
      } else {
        print_failure(2);
      }
    }
  }
}
