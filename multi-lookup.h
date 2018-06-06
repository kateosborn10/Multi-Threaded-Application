
#ifndef MULTI_H
#define MULTI_H



#include "util.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/time.h>


void* request(void* filename);
void* resolve(void* filename);

#endif
