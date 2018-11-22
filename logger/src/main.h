/**
 * @file main.h
 * @author weichbr
 */

#include <signal.h>

#ifndef SGX_PERF_MAIN_H_H
#define SGX_PERF_MAIN_H_H

void sigint_handler(int signum, siginfo_t *siginfo, void *priv);

#endif //SGX_PERF_MAIN_H_H
