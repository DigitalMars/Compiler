#include        <stdio.h>
#include        <time.h>
#include        <string.h>
#include        <stdlib.h>
#if _WIN32
#include        <dos.h>
#endif

#include        "cc.h"
#include        "type.h"
#include        "token.h"
#include        "parser.h"
#include        "global.h"
#include        "el.h"
#include        "oper.h"
#include        "cpp.h"
#include        "exh.h"
#include        "filespec.h"
#include        "dt.h"
#if OMFOBJ
#include        "cgcv.h"
#endif
#include        "allocast.h"
#include        "outbuf.h"
