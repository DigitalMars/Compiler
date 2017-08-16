
extern (C++):

nothrow:
@nogc:

char *mem_strdup(const(char) *);
void *mem_malloc(size_t);
void *mem_calloc(size_t);
void *mem_realloc(void *,size_t);
void mem_free(void *);
void mem_init();
void mem_term();

alias mem_freefp = mem_free;
