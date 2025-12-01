# SYSC4001_A3P2

This project implements a multi-process TA exam-marking system using shared memory.
Part 2(a) provides a version without synchronization, and Part 2(b) introduces semaphore-based coordination.
Both programs simulate multiple TAs concurrently updating a rubric, marking exam questions, and loading new exam files until the sentinel student number 9999 is reached.


Compilation:

Part 2.a (Unsynchronized Version):
gcc -std=c99 -O2 -lrt -pthread \
    -o ta_marking_part_a ta_marking_part_a_101268848_101281787.c

Part 2.b (Semaphore-Based Version):
gcc -std=c99 -O2 -lrt -pthread \
    -o ta_marking_part_b ta_marking_part_b_101268848_101281787.c


