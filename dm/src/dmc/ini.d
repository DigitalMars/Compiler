/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1994-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 1999-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/ini.d
 */

module ini;

version (SPP)
{

import core.stdc.ctype;
import core.stdc.stdio;
import core.stdc.stdlib;
import core.stdc.string;

import tk.filespec;
import tk.mem;

extern (C++):

struct IniGlobals
{
    char *buf;        // input line buffer
    int bufmax;       // max size of line buffer
    int curline;      // ini file line counter
    char *path;       // path to ini file
}

private __gshared IniGlobals iniglobals;

version (DigitalMars)
{
    import core.stdc.string : memicmp;
    extern (C) int putenv(const(char)*);
}
else
{
int memicmp(const(char)* s1, const(char)* s2, int n)
{
    int result = 0;

    for (int i = 0; i < n; i++)
    {   char c1 = s1[i];
        char c2 = s2[i];

        result = c1 - c2;
        if (result)
        {
            if ('A' <= c1 && c1 <= 'Z')
                c1 += 'a' - 'A';
            if ('A' <= c2 && c2 <= 'Z')
                c2 += 'a' - 'A';
            result = c1 - c2;
            if (result)
                break;
        }
    }
    return result;
}
}

/***************************
 * Read and parse ini file.
 * Input:
 *      argv0   argv[0], used to get path to .INI file
 *      ini     name of .INI file
 */

int readini(char *argv0,char *ini)
{   char *file;
    int status;

    // If ini is fully qualified
    if (*ini == '/' || *ini == '\\' || strchr(ini,':'))
        status = readinix(ini);
    else
    {
        iniglobals.path = mem_strdup(argv0);
        *filespecname(iniglobals.path) = 0;
        file = filespecaddpath(iniglobals.path,ini);
        status = readinix(file);
        mem_free(iniglobals.path);
        mem_free(file);
    }
    return status;
}

/*********************
 * Read makefile and build data structures.
 * Returns:
 *      0       success
 *      1       no ini file
 *      2       errors in ini file
 */

int readinix(char *file)
{   FILE *f;
    char* line,p,pn;
    int env;

    //printf("readinx('%s')\n",file);
    f = fopen(file,"rb");
    if (!f)
        return 1;
    env = 0;
    while (readline(f))
    {   line = expandline(iniglobals.buf, 1);      // expand macros
        p = skipspace(line);
        switch (*p)
        {   case '[':           // look for [Environment]
                p++;
                p = skipspace(p);
                pn = skipname(p);
                if (pn - p == strlen("Environment") &&
                    memicmp(p,"Environment",strlen("Environment")) == 0 &&
                    *skipspace(pn) == ']'
                   )
                    env = 1;
                else
                    env = 0;
                break;
            case ';':                   // comment line
            case 0:                     // blank line
                break;
            default:
                if (env)
                {   int status;

                    pn = p;

                    // Remove trailing spaces
                    p = pn + strlen(pn);
                    while (p > pn && isspace(p[-1]))
                        *--p = 0;

                    // Convert environment variable name to upper case,
                    // to match behavior of DOS and putenv().
                    for (p = pn; *p && *p != '='; p++)
                    {   if (islower(*p))
                            *p &= ~0x20;
                    }

                    if (*p == '=')
                    {
                        // Remove spaces following '='
                        while (isspace(p[1]))
                            memmove(p + 1,p + 2,strlen(p + 2) + 1);

                        // Remove spaces preceding '='
                        while (p > pn && isspace(p[-1]))
                        {   memmove(p - 1,p,strlen(p) + 1);
                            p--;
                        }
                    }

                    status = putenv(pn);
                    //printf("putenv('%s') = %d\n",pn,status);
                    //printf("getenv(\"TEST\") = '%s'\n",getenv("TEST"));
                }
                break;
        }
        mem_free(line);
    }
    mem_free(iniglobals.buf);
    iniglobals.buf = null;
    iniglobals.bufmax = 0;
    fclose(f);
    return 0;
}

/*************************
 * Read line from file f into iniglobals.buf.
 * Returns:
 *      0 if end of file
 */

extern (C) int readline(FILE *fp)
{   int i,c;
    int result;

    i = 0;
    while (1)
    {
        if (i >= iniglobals.bufmax)
        {   iniglobals.bufmax += 100;
            iniglobals.buf = cast(char *)mem_realloc(iniglobals.buf,iniglobals.bufmax);
        }
        c = fgetc(fp);

        switch (c)
        {
            case '\r':
            case 0:
                continue;       // ignore

            case 0x1A:
            case EOF:
                result = (i > 0);
                break;

            case '\n':
                result = 1;
                break;

            default:
                iniglobals.buf[i++] = cast(char)c;
                continue;
        }
        break;
    }
    iniglobals.buf[i] = 0;                 /* terminate string             */
    iniglobals.curline++;
    return result;
}

/************************
 * Perform macro expansion on the line pointed to by iniglobals.buf.
 * Return pointer to created string.
 */

char *expandline(char *buf, int domacros)
{
    uint i;                 /* where in buf we have expanded up to  */
    uint b;                 /* start of macro name                  */
    uint t;                 /* start of text following macro call   */
    uint p;                 /* 1 past end of macro name             */
    uint textlen;           /* length of replacement text (excl. 0) */
    uint buflen;            /* length of buffer (excluding 0)       */

    buf = mem_strdup(buf);
    i = 0;
    while (buf[i])
    {
        char c;
        const(char)* text;
        if (buf[i] == '%')      /* if start of macro            */
        {   b = i + 1;
            p = b;
            while (buf[p] != '%')
                if (!buf[p++])
                    goto L1;
            t = p + 1;
            c = buf[p];
            buf[p] = 0;
            if (domacros)
                text = searchformacro(buf + b);
            else
                text = "";
            buf[p] = c;
            textlen = strlen(text);
            // If replacement text exactly matches macro call, skip expansion
            if (textlen == t - i && memicmp(text,buf + i,textlen) == 0)
                i = t;
            else
            {
                buflen = strlen(buf);
                buf = cast(char *)mem_realloc(buf,buflen + textlen + 1);
                memmove(buf + i + textlen,buf + t,buflen + 1 - t);
                memmove(buf + i,text,textlen);
            }
            if (domacros)
                mem_free (cast(void *)text);
        }
        else
        {
         L1:
            i++;
        }
    }
    return buf;
}

/*********************
 * Search for macro.
 */

char *searchformacro(char *name)
{   char *envstring;
    char *p;

    name = mem_strdup(name);
    for (p = name; *p; p++)             // convert name to upper case
        if (islower(*p))
            *p &= ~0x20;
    if (strcmp(name,"@P") == 0)
    {
            envstring = mem_strdup (iniglobals.path);

            /* @P should expand to path to sc.ini without \ */
            if (envstring[strlen(envstring)-1] == '\\')
               envstring[strlen(envstring)-1] = '\0';
        }
    else
        {   envstring = getenv(name);
            if (envstring)
                envstring = expandline (envstring, 0);
        }
    mem_free(name);
    return (envstring) ? envstring : mem_strdup ("");
}

/********************
 * Skip spaces.
 */

char *skipspace(const(char)* p)
{
    while (isspace(*p))
        p++;
    return cast(char *)p;
}

/********************
 * Skip name.
 */

char *skipname(const(char)* p)
{
    while (isalnum(*p))
        p++;
    return cast(char *)p;
}

}
