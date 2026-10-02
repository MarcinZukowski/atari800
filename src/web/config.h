/* config.h for the web build (web/Makefile, Emscripten): the emulator core
   with its own small platform layer (src/web/web.c) and no user interface.
   The HAVE_ lines are what configure found when run under emconfigure. */
#ifndef CONFIG_H_
#define CONFIG_H_

/* The core */
#define NEW_CYCLE_EXACT 1
#define MONITOR_PROFILE 1   /* a8.profile() */
#define HAVE_LIBZ 1         /* saved states are compressed */
#define WITH_EXT 1          /* the extension hooks; the scripts run in the page */

#define CONSOLE_SOUND 1
#define EMUOS_ALTIRRA 1
#define HAVE_ARPA_INET_H 1
#define HAVE_ATEXIT 1
#define HAVE_CHMOD 1
#define HAVE_CLOCK 1
#define HAVE_DIRENT_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_FDOPEN 1
#define HAVE_FFLUSH 1
#define HAVE_FLOOR 1
#define HAVE_FSEEKO 1
#define HAVE_FSTAT 1
#define HAVE_GETCWD 1
#define HAVE_GETHOSTBYADDR 1
#define HAVE_GETHOSTBYNAME 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_INET_NTOA 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIBPTHREAD 1
#define HAVE_LOCALTIME 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMSET 1
#define HAVE_MKDIR 1
#define HAVE_MKSTEMP 1
#define HAVE_MKTEMP 1
#define HAVE_MODF 1
#define HAVE_NANOSLEEP 1
#define HAVE_NETDB_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_OPENDIR 1
#define HAVE_POPEN 1
#define HAVE_RENAME 1
#define HAVE_REWIND 1
#define HAVE_RMDIR 1
#define HAVE_SELECT 1
#define HAVE_SETJMP 1
#define HAVE_SIGNAL 1
#define HAVE_SIGNAL_H 1
#define HAVE_SNPRINTF 1
#define HAVE_SOCKET 1
#define HAVE_STAT 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCASECMP 1
#define HAVE_STRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRNCPY 1
#define HAVE_STRRCHR 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_SYSTEM 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TERMIOS_H 1
#define HAVE_TIME 1
#define HAVE_TIME_H 1
#define HAVE_TMPFILE 1
#define HAVE_TMPNAM 1
#define HAVE_UINTPTR_T 1
#define HAVE_UNISTD_H 1
#define HAVE_UNLINK 1
#define HAVE_USLEEP 1
#define HAVE_VPRINTF 1
#define HAVE_VSNPRINTF 1
#define HAVE_WCHAR_H 1
#define INTERPOLATE_SOUND 1
#define LSTAT_FOLLOWS_SLASHED_SYMLINK 1
#define NONLINEAR_MIXING 1
#define PACKAGE "atari800"
#define PACKAGE_BUGREPORT "pstehlik@sophics.cz"
#define PACKAGE_NAME "Atari800"
#define PACKAGE_STRING "Atari800 7.2.1"
#define PACKAGE_TARNAME "atari800"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "7.2.1"
#define SELECT_TYPE_ARG1 int
#define SELECT_TYPE_ARG234 (fd_set *)
#define SELECT_TYPE_ARG5 (struct timeval *)
#define SOUND 1
#define STDC_HEADERS 1
#define STEREO_SOUND 1
#define VERSION "7.2.1"
#define WORDS_UNALIGNED_OK 1

#endif /* CONFIG_H_ */
