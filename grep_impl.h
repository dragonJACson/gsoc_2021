#pragma once

#include <stddef.h>
#include <glib.h>

typedef struct _Grep Grep;
typedef struct _grepInitArgs grepInitArgs;

/* Used for multi-thread */
struct _grepInitArgs {
    int arg1;
    const char **arg2;
    size_t arg3;
    Grep *grep;
};

struct _Grep {
    GSList *pathList;
    Grep *next;
};

void
GrepFree(Grep *g);

Grep *
GrepInit(int recursive,
         const char **paths,
         size_t npaths);

typedef int (*GrepCallback) (const char *file,
                             const char *pattern,
                             int linenumber,
                             int filename);

void *
GrepInitWrapper(void *arg);

int
GrepDo(Grep *grep,
       const char *pattern,
       int linenumber,
       int filename,
       GrepCallback cb);
