#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>

#include "grep_impl.h"


/**
 * GrepFree:
 * @grep: Grep structure
 *
 * Free given Grep structure among with its members (that need
 * it). NOP if @g is NULL.
 */
void
GrepFree(Grep *grep)
{
    if (!grep) {
        return ;
    }

    if (grep->pathList) {
        while (grep->pathList->next) {
            GSList *tmp = grep->pathList;
            grep->pathList = grep->pathList->next;
            free(tmp);
        }
    }

    while (grep) {
        Grep *t = grep;
        grep = grep->next;
        free(t);
    }
}


GSList *
addPaths(GSList *paths,
         const char *path,
         const char *rootpath,
         int recursive)
{
    struct stat sb;
    GDir *dirp = NULL;

    if (strcmp(path, "-") == 0)
        path = "/dev/stdin";

    if (lstat(path, &sb) < 0) {
        perror(path);
        goto error;
    }

    if (!S_ISDIR(sb.st_mode)) {
        paths = g_slist_prepend(paths, g_strdup(path));
    } else {
        const char *f;
        if (!recursive) {
            errno = EISDIR;
            perror(path);
            goto error;
        }

        if (!(dirp = g_dir_open(path, 0, NULL))) {
            perror(path);
            goto error;
        }

        while ((f = g_dir_read_name(dirp))) {
            g_autofree char *newpath = g_build_filename(path, f, NULL);
            if ((paths = addPaths(paths, newpath, rootpath, recursive)) == NULL && path == rootpath) {
                goto error;
            }
        }
    }

    if (dirp)
        g_dir_close(dirp);
    return paths;

  error:
    if (dirp)
        g_dir_close(dirp);
    return NULL;
}

/**
 * GrepInit:
 * @recursive: whether to traverse paths recursively
 * @paths: paths to traverse
 * @npaths: number of items in @paths array
 *
 * Allocate and initialize Grep structure. Process given @paths.
 *
 * Returns: an allocated structure on success,
 *          NULL otherwise.
 */
Grep *
GrepInit(int recursive,
         const char **paths,
         size_t npaths)
{
    /* No path given and recursive != 1, read from stdin */
    if (*paths == NULL && recursive == 0) {
        Grep *grep = malloc(sizeof(Grep));
        grep->pathList = NULL;
        grep->next = NULL;
        char tmp[2] = "-";
        const char *tmp_path = tmp;
        grep->pathList = addPaths(grep->pathList, tmp_path, tmp_path, recursive);
        return grep;
    } else if (*paths == NULL && recursive != 0) {
    /* No path given and recursive == 1, grep cur dir  */
        Grep *grep = malloc(sizeof(Grep));
        grep->pathList = NULL;
        grep->next = NULL;
        char tmp[3] = "./";
        const char *tmp_path = tmp;
        grep->pathList = addPaths(grep->pathList, tmp_path, tmp_path, recursive);
        return grep;
    } else {
    /* Normal situation, for every path given, use a grep
    structure to store the path generated */
        Grep *grep = malloc(sizeof(Grep) * npaths);
        grep->next = NULL;
        grep->pathList = NULL;
        Grep *head = grep;
        for (int i = 0; paths[i] != NULL; i++) {
            grep->pathList = addPaths(grep->pathList, paths[i], paths[i], recursive);
            if (paths[i + 1] != NULL) {
                Grep *tmp = malloc(sizeof(Grep));
                tmp->pathList = NULL;
                tmp->next = NULL;
                grep->next = tmp;
                grep = grep->next;
            }
        }
        return head;
    }
}


/* Used for multi-thread */
void *
GrepInitWrapper(void *arg)
{
    grepInitArgs *args;
    args = (grepInitArgs *) arg;
    args->grep = GrepInit(args->arg1, args->arg2, args->arg3);
    return ((void *)0);
}

/**
 * GrepDo:
 * @grep: Grep structure
 * @pattern: pattern to match
 * @linenumber: whether to report line numbers
 * @filename: whether to report filenames
 * @cb: pattern matching callback
 *
 * Feeds @cb with each path to match.
 *
 * Returns: 0 on success,
 *         -1 otherwise.
 */
int
GrepDo(Grep *grep,
       const char *pattern,
       int linenumber,
       int filename,
       GrepCallback cb)
{
    int ret = -1;
    if (grep && grep->pathList) {
        /* Default behaviour */
        if (!grep->next && !grep->pathList->next && filename != 1) {
            const char *path = grep->pathList->data;
            ret = cb(path, pattern, linenumber, 0);
            return ret;
        } else {
            /* filename % 2 corresponds to the original
            filename for callback func */
            filename = filename % 2;
            while (grep) {
                while(grep->pathList) {
                    const char *path = grep->pathList->data;
                    ret = cb(path, pattern, linenumber, filename);
                    grep->pathList = grep->pathList->next;
                    if (ret < 0) {
                        return ret;
                    }
                }
                if (grep->next) {
                    GrepDo(grep->next, pattern, linenumber, filename, cb);
                    grep->next = NULL;
                } else {
                    return ret;
                }
            }
            return ret;
        }
    }
    return ret;
}
