// Copyright (C) 1985-1995 by Symantec
// Copyright (C) 2000-2009 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
// Written by Walter Bright
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
 */

// All .OBJ file writing is done here.

#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<ctype.h>
#include	"cc.h"
#include	"token.h"
#include	"global.h"
#include	"parser.h"

#if (SCPP || MARS) && !HTOD

static char __file__[] = __FILE__;	/* for tassert.h		*/
#include	"tassert.h"

static char *fobjname;			// output file name

/*******************************
 */

STATIC void objfile_error()
{
    err_fatal(EM_write_error,fobjname);		// error writing output file
}

/***************************************
 * Open .OBJ file.
 */

void objfile_open(const char *name)
{
    fobjname = (char *)name;
}

/************************************
 * Close .OBJ file.
 */

void objfile_close(void *data, unsigned len)
{
    if (file_write(fobjname, data, len))
	objfile_error();
}

/************************************
 * Close and delete .OBJ file.
 */

void objfile_delete()
{
    file_remove(fobjname);	// delete corrupt output file
}

/**********************************
 * Terminate.
 */

void objfile_term()
{
#if TERMCODE
    mem_free(fobjname);
    fobjname = NULL;
#endif
}

#endif
