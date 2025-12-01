// ta_marking_part_a_101268848_101281787.c
// Part 2.a
// Compile: gcc -std=c99 -O2 -lrt -pthread -o ta_marking ta_marking.c

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
#include <sys/wait.h>
#include <signal.h>

#define SHM_NAME "/ta_shared_mem_v1"
#define MAX_EXAMS 256
#define MAX_PATH 256
#define N_RUBRIC 5

typedef struct {
    int exam_index;                 // index of current exam (0..)
    int student_num;             // current exam student number
    int marked[N_RUBRIC];           // 0 = unmarked, 1 = marked
    char rubric[N_RUBRIC];          // first character after comma for each rubric line (eg 'A','B'..)
    int total_exams;                // total exam files available
    char exam_files[MAX_EXAMS][MAX_PATH]; // names of exam files
    int load_flag;                  // simple flag to indicate "someone is loading" (not used for sync here)
} shared_t;

shared_t *shm = NULL;
int shm_fd = -1;

void cleanup_shm() {
    if (shm) {
        munmap(shm, sizeof(shared_t));
        shm = NULL;
    }
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
    // Parent should unlink
    shm_unlink(SHM_NAME);
}

void sigint_handler(int sig) {
    (void)sig;
    cleanup_shm();
    exit(0);
}

double random_between(double a, double b) {
    return a + (b - a) * (rand() / (double)RAND_MAX);
}

void save_rubric_to_file(const char *rubric_fname) {
    FILE *f = fopen(rubric_fname, "w");
    if (!f) {
        fprintf(stderr, "[%d] Error opening rubric file for write: %s\n", getpid(), strerror(errno));
        return;
    }
    for (int i = 0; i < N_RUBRIC; ++i) {
        // Write format: "1, A\n"
        fprintf(f, "%d, %c\n", i+1, shm->rubric[i]);
    }
    fclose(f);
}

void load_exam_into_shm(int idx) {
    if (idx < 0 || idx >= shm->total_exams) return;
    const char *fname = shm->exam_files[idx];
    FILE *f = fopen(fname, "r");
    if (!f) {
        fprintf(stderr, "[%d] Could not open exam file %s : %s\n", getpid(), fname, strerror(errno));
        // If it can't open, mark as student 9999 to terminate (fail-safe to avoid infinite loop)
        shm->student_num = 9999;
        return;
    }
    int stud = -1;
    if (fscanf(f, "%d", &stud) != 1) {
        fprintf(stderr, "[%d] parse error reading student number from %s\n", getpid(), fname);
        fclose(f);
        shm->student_num = 9999;
        return;
    }
    fclose(f);
    shm->student_num = stud;
    for (int i=0;i<N_RUBRIC;i++) shm->marked[i] = 0;
    shm->exam_index = idx;
    printf("[%d] loaded exam '%s' (student %04d) into shared memory\n", getpid(), fname, shm->student_num);
}

