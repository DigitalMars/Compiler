
#if USEDLLSHELL
#if __NT__
#include        "netspawn.h"
#else
#include        "dll_impl.h"
#endif
#else

#define NetSpawnProgress(l)     NetSpawnOK
#define NetSpawnMessage(p)
#define NetSpawnFile(a,b)
#define NetSpawnTranslateFileName(name,mode)    (name)
#define NetSpawnDisposeFile(name)

#endif

