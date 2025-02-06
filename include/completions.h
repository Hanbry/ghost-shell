#ifndef COMPLETIONS_H
#define COMPLETIONS_H

#include <histedit.h>

/* Initialize completion system */
void completions_init(void);

/* Clean up completion system */
void completions_cleanup(void);

/* Tab completion function for libedit */
unsigned char ghost_complete(EditLine *edit_line, int ch);

#endif /* COMPLETIONS_H */
