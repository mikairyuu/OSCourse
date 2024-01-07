#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <string>
#include <thread>

#ifdef WIN32

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
#include <csignal>
#include <cstring>

const char* SHM_NAME = "/my_shm";
const char* SEM_NAME = "/my_shm_sem";
const char* MAIN_PROC_SHM_NAME = "/main_proc_shm";
const size_t SIZE = 4096;

enum class op_type { WRITE, READ, INCREMENT, MULTIPLY, DIVIDE };

int get_pid() {
#ifdef WIN32
  return 0;  // TODO
#else
  return getpid();
#endif
}

void msleep(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int shmem_op(op_type t, const char* shmem_name, int val = 0) {
  int rval = -1;

  int shm_fd = shm_open(shmem_name, O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("shm_open");
    return 1;
  }

  if (ftruncate(shm_fd, SIZE) == -1) {
    perror("ftruncate");
    return 1;
  }

  int* ptr = (int*)mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
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

  sem_post(sem);

  munmap(ptr, SIZE);
  close(shm_fd);
  sem_close(sem);

  return rval;
}

FILE* fptr;
int first_child_status = -1;
int second_child_status = -1;
bool exited = false;

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
  if (is_main_proc()) {
    shm_unlink(SHM_NAME);
    shm_unlink(MAIN_PROC_SHM_NAME);
    sem_unlink(SEM_NAME);
  }
  exit(0);
}

int main(int argc, char* argv[]) {
  atexit(on_exit);
#ifndef WIN32
  signal(SIGINT, [](int _) { on_exit(); });
#endif

  fptr = fopen("log.txt", "a");
  setbuf(fptr, NULL);
  fprintf(fptr, "LAUNCH PID: %i TS: %lu\n", get_pid(),
          (unsigned long)time(NULL));
  uint64_t ms_elapsed = 0;

  if (argc > 1) {
    shmem_op(op_type::WRITE, SHM_NAME, atoi(argv[1]));
  }

  while (true) {
    msleep(100);
    ms_elapsed += 100;
    if (ms_elapsed % 300 == 0) {
      shmem_op(op_type::INCREMENT, SHM_NAME, 1);
    }

    if (ms_elapsed % 1000 == 0) {
      fprintf(fptr, "UPDATE PID: %i TS: %lu, CNT: %i\n", get_pid(),
              (unsigned long)time(NULL), shmem_op(op_type::READ, SHM_NAME));
    }
    
    if (ms_elapsed % 3000 == 0 && is_main_proc()) {
      auto print_failure = [](int num) { printf("WAITING ON FORK %i\n", num); };
      if (first_child_status == -1) {
        pid_t pid = fork();
        if (pid == 0) {
          fprintf(fptr, "FORK 1 LAUNCH PID: %i TS: %lu\n", get_pid(),
                  (unsigned long)time(NULL));
          shmem_op(op_type::INCREMENT, SHM_NAME, 10);
          fprintf(fptr, "FORK 1 FINISH PID: %i TS: %lu\n", get_pid(),
                  (unsigned long)time(NULL));
          exit(0);
        } else {
          std::thread waiting_thread([pid] {
            int status;
            waitpid(pid, &status, 0);
            first_child_status = -1;
          });
          waiting_thread.detach();
        }
      } else {
        print_failure(1);
      }
      if (second_child_status == -1) {
        pid_t pid = fork();
        if (pid == 0) {
          fprintf(fptr, "FORK 2 LAUNCH PID: %i TS: %lu\n", get_pid(),
                  (unsigned long)time(NULL));
          shmem_op(op_type::MULTIPLY, SHM_NAME, 2);
          msleep(2000);
          shmem_op(op_type::DIVIDE, SHM_NAME, 2);
          fprintf(fptr, "FORK 2 FINISH PID: %i TS: %lu\n", get_pid(),
                  (unsigned long)time(NULL));
          exit(0);
        } else {
          std::thread waiting_thread([pid] {
            int status;
            waitpid(pid, &status, 0);
            second_child_status = -1;
          });
          waiting_thread.detach();
        }
      } else {
        print_failure(2);
      }
    }
  }
}
