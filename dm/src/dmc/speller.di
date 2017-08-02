
extern (C++):

alias void* function(void*, const(char)*) fp_speller_t;

extern __gshared char[63] idchars;

void* speller(const(char)* seed, fp_speller_t fp, void* fparg, const(char)* charset);

