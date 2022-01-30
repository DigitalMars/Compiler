# Source Code to the Digital Mars C and C++ compilers.

http://www.digitalmars.com

## Running the compiler:

https://digitalmars.com/ctg/sc.html

## Building the compiler (Win32 only):

1. Download and install Digital Mars C++ compiler from https://www.digitalmars.com/download/freecompiler.html
2. Download and install DMD 2.074.1 from http://downloads.dlang.org/releases/2017/
3. In the DMD installation, edit the file `src\druntime\importcore\stdc\stdio.d` and replace
   `shared stdout = &_iob[1];`
   with
   `enum stdout = &_iob[1];`.
4. Change directory to `dm\src\dmc`
5. Make sure the `dm\bin\make.exe` program is on your `PATH`.
6. Execute the commands:
   `make clean`
   `make scppn`
You might need to edit the `makefile` to set the path to your DMD installation.

Note that DMC runs on Win32, and hasn't been ported to other platforms.
It can generate code for Win32, 16 bit DOS, 16 and 32 bit Windows, 286 DOS extenders,
and 386 DOS extenders.
