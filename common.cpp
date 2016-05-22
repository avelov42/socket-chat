#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include "common.h"

extern void die(int, const char *);

void fail_aux(const char *line_text, int line_number, const char *file_name, const char *emsg)
{
    //fprintf(stderr, "Error at line #%d in file %s\n%s\n%s\n", line_number, file_name, line_text, emsg);
    //perror(NULL);
    die(1, emsg);
}

void *safe_malloc(size_t bytes)
{
    void *rv = malloc(bytes);
    if(rv == NULL) 
    {
	die(false, "memory error");
	return NULL;
    }
    else return rv;
}

void fatal(const char *fmt, ...)
{
    va_list fmt_args;

    fprintf(stderr, "ERROR: ");

    va_start(fmt_args, fmt);
    if(vfprintf(stderr, fmt, fmt_args) < 0)
    {
        fprintf(stderr, " (also error in fatal) ");
    }
    va_end(fmt_args);

    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

void safe_all_write(int fd, char *buffer, size_t len)
{
    ssize_t write_rv;
    size_t written = 0;
    while(written < len)
    {
        negative_is_bad(write_rv = write(fd, buffer + written, len - written), "send error");
        written += write_rv;
    }
}

void ignore(const char* fmt, ...)
{
    return;
}
