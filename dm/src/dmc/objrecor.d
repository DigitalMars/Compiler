/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1985-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * Authors:     $(LINK2 http://www.digitalmars.com, Walter Bright)
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/objrecor.d
 */

// All .OBJ file writing is done here.

module objrecor;

version (SPP)
{
}
else version (HTOD)
{
}
else
{

import ddmd.backend.global;

import tk.mem;

import msgs2;

extern (C++):

private __gshared char* fobjname;                  // output file name

enum TERMCODE = 1;

/*******************************
 */

private void objfile_error()
{
    err_fatal(EM_write_error,fobjname);         // error writing output file
}

/***************************************
 * Open .OBJ file.
 */

void objfile_open(const(char)* name)
{
    fobjname = cast(char*)name;
}

/************************************
 * Close .OBJ file.
 */

void objfile_close(void *data, uint len)
{
    if (file_write(fobjname, data, len))
        objfile_error();
}

/************************************
 * Close and delete .OBJ file.
 */

void objfile_delete()
{
    file_remove(fobjname);      // delete corrupt output file
}

/**********************************
 * Terminate.
 */

void objfile_term()
{
    if (TERMCODE)
    {
        mem_free(fobjname);
        fobjname = null;
    }
}

}
