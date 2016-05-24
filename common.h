#ifndef _ERR_
#define _ERR_

#define MAX_MESSAGE_LENGTH 1000
#define PORT_DEFAULT 20160

void fail_aux(const char* line_text, int line_number, const char* file_name, const char *emsg);

void* safe_malloc(size_t bytes);
void safe_all_write(int fd, char *buffer, size_t len);

void fatal(const char *fmt, ...);
void _ignore(const char* fmt, ...);

#define negative_is_bad(fun, emsg) ((fun) < 0) ? fail_aux(#fun, __LINE__, __FILE__, emsg) : (void) 0
#define zero_is_ok(fun, emsg) ((fun) != 0) ? fail_aux(#fun, __LINE__, __FILE__, emsg) : (void) 0

struct MessageBuffer
{
    bool rcvd_only_first_byte;
    short to_receive; //-1 means we didnt started receiving
    unsigned short received;
    char* data;
};


#endif
