#ifndef OOM_H
#define OOM_H

#include <sys/types.h>

#define OOM_SCORE_ADJ_MIN (-1000)
#define OOM_SCORE_ADJ_MAX 1000
#define OOM_SCORE_ADJ_STEP 50

int oom_read(pid_t pid, int *out_value);
int oom_write(pid_t pid, int value);

/* Clamp value to [OOM_SCORE_ADJ_MIN, OOM_SCORE_ADJ_MAX] */
int oom_clamp(int value);

#endif /* OOM_H */
