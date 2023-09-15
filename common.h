#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;
#define nil NULL

#define nelem(array) (sizeof(array)/sizeof(array[0]))

void panic(const char *fmt, ...);
int hasinput(int fd);
int dial(const char *host, int port);
void nodelay(int fd);

void inittime(void);
u64 gettime(void);
void nsleep(u64 ns);

char **split(char *line, int *pargc);
