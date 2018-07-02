
extern (C):

nothrow:
@nogc:

char *mem_strdup(const(char) *);
void *mem_malloc(size_t);
void *mem_calloc(size_t);
void *mem_realloc(void *,size_t);
void mem_free(void *);
void mem_init();
void mem_term();

extern (C++)
{
    void mem_free_cpp(void *);
    alias mem_freefp = mem_free_cpp;
}

enum MEM_E { MEM_ABORTMSG, MEM_ABORT, MEM_RETNULL, MEM_CALLFP, MEM_RETRY }
void mem_setexception(MEM_E,...);

version (MEM_DEBUG)
{
    alias mem_fstrdup = mem_strdup;
    alias mem_fcalloc = mem_calloc;
    alias mem_fmalloc = mem_malloc;
    alias mem_ffree   = mem_free;
}
else
{
    char *mem_fstrdup(const(char) *);
    void *mem_fcalloc(size_t);
    void *mem_fmalloc(size_t);
    void mem_ffree(void *) { }
}
