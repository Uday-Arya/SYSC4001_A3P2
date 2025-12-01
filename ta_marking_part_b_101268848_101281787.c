// ta_marking_partb_101268848_101281787.c
// Part 2.b
// Compile: gcc -std=c99 -O2 -o ta_marking_sem ta_marking_sem.c -lrt -pthread
// Run: ./ta_marking_sem <num_TAs>=n >=2 <exams_dir> <rubric_file>


/*

Authors: Uday Arya, Qurb E Muhammad Syed
Student Numbers: 101268848, 101281787
  
*/




#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/wait.h>

#define SHM_NAME "/ta_shm_sem_v1"
#define MAX_EXAMS 256
#define MAX_PATH 256
#define N_RUBRIC 5

typedef struct {
    int index_exam;                 // index of current exam (0..)
    int student_num;             // current exam student number
    int marked[N_RUBRIC];           // 0 = unmarked, 1 = marked
    char rubric[N_RUBRIC];          // rubric char for each question
    int total_exams;                // total exam files
    char exam_files[MAX_EXAMS][MAX_PATH]; // exam filenames
} shared_t;

shared_t *shm = NULL;
int shm_fd = -1;

// Named semaphores
sem_t *sem_rubric = NULL; // for exclusive rubric writing
sem_t *sem_mark = NULL;   // for exclusive marking selection/update
sem_t *sem_load = NULL;   // for exclusive loading next exam

const char *SEM_RUBRIC_NAME = "/sem_ta_rubric_v1";
const char *SEM_MARK_NAME   = "/sem_ta_mark_v1";
const char *SEM_LOAD_NAME   = "/sem_ta_load_v1";

