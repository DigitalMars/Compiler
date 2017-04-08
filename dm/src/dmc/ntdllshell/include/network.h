/**
 * Implementation of the
 * $(LINK2 http://www.digitalmars.com/download/freecompiler.html, Digital Mars C/C++ Compiler).
 *
 * Copyright:   Copyright (c) 1995-1998 by Symantec, All Rights Reserved
 *              Copyright (c) 2000-2017 by Digital Mars, All Rights Reserved
 * License:     Distributed under the Boost Software License, Version 1.0.
 *              http://www.boost.org/LICENSE_1_0.txt
 * Source:      https://github.com/DigitalMars/Compiler/blob/master/dm/src/dmc/ntllshell/include/network.h
 */


#ifndef __NETWORK_H
#define __NETWORK_H

#if __cplusplus
extern "C" {
#endif

typedef short HCONNECTION;

typedef enum
{
   NetCommStatusActive = 0,
   NetCommStatusInactive,
   NetCommStatusOK,
   NetCommStatusFail
} NetCommStatus;

#define MAX_NETWORK_NAME_LENGTH 16

typedef struct
{
  char name[MAX_NETWORK_NAME_LENGTH];                 // is 16 bytes
  HCONNECTION hconn;
} NetCommAddress;

typedef enum
{
  NetCommProtocol_UnDefined,
  NetCommProtocol_NetBiosViaIPX_SPX,
  NetCommProtocol_NetBiosViaNetBeui,
  NetCommProtocol_IPX_SPX
} NetComProtocol;

#if __cplusplus
}
#endif

#endif
