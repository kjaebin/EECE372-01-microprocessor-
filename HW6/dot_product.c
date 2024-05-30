#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "omp.h"

#define ARRAY_SIZE 1000000

double dotp(double *x, double *y);
double dotp_omp(double *x, double *y);

int main() {
    double error = 0;
    double global_sum = 0;
    double global_sum_ref = 0;
    double *x = malloc(sizeof(double) * ARRAY_SIZE);
    double *y = malloc(sizeof(double) * ARRAY_SIZE);

    clock_t start, end;

    srand((unsigned)time(NULL));
    for (int i = 0; i < ARRAY_SIZE; i++) {
        x[i] = rand() % 10 + 1;
        y[i] = rand() % 10 + 1;
    }

    start = clock();
    global_sum = dotp(x, y);
    printf("Execution time (dot product)    : %7.3lf[ms]\n", ((double)clock() - start) / ((double)CLOCKS_PER_SEC / 1000));

    start = clock();
    global_sum = dotp_omp(x, y);
    printf("Execution time (dot product omp): %7.3lf[ms]\n", ((double)clock() - start) / ((double)CLOCKS_PER_SEC / 1000));

    for (int i = 0; i < ARRAY_SIZE; i++) {
        global_sum_ref += x[i] * y[i];
    }

    error = global_sum - global_sum_ref;
    if (error == 0) {
        printf("PASS\n");
    }
    else {
        printf("FAIL\n");
    }

    free(x);
    free(y);
    return 0;
}

double dotp(double *x, double *y) {
    double global_sum = 0.0;

    for (int i = 0; i < ARRAY_SIZE; i++) {
        global_sum += x[i] * y[i];
    }

    return global_sum;
}

double dotp_omp(double *x, double *y) {
    omp_set_num_threads(6);

    double global_sum = 0.0;

#pragma omp parallel for reduction(+:global_sum)
    for (int i = 0; i < ARRAY_SIZE; i++) {
        global_sum += x[i] * y[i];
    }

    return global_sum;
}