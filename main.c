#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <math.h>

#define MAX_QUEUE_SIZE 700
#define NUM_PRODUCERS 4
#define TASKS_PER_PRODUCER 500

struct workFunction {
    void *(*work)(void *);
    void *arg;
    struct timeval enque_time;
};

// Sychronization
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

// FIFO queue
struct workFunction queue[MAX_QUEUE_SIZE];
int front = 0, rear = 0, queue_count = 0;

// To know when the producers are finished
int producers_done = 0;

// Statistics
long total_wait_time = 0;
long total_exec_time = 0;
long total_tasks = 0;

const int RUNS = 300;  // Number of runs for better statistics

void *sin_work(void *arg) {
    double *angles = (double *)arg;
    for (int i = 0; i < 10; i++) {
        double result = sin(angles[i]);
    }
    return NULL;
}

// FIFO Push
void enqueue(struct workFunction item) {
    queue[rear] = item;
    rear = (rear + 1) % MAX_QUEUE_SIZE;
    queue_count++;
}

// FIFO Pop
struct workFunction dequeue() {
    struct workFunction item = queue[front];
    front = (front + 1) % MAX_QUEUE_SIZE;
    queue_count--;
    return item;
}

void *producer(void *arg) {
    for (int i = 0; i < TASKS_PER_PRODUCER; i++) {
        struct workFunction work;
        work.work = sin_work;

        // Allocate an array of 10 doubles to calculate for 10 different angles
        double *angles = malloc(10 * sizeof(double));

        // Fill the array with 10 different angles
        for (int j = 0; j < 10; j++) {
            angles[j] = (double)((i + j) * M_PI / 180.0);  // Incrementing angles
        }

        work.arg = angles;  // Pass the array to sin_work
        gettimeofday(&work.enque_time, NULL);

        pthread_mutex_lock(&queue_mutex);
        while (queue_count == MAX_QUEUE_SIZE) {
            pthread_cond_wait(&queue_not_full, &queue_mutex);
        }
        enqueue(work);
        pthread_cond_signal(&queue_not_empty);
        pthread_mutex_unlock(&queue_mutex);
    }

    // Signal completion
    pthread_mutex_lock(&queue_mutex);
    producers_done++;
    pthread_cond_broadcast(&queue_not_empty);
    pthread_mutex_unlock(&queue_mutex);
    return NULL;
}

void *consumer(void *arg) {
    while (1) {
        pthread_mutex_lock(&queue_mutex);

        while (queue_count == 0 && producers_done < NUM_PRODUCERS) {
            pthread_cond_wait(&queue_not_empty, &queue_mutex);
        }

        if (queue_count == 0 && producers_done == NUM_PRODUCERS) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }

        struct workFunction work = dequeue();
        pthread_cond_signal(&queue_not_full);
        pthread_mutex_unlock(&queue_mutex);

        struct timeval start, end;
        gettimeofday(&start, NULL);

        long wait_time = (start.tv_sec - work.enque_time.tv_sec) * 1000000L +
                         (start.tv_usec - work.enque_time.tv_usec);

        work.work(work.arg);

        gettimeofday(&end, NULL);
        long exec_time = (end.tv_sec - start.tv_sec) * 1000000L +
                         (end.tv_usec - start.tv_usec);

        pthread_mutex_lock(&queue_mutex);
        total_wait_time += wait_time;
        total_exec_time += exec_time;
        total_tasks++;
        pthread_mutex_unlock(&queue_mutex);

        free(work.arg);
    }

    return NULL;
}

int main() {
    pthread_t producers[NUM_PRODUCERS];
    pthread_t *consumers;

    // Opens a CSV file to store the results
    FILE *fp = fopen("average_wait_time.csv", "w");
    if (fp == NULL) {
        perror("It's not possible to create the file");
        return -1;
    }

    // Header for CSV
    fprintf(fp, "NumConsumers,AvgWaitTime_us\n");

    // Trials for different number of producers
    for (int num_consumers = 1; num_consumers <= 100; num_consumers+=2) {
        consumers = malloc(sizeof(pthread_t) * num_consumers);  // Creation of consumer matrix

        double final_avg = 0.0;

        // Run the program for the current number of producers,
        for (int r = 0; r < RUNS; r++) {
            // Reset of the statistics for every run
            total_wait_time = 0;
            total_exec_time = 0;
            total_tasks = 0;
            producers_done = 0;

            // Creation of producers
            for (int i = 0; i < NUM_PRODUCERS; i++) {
                pthread_create(&producers[i], NULL, producer, NULL);
            }

            // Creation of Consumers
            for (int i = 0; i < num_consumers; i++) {
                pthread_create(&consumers[i], NULL, consumer, NULL);
            }

            // Waiting for the producer threads to finish
            for (int i = 0; i < NUM_PRODUCERS; i++) {
                pthread_join(producers[i], NULL);
            }

            // Waiting for the consumer threads to finish
            for (int i = 0; i < num_consumers; i++) {
                pthread_join(consumers[i], NULL);
            }

            // Calculate the mean waiting time for this run
            double avg_wait_time = total_tasks > 0 ? (double)total_wait_time / total_tasks : 0;
            final_avg += avg_wait_time;
        }

        // Calculation of the mean waiting time for the current number of consumers
        final_avg /= RUNS;

        // Write the results to the CSV file
        fprintf(fp, "%d,%.2f\n", num_consumers, final_avg);

        // Free the allocated memory of the consmers
        free(consumers);
    }

    fclose(fp);
    printf("The results have been written to  average_wait_time.csv\n");

    return 0;
}
