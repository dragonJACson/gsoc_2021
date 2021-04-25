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
        while (grep->pathList) {
            GSList *tmp = grep->pathList;
            grep->pathList = grep->pathList->next;
            g_free(tmp->data);
            g_slist_free_1(tmp);
        }
    }

    while (grep) {
        Grep *t = grep;
        grep = grep->next;
        free(t);
    }
}


static int
addPaths(GSList **paths,
         const char *path,
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
        *paths = g_slist_prepend(*paths, g_strdup(path));
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

            if (addPaths(paths, newpath, recursive) < 0)
                goto error;
        }
    }

    if (dirp)
        g_dir_close(dirp);
    return 0;

 error:
    if (dirp)
        g_dir_close(dirp);
    return -1;
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
    /* Though npaths = 0, we still have to store a path
    like '/dev/stdin' or './' */
    if (npaths == 0) {
        npaths = 1;
    }
    Grep *grep = malloc(sizeof(Grep) * npaths);
    grep->pathList = NULL;
    grep->next = NULL;
    /* No path given and recursive != 1, read from stdin */
    if (*paths == NULL && recursive == 0) {
        GSList **listHead = &grep->pathList;
        char tmp[2] = "-";
        const char *tmp_path = tmp;
        addPaths(listHead, tmp_path, recursive);
        return grep;
    } else if (*paths == NULL && recursive != 0) {
    /* No path given and recursive == 1, grep cur dir  */
        GSList **listHead = &grep->pathList;
        char tmp[3] = "./";
        const char *tmp_path = tmp;
        addPaths(listHead, tmp_path, recursive);
        return grep;
    } else {
    /* Normal situation, for every path given, use a grep
    structure to store the path generated */
        GSList **listHead = &grep->pathList;
        Grep *head = grep;
        for (int i = 0; paths[i] != NULL; i++) {
            addPaths(listHead, paths[i], recursive);
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
        Grep **grepNode = &grep;
        GSList **node = &(*grepNode)->pathList;
        /* Default behaviour */
        if (!(*grepNode)->next && !(*node)->next && filename != 1) {
            const char *path = (*node)->data;
            ret = cb(path, pattern, linenumber, 0);
            return ret;
        } else {
            /* filename % 2 corresponds to the original
            filename for callback func */
            filename = filename % 2;
            while (*grepNode) {
                while(*node) {
                    const char *path = (*node)->data;
                    ret = cb(path, pattern, linenumber, filename);
                    node = &(*node)->next;
                    if (ret < 0) {
                        return ret;
                    }
                }
                if ((*grepNode)->next) {
                    GrepDo((*grepNode)->next, pattern, linenumber, filename, cb);
                    grepNode = &(*grepNode)->next;
                } else {
                    return ret;
                }
            }
            return ret;
        }
    }
    return ret;
}
