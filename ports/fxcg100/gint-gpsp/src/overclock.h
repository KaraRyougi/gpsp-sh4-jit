#ifndef CGBA_OVERCLOCK_H
#define CGBA_OVERCLOCK_H

/* Apply cgba's default hardware clock profile. The no-overclock build keeps
 * this entry point as a no-op so startup has one lifecycle path. */
void cgba_overclock_init(void);

#endif