void ta_main_loop(int ta_id, const char *rubric_fname) {
    while (1) {
        int stud = shm->student_num;
        if (stud == 9999) {
            printf("[TA %d - pid %d] saw student 9999 -> exiting\n", ta_id, getpid());
            break;
        }

        // Read rubric from shared memory (shared read)
        printf("[TA %d - pid %d] reading rubric: ", ta_id, getpid());
        for (int i=0;i<N_RUBRIC;i++) printf("%c ", shm->rubric[i]);
        printf("\n");

        // Iterate through rubric lines and randomly decide to correct (0.5 - 1.0s delay each decision)
        for (int i = 0; i < N_RUBRIC; ++i) {
            double d = random_between(0.5, 1.0);
            usleep((useconds_t)(d * 1e6));
            int decide = rand() % 2; // 50% chance to "need change"
            if (decide) {
                char old = shm->rubric[i];
                char next = old + 1;
                shm->rubric[i] = next;
                printf("[TA %d - pid %d] changed rubric line %d: %c -> %c (writing file)\n",
                       ta_id, getpid(), i+1, old, next);
                // write to rubric file (racy in this part a)
                save_rubric_to_file(rubric_fname);
            } else {
                printf("[TA %d - pid %d] decided rubric line %d ok (%c)\n", ta_id, getpid(), i+1, shm->rubric[i]);
            }
        }

        // TAs should try to pick unmarked questions randomly
        int any_unmarked = 0;
        for (int attempt = 0; attempt < 100; ++attempt) {
            int pick = rand() % N_RUBRIC;
            if (shm->marked[pick] == 0) {
                // simulate marking time 1.0 - 2.0s
                double dt = random_between(1.0, 2.0);
                printf("[TA %d - pid %d] is marking student %04d question %d (will take %.2fs)\n",
                       ta_id, getpid(), shm->student_num, pick+1, dt);
                usleep((useconds_t)(dt * 1e6));
                // Mark it
                shm->marked[pick] = 1;
                printf("[TA %d - pid %d] marked student %04d question %d\n",
                       ta_id, getpid(), shm->student_num, pick+1);
                any_unmarked = 1;
                break;
            } else {
                // small pause then try another pick
                usleep(100000);
            }
        }
        if (!any_unmarked) {
            // No unmarked questions found -> either all done or racing; check
            int all_marked = 1;
            for (int i = 0; i < N_RUBRIC; ++i) if (shm->marked[i] == 0) { all_marked = 0; break; }
            if (all_marked) {
                // attempt to load next exam (race allowed)
                int next_idx = shm->exam_index + 1;
                if (next_idx >= shm->total_exams) {
                    // No more exams; set termination student 9999 to stop everyone
                    printf("[TA %d - pid %d] no more exams -> setting student 9999 and exiting\n", ta_id, getpid());
                    shm->student_num = 9999;
                    break;
                } else {
                    printf("[TA %d - pid %d] all questions done for current exam -> loading exam index %d\n",
                           ta_id, getpid(), next_idx);
                    load_exam_into_shm(next_idx);
                    // if loaded student is 9999, loop will exit at top
                }
            } else {
                // some were unmarked but this TA couldn't get one due to races; small sleep and retry
                usleep(200000);
            }
        }
        // small random sleep before next cycle
        usleep(100000);
    }
    // exit TA process
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

    // Seed randomness in each process
    srand(time(NULL) ^ (getpid()<<16));

    // Create or open SHM
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return 1;
    }
    if (ftruncate(shm_fd, sizeof(shared_t)) == -1) {
        perror("ftruncate");
        shm_unlink(SHM_NAME);
        return 1;
    }
    shm = mmap(NULL, sizeof(shared_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) {
        perror("mmap");
        shm_unlink(SHM_NAME);
        return 1;
    }

    // Setup SIGINT handler to clean shm on ctrl-c
    signal(SIGINT, sigint_handler);

    // Initialize shared memory: load list of exams from directory (simple listing sequence)

    shm->total_exams = 0;
    for (int i = 1; i <= 500 && shm->total_exams < MAX_EXAMS; ++i) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s/exam%02d.txt", exams_dir, i);
        if (access(path, R_OK) == 0) {
            strncpy(shm->exam_files[shm->total_exams], path, MAX_PATH-1);
            shm->exam_files[shm->total_exams][MAX_PATH-1] = '\0';
            shm->total_exams++;
        } else {
            // stop when no file
            // continue searching; 
        }
    }
    if (shm->total_exams == 0) {
        fprintf(stderr, "No exam files found in %s matching examNN.txt pattern.\n", exams_dir);
        cleanup_shm();
        return 1;
    }

    // Load rubric file into shared memory
    FILE *rf = fopen(rubric_fname, "r");
    if (!rf) {
        fprintf(stderr, "Could not open rubric file %s: %s\n", rubric_fname, strerror(errno));
        cleanup_shm();
        return 1;
    }
    char line[256];
    int idx = 0;
    while (fgets(line, sizeof(line), rf) && idx < N_RUBRIC) {
        // expected format: "1, A" or "2, B"
        char *comma = strchr(line, ',');
        if (comma && *(comma+1) != '\0') {
            // skip spaces
            char *p = comma + 1;
            while (*p == ' ' || *p == '\t') p++;
            shm->rubric[idx] = *p ? *p : 'A';
        } else {
            shm->rubric[idx] = 'A' + idx;
        }
        idx++;
    }
    fclose(rf);
    // fill remainder if any
    for (int i = idx; i < N_RUBRIC; ++i) shm->rubric[i] = 'A' + i;

    // Load first exam into shared memory
    load_exam_into_shm(0);

    // Fork n_tas - 1 children. Parent also acts as TA.
    pid_t *kids = calloc(n_tas-1, sizeof(pid_t));
    for (int i = 1; i < n_tas; ++i) {
        pid_t p = fork();
        if (p < 0) {
            perror("fork");
            // continue; try to spawn remaining
        } else if (p == 0) {
            // child executes TA main loop
            // reseed randomness for child
            srand(time(NULL) ^ (getpid()<<8));
            ta_main_loop(i, rubric_fname);
            // should never reach
            _exit(0);
        } else {
            kids[i-1] = p;
        }
    }

    // parent acts as TA id 0
    ta_main_loop(0, rubric_fname);

    // parent cleanup and wait children
    for (int i = 0; i < n_tas - 1; ++i) {
        if (kids[i] > 0) waitpid(kids[i], NULL, 0);
    }
    free(kids);
    cleanup_shm();
    return 0;
}
