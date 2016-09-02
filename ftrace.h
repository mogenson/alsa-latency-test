#ifndef FTRACE_H
#define FTRACE_H

int trace_init(void);
int trace_start(char *msg);
int trace_stop(char *msg);

#endif /* FTRACE_H */
