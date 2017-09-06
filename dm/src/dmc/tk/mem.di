
extern (C++):

nothrow:
@nogc:

char *mem_strdup(const(char) *);
char *mem_fstrdup(const(char) *);
void *mem_malloc(size_t);
void *mem_fmalloc(size_t);
void *mem_calloc(size_t);
void *mem_realloc(void *,size_t);
void mem_free(void *);
void mem_ffree(void *) { }
void mem_init();
void mem_term();

alias mem_freefp = mem_free;

enum MEM_E { MEM_ABORTMSG, MEM_ABORT, MEM_RETNULL, MEM_CALLFP, MEM_RETRY }
void mem_setexception(MEM_E,...);

