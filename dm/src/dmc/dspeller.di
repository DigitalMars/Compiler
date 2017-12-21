
module dmd.root.speller;

alias dg_speller_t = void* delegate(const(char)*, ref int);

__gshared const(char)* idchars;

void* speller(const(char)* seed, scope dg_speller_t dg, const(char)* charset);

