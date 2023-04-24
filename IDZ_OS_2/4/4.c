#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define SINGLE_ROOMS 10
#define DOUBLE_ROOMS 15
#define TOTAL_ROOMS (SINGLE_ROOMS + DOUBLE_ROOMS)

typedef struct {
  int single_rooms;
  int double_rooms;
  int single_occupants;
  int double_occupants;
} hotel_status;

static hotel_status *hotel;
static sem_t *sem_single, *sem_double, *sem_mutex;
static bool should_exit = false;

void signal_handler(int signal) { should_exit = true; }

void init_shared_memory() {
  int shm_fd = shm_open("hotel_shm", O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("shm_open");
    exit(1);
  }

  if (ftruncate(shm_fd, sizeof(hotel_status)) == -1) {
    perror("ftruncate");
    exit(1);
  }

  hotel = (hotel_status *)mmap(NULL, sizeof(hotel_status),
                               PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (hotel == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }

  close(shm_fd);
}

void init_semaphores() {
  sem_single = sem_open("sem_single", O_CREAT, 0666, SINGLE_ROOMS);
  sem_double = sem_open("sem_double", O_CREAT, 0666, DOUBLE_ROOMS);
  sem_mutex = sem_open("sem_mutex", O_CREAT, 0666, 1);
}

void destroy_shared_memory() {
  munmap(hotel, sizeof(hotel_status));
  shm_unlink("hotel_shm");
}

void destroy_semaphores() {
  sem_close(sem_single);
  sem_close(sem_double);
  sem_close(sem_mutex);
  sem_unlink("sem_single");
  sem_unlink("sem_double");
  sem_unlink("sem_mutex");
}

void customer(bool is_male) {
  while (!should_exit) {
    usleep(rand() % 2000000 + 500000);

    if (is_male) {
      if (sem_trywait(sem_single) == 0) {
        sem_wait(sem_mutex);
        hotel->single_rooms--;
        hotel->single_occupants++;
        printf("Мужчина заселяется в одноместный номер. Оставшиеся номера: %d "
               "одноместных, %d двухместных\n",
               hotel->single_rooms, hotel->double_rooms);
        sem_post(sem_mutex);
        usleep(rand() % 2000000 + 500000);
        sem_wait(sem_mutex);
        hotel->single_rooms++;
        hotel->single_occupants--;
        printf("Мужчина освобождает одноместный номер.\n");
        sem_post(sem_mutex);
        sem_post(sem_single);
      } else if (sem_trywait(sem_double) == 0) {
        sem_wait(sem_mutex);
        hotel->double_rooms--;
        hotel->double_occupants++;
        printf("Мужчина заселяется в двухместный номер. Оставшиеся номера: %d "
               "одноместных, %d двухместных\n",
               hotel->single_rooms, hotel->double_rooms);
        sem_post(sem_mutex);
        usleep(rand() % 2000000 + 500000);
        sem_wait(sem_mutex);
        hotel->double_rooms++;
        hotel->double_occupants--;
        printf("Мужчина освобождает двухместный номер.\n");
        sem_post(sem_mutex);
        sem_post(sem_double);
      } else {
        printf("Мужчина уходит, нет свободных номеров.\n");
      }
    } else {
      if (sem_trywait(sem_double) == 0) {
        sem_wait(sem_mutex);
        hotel->double_rooms--;
        hotel->double_occupants++;
        printf("Женщина заселяется в двухместный номер. Оставшиеся номера: %d "
               "одноместных, %d двухместных\n",
               hotel->single_rooms, hotel->double_rooms);
        sem_post(sem_mutex);
        usleep(rand() % 2000000 + 500000);
        sem_wait(sem_mutex);
        hotel->double_rooms++;
        hotel->double_occupants--;
        printf("Женщина освобождает двухместный номер.\n");
        sem_post(sem_mutex);
        sem_post(sem_double);
      } else {
        printf("Женщина уходит, нет свободных номеров.\n");
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <num_clients>\n", argv[0]);
    exit(1);
  }

  int num_clients = atoi(argv[1]);

  if (num_clients < 1) {
    fprintf(stderr, "Number of clients must be greater than 0\n");
    exit(1);
  }

  signal(SIGINT, signal_handler);
  init_shared_memory();
  init_semaphores();

  hotel->single_rooms = SINGLE_ROOMS;
  hotel->double_rooms = DOUBLE_ROOMS;
  hotel->single_occupants = 0;
  hotel->double_occupants = 0;

  pid_t *pids = malloc(num_clients * sizeof(pid_t));

  for (int i = 0; i < num_clients; i++) {
    pids[i] = fork();
    if (pids[i] == 0) {
      srand(getpid());
      customer(i % 2 == 0);
      exit(0);
    }
  }

  while (!should_exit) {
    usleep(500000);
  }

  for (int i = 0; i < num_clients; i++) {
    kill(pids[i], SIGINT);
  }

  for (int i = 0; i < num_clients; i++) {
    wait(NULL);
  }

  free(pids);
  destroy_shared_memory();
  destroy_semaphores();

  return 0;
}