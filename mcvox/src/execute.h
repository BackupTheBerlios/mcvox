#ifndef MC_EXECUTE_H
#define MC_EXECUTE_H

#define EXECUTE_INTERNAL   1
#define EXECUTE_AS_SHELL   4

/* Execute functions that use the shell to execute */
void shell_execute (const char *command, int flags);

/* This one executes a shell */
void exec_shell (void);

/* Handle toggling panels by Ctrl-O */
void toggle_panels (void);

/* Handle toggling panels by Ctrl-Z */
void suspend_cmd (void);

/* Execute command on a filename that can be on VFS */
void execute_with_vfs_arg (const char *command, const char *filename);

#endif /* !MC_EXECUTE_H */