void cleanup_resources_parent() {
    if (shm) {
        munmap(shm, sizeof(shared_t));
        shm = NULL;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
    shm_unlink(SHM_NAME);

    if (sem_rubric) { sem_close(sem_rubric); sem_unlink(SEM_RUBRIC_NAME); sem_rubric = NULL; }
    if (sem_mark)   { sem_close(sem_mark);   sem_unlink(SEM_MARK_NAME);   sem_mark = NULL; }
    if (sem_load)   { sem_close(sem_load);   sem_unlink(SEM_LOAD_NAME);   sem_load = NULL; }
}

void cleanup_and_exit(int sig) {
    (void)sig;
    cleanup_resources_parent();
    exit(0);
}

double rand_between(double a, double b) {
    return a + (b - a) * (rand() / (double)RAND_MAX);
}

void save_rubric_to_file(const char *fname) {
    FILE *f = fopen(fname, "w");
    if (!f) {
        fprintf(stderr, "[pid %d] ERROR: cannot open rubric file %s: %s\n", getpid(), fname, strerror(errno));
        return;
    }
    for (int i = 0; i < N_RUBRIC; ++i) {
        // write "1, A\n" style
        fprintf(f, "%d, %c\n", i+1, shm->rubric[i]);
    }
    fclose(f);
}

void load_exam_to_shm(int idx) {
    if (idx < 0 || idx >= shm->total_exams) return;
    const char *fname = shm->exam_files[idx];
    FILE *f = fopen(fname, "r");
    if (!f) {
        fprintf(stderr, "[pid %d] ERROR: cannot open exam file %s: %s\n", getpid(), fname, strerror(errno));
        shm->student_num = 9999; // fail-safe terminate
        return;
    }
    int stud = -1;
    if (fscanf(f, "%d", &stud) != 1) {
        fprintf(stderr, "[pid %d] ERROR: parse error in %s\n", getpid(), fname);
        shm->student_num = 9999;
        fclose(f);
        return;
    }
    fclose(f);
    shm->student_num = stud;
    for (int i = 0; i < N_RUBRIC; ++i) shm->marked[i] = 0;
    shm->index_exam = idx;
    printf("[pid %d] loaded exam '%s' (student %04d) into shared memory\n", getpid(), fname, shm->student_num);
}

void ta_process_loop(int ta_id, const char *rubric_fname) {
    // change randomness per process
    srand(time(NULL) ^ (getpid() << 8));

    while (1) {
        int stud = shm->student_num;
        if (stud == 9999) {
            printf("[TA %d - pid %d] saw student 9999 -> exiting\n", ta_id, getpid());
            break;
        }

        // Read rubric concurrently and keep printing for visibility
        printf("[TA %d - pid %d] reading rubric: ", ta_id, getpid());
        for (int i = 0; i < N_RUBRIC; ++i) printf("%c ", shm->rubric[i]);
        printf("\n");

        // For each rubric line check if correction needed (0.5 - 1.0s each)
        for (int i = 0; i < N_RUBRIC; ++i) {
            double d = rand_between(0.5, 1.0);
            usleep((useconds_t)(d * 1e6));
            int decide = rand() % 2; // 50% chance
            if (decide) {
                // Acquire rubric writer lock (only one TA can modify/write at a time)
                sem_wait(sem_rubric);
                char old = shm->rubric[i];
                char next = old + 1;
                shm->rubric[i] = next;
                printf("[TA %d - pid %d] updating rubric line %d: %c -> %c (writing file)\n",
                       ta_id, getpid(), i+1, old, next);
                save_rubric_to_file(rubric_fname);
                sem_post(sem_rubric);
            } else {
                printf("[TA %d - pid %d] decided rubric line %d ok (%c)\n",
                       ta_id, getpid(), i+1, shm->rubric[i]);
            }
        }

        // Marking: pick an unmarked question under protection to avoid races
        int picked = -1;
        sem_wait(sem_mark);
        for (int i = 0; i < N_RUBRIC; ++i) {
            if (shm->marked[i] == 0) { picked = i; shm->marked[i] = 2; /* 2 = being marked */ break; }
        }
        sem_post(sem_mark);

        if (picked != -1) {
            double mark_time = rand_between(1.0, 2.0);
            printf("[TA %d - pid %d] is marking student %04d question %d (%.2fs)\n",
                   ta_id, getpid(), shm->student_num, picked+1, mark_time);
            usleep((useconds_t)(mark_time * 1e6));
            // finalize marking
            sem_wait(sem_mark);
            shm->marked[picked] = 1;
            sem_post(sem_mark);
            printf("[TA %d - pid %d] marked student %04d question %d\n",
                   ta_id, getpid(), shm->student_num, picked+1);
        } else {
            // all questions might be already marked; check and load next exam if done
            int all_marked = 1;
            sem_wait(sem_mark);
            for (int i = 0; i < N_RUBRIC; ++i) if (shm->marked[i] == 0) { all_marked = 0; break; }
            sem_post(sem_mark);

            if (all_marked) {
                // Only one should load the next exam at a time
                if (sem_trywait(sem_load) == 0) {
                    // we got loader privilege
                    int next_idx = shm->index_exam + 1;
                    if (next_idx >= shm->total_exams) {
                        // set termination sentinel
                        printf("[TA %d - pid %d] no more exams -> setting student 9999 and exiting\n", ta_id, getpid());
                        shm->student_num = 9999;
                        sem_post(sem_load);
                        break;
                    } else {
                        printf("[TA %d - pid %d] loading next exam index %d\n", ta_id, getpid(), next_idx);
                        load_exam_to_shm(next_idx);
                        sem_post(sem_load);
                    }
                } else {
                    // someone else is loading; wait a bit
                    usleep(100000);
                }
            } else {
                // some questions unmarked but couldn't grab one; small wait then retry
                usleep(100000);
            }
        }
        // small random sleep to avoid busy loop
        usleep(50000);
    }

    _exit(0);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <num_TAs>=n >=2 <exams_dir> <rubric_file>\n", argv[0]);
        fprintf(stderr, "Example: %s 3 exams rubric.txt\n", argv[0]);
        return 1;
    }

    int n_tas = atoi(argv[1]);
    if (n_tas < 2) n_tas = 2;
    const char *exams_dir = argv[2];
    const char *rubric_fname = argv[3];

    // Setup signal handling for cleanup
    signal(SIGINT, cleanup_and_exit);
    signal(SIGTERM, cleanup_and_exit);

    // create/open shared memory
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) { perror("shm_open"); return 1; }
    if (ftruncate(shm_fd, sizeof(shared_t)) == -1) { perror("ftruncate"); shm_unlink(SHM_NAME); return 1; }
    shm = mmap(NULL, sizeof(shared_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); shm_unlink(SHM_NAME); return 1; }

    // setup semaphores (parent creates and initializes)
    sem_unlink(SEM_RUBRIC_NAME); sem_unlink(SEM_MARK_NAME); sem_unlink(SEM_LOAD_NAME);
    sem_rubric = sem_open(SEM
