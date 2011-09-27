// Copyright (C) 1995-1998 by Symantec
// Copyright (C) 2000-2009 by Digital Mars
// All Rights Reserved
// http://www.digitalmars.com
/*
 * This source file is made available for personal use
 * only. The license is in /dmd/src/dmd/backendlicense.txt
 * or /dm/src/dmd/backendlicense.txt
 * For any other uses, please contact Digital Mars.
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
