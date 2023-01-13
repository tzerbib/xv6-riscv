#ifndef __COMM_H__
#define __COMM_H__

#include <stdint.h>
#include <stddef.h>

#define COMM_BUF_SZ (1 << 21) // Communication buffer maximum size = 2Mb
#define NMESSAGES ((COMM_BUF_SZ-2*sizeof(size_t))/sizeof(struct message))

extern char comm_ready;

// We choose to store at most n=2 args directly to prevent Read/Write
// of unused parameters for functions with less than n args
struct message{
  void (*_Atomic func)(uintptr_t a1, uintptr_t a2);
  uintptr_t a1;
  uintptr_t a2;
} __attribute__ ((aligned (64))); // Aligned on cache lines
                                  // Thus, no cache line sharing among cores
                                  // (2 cores writting messages will access
                                  // 2 different cache line)

struct ring{
  struct message messages[NMESSAGES];
  _Atomic size_t iprod;
  _Atomic size_t icons;
} __attribute__ ((aligned (64)));


struct barrier{
  size_t remaining;
  int owner;
  char* wait[0];
};


void initcomm(void);
void process_msg();
void send(int, void (*)(uintptr_t, uintptr_t), uintptr_t, uintptr_t);

// Sync functions
struct barrier* create_barrier(size_t);
void release_barrier(uintptr_t, uintptr_t);
void on_barrier(uintptr_t, uintptr_t);
void wait_barrier(struct barrier*, size_t);

// Remote functions
void remote_grant(uintptr_t, uintptr_t);
void remote_printf(uintptr_t, uintptr_t);


#endif // __COMM_H__
