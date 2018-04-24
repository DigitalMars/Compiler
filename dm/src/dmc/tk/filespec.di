/*_ filespec.h   Fri Jul  8 1988   Modified by: bright */
/* Copyright (C) 1986-1987 by Northwest Software        */
/* All Rights Reserved                                  */
/* Written by Walter Bright                             */

extern (C++):

/*********************************
 * String compare of filenames.
 */

version (Windows)
{
    extern (C) int stricmp(const(char)*, const(char)*);
    extern (C) int memicmp(const(void)*, const(void)*, size_t);

    alias filespeccmp = stricmp;
    alias filespecmemcmp = memicmp;
}
else
{
    import core.stdc.string : strcmp, memcmp;
    alias filespeccmp = strcmp;
    alias filespecmemcmp = memcmp;
}

/****************************
 * Combine path and filename to form a filespec.
 * Input:
 *      path            Path, with or without trailing /
 *                      (can be NULL)
 *      filename        Cannot be NULL
 * Returns:
 *      filespec        mem_malloc'd file specification
 *      NULL            Out of memory
 */

char *filespecaddpath(const(char)*, const(char)*);

/******************************* filespecrootpath **************************
 * Purpose: To expand a relative path into an absolute path.
 *
 * Side Effects: mem_frees input string.
 *
 * Returns: mem_malloced string with absolute path.
 *          NULL if some failure.
 */

char *filespecrootpath(char *);

/*****************************
 * Add extension onto filespec, if one isn't already there.
 * Input:
 *      filespec        Cannot be NULL
 *      ext             Extension (without the .)
 * Returns:
 *      mem_malloc'ed string (NULL if error)
 */

char *filespecdefaultext(const(char)*, const(char)*);

/**********************
 * Return string that is the dot and extension.
 * The string returned is NOT mem_malloc'ed.
 * Return pointer to the 0 at the end of filespec if dot isn't found.
 * Return NULL if filespec is NULL.
 */

char *filespecdotext(const(char)*);

/*****************************
 * Force extension onto filespec.
 * Input:
 *      filespec        String that may or may not contain an extension
 *      ext             Extension that doesn't contain a .
 * Returns:
 *      mem_malloc'ed string (NULL if error)
 *      NULL if filespec is NULL
 *      If ext is NULL, return mem_strdup(filespec)
 */

char *filespecforceext(const(char)*, const(char)*);

/***********************
 * Get root name of file name.
 * That is, return a mem_strdup()'d version of the filename without
 * the .ext.
 */

char *filespecgetroot(const(char)*);

/**********************
 * Return string that is the filename plus dot and extension.
 * The string returned is NOT mem_malloc'ed.
 */

char *filespecname(const(char)*);

/************************************
 * If first character of filespec is a ~, perform tilde-expansion.
 * Output:
 *      Input filespec is mem_free'd.
 * Returns:
 *      mem_malloc'd string
 */

version (Windows)
{
    char *filespectilde(char *f) { return f; }
}
else
{
    char *filespectilde(char *);
}

/************************************
 * Expand all ~ in the given string.
 *
 * Output:
 *      Input filespec is mem_free'd.
 * Returns:
 *      mem_malloc'd string
 */

version (Windows)
{
    char *filespecmultitilde(char *f) { return f; }
}
else
{
    char *filespecmultitilde(char *);
}

/*****************************
 * Convert filespec into a backup filename appropriate for the
 * operating system. For instance, under MS-DOS path\filename.ext will
 * be converted to path\filename.bak.
 * Input:
 *      filespec        String that may or may not contain an extension
 * Returns:
 *      mem_malloc'ed string (NULL if error)
 *      NULL if filespec is NULL
 */

char *filespecbackup(const(char)* );
