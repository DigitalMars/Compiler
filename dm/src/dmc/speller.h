
typedef void *(fp_speller_t)(void *, const char *);

extern char idchars[];

void *speller(const char *seed, fp_speller_t fp, void *fparg, const char *charset);

