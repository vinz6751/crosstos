/*
 * Map TOS-style paths (drive letters, backslashes, 8.3 names) to a host path
 * string. Caller frees with path_open's result via path_close(). Resolution is
 * case-insensitive on the host directory entries.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctype.h>
#include "paths.h"

/* Advance *in past '/' or '\\'; optional output is the first non-separator. */
static bool skip_slashes(char** in, char** out)
{
    char*  str = *in;

    bool found = false;

    while((*str == '/') || (*str == '\\'))
    {
        str++;

        found = true;
    }

    if(out)
    {
        *out = str;
    }

    return found;
}

/* If *in starts with "C:".."Z:", skip it and any following slashes. *in becomes
 * the path relative to that drive (e.g. ".\FOO.S" or "AUTO\PRG"). */
static bool skip_rootdrive(char** in)
{
    char*  str = *in;

    bool found = false;

	int c = toupper((unsigned char)*str);
	
	if (c >= 'C' && c <= 'Z' && str[1] == ':')
	{
		str += 2;
		found = true;
	}
    while((*str == '/') || (*str == '\\'))
    {
        str++;

        found = true;
    }

    *in = str;

    return found;
}

/* Length of one path component: up to (but not including) next '/' or '\\'. */
static int count_chars(char** in, char** out)
{
    char* str    = *in;

    int   count = 0;

    while(*str && (*str != '/') && (*str != '\\'))
    {
        str++;
        count++;
    }

    if(out)
    {
        *out = str;
    }

    return count;
}

/* Look for a directory entry matching item[0..count) under path (case fold).
 * On success, append "/" and the real directory name to path (length grows). */
static bool path_find_item(char* path, char* item, int count)
{
    bool match = false;
    struct dirent *de;

    int i;

    DIR* dir = opendir (path);

    if(dir)
    {
        while(!match && (de = readdir (dir)))
        {
            match = true;

            char c1, c2;
            for(i = 0; i < count; i++)
            {
                c1 = toupper(de->d_name[i]);
                c2 = toupper(item[i]);

                if(c1 != c2)
                {   
                    match = false;
                    break;
                }
                else
                {
                    if(!c1)
                    {
                        break;
                    }
                }
            }
            if (de->d_name[i])
                match = false;
        }

        if(match)
        {
            strcat(path, "/");
            strcat(path, de->d_name);
        }

        closedir(dir);
    }

    return match;
}

void path_close(char* path) /* free() wrapper for path_open result */
{
    if(path)
    {
        free(path);
    }
}

/*
 * exist: if true, every path component must exist in the host tree; otherwise
 * the final missing component may be appended (for creating a new file name).
 * Returns malloc'd host path or NULL on failure.
 */
char* path_open(char* fname, bool exist)
{
    char* search_path = malloc(4000);
    char* root_path;

    if(search_path)
    {
        if(skip_rootdrive(&fname))
        {
            /*
             * After "C:" the old logic always rooted at TOS_ROOT_PATH or "/" and
             * walked path components from there. Programs can pass paths like
             * "C:.\FOO.S" (current directory on the drive). The first segment is
             * then ".", which under "/" becomes a broken path (e.g. "//.") and
             * path_open returns NULL — Fsfirst/Fopen then see EFILNF even when
             * FOO.S exists in the host cwd. Treat ".\..." / "./..." after the
             * drive as host-relative: start from "." so case-insensitive lookup
             * finds the file next to the emulated binary.
             */
            if (fname[0] == '.' && (fname[1] == '\\' || fname[1] == '/'))
            {
                fname += 2;
                while (*fname == '\\' || *fname == '/')
                    fname++;
                strcpy(search_path, ".");
            } else
            {
                if ((root_path = getenv("TOS_ROOT_PATH")) != NULL)
                    strcpy(search_path, root_path);
                else
                    strcpy(search_path, "/");
            }
        }
        else
        {
            /* No "C:" — path is relative to host cwd. */
            strcpy(search_path, ".");
        }

        /* Walk fname one component at a time, extending search_path. */
        char* fnext = fname;

        do
        {
            char* fend;

            /*
             * Count number of chars until slash/end
             */

            int count = count_chars(&fnext, &fend);

            /* item now at <fnext>, <count> chars long */
            if(path_find_item(search_path, fnext, count)) // also adds actual name to path
            {
            //    printf("fend %s fnext %s\n", fend, fnext);

                /* Proceed to next item */
                skip_slashes(&fend, &fnext);

            //    printf("fend %s fnext %s\n", fend, fnext);
            }
            else
            {
                /* Component not found: fail if more path remains or caller
                 * requires full resolution; else append rest (new file path). */
                if(*fend || exist)
                {
                    free(search_path);
                    search_path = NULL;
                }
                else
                {
                    strcat(search_path, "/");
                    strcat(search_path, fnext);
                }
 
                break;
            }
        }
        while(*fnext);

    //    printf("found %s\n", search_path);
    }

    return search_path;
}
