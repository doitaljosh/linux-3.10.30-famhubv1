#ifndef KDBG_WAITMACROS_H_
#define KDBG_WAITMACROS_H_
/* ported macros from user space libs
      - considered for non-bsd case
 */
/* from waitstatus.h */
#define	__WEXITSTATUS(status)	(((status) & 0xff00) >> 8)
#define	__WTERMSIG(status)	((status) & 0x7f)
#define	__WSTOPSIG(status)	__WEXITSTATUS(status)
#define	__WIFEXITED(status)	(__WTERMSIG(status) == 0)
#define __WIFSIGNALED(status) \
  (((signed char) (((status) & 0x7f) + 1) >> 1) > 0)
#define	__WIFSTOPPED(status)	(((status) & 0xff) == 0x7f)
#ifdef WCONTINUED
# define __WIFCONTINUED(status)	((status) == __W_CONTINUED)
#endif
#define	__WCOREDUMP(status)	((status) & __WCOREFLAG)
#define	__W_EXITCODE(ret, sig)	((ret) << 8 | (sig))
#define	__W_STOPCODE(sig)	((sig) << 8 | 0x7f)
#define __W_CONTINUED		0xffff
#define	__WCOREFLAG		0x80

/* from sys/wait.h */
#  define __WAIT_INT(status)	(status)
# define WEXITSTATUS(status)	__WEXITSTATUS (__WAIT_INT (status))
# define WTERMSIG(status)	__WTERMSIG (__WAIT_INT (status))
# define WSTOPSIG(status)	__WSTOPSIG (__WAIT_INT (status))
# define WIFEXITED(status)	__WIFEXITED (__WAIT_INT (status))
# define WIFSIGNALED(status)	__WIFSIGNALED (__WAIT_INT (status))
# define WIFSTOPPED(status)	__WIFSTOPPED (__WAIT_INT (status))
# ifdef __WIFCONTINUED
#  define WIFCONTINUED(status)	__WIFCONTINUED (__WAIT_INT (status))
#endif
#endif
