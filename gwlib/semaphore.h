/*
 * semaphore.h - declarations of semaphores
 *
 * Lars Wirzenius
 */


#ifndef SEMAPHORE_H
#define SEMAPHORE_H


typedef struct Semaphore Semaphore;

Semaphore *semaphore_create(long n);
void semaphore_destroy(Semaphore *semaphore);
void semaphore_up(Semaphore *semaphore);
void semaphore_down(Semaphore *semaphore);


#endif
