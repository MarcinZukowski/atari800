#ifndef EXT_H
#define EXT_H

/* Extensions are game-specific hacks written in JavaScript, one module per
   game in data/ext/<name>/init.js (see data/ext/README.md).

   This file is the emulator-side plumbing: the list of extensions, activating
   the one whose memory fingerprint matches (via the TAB menu or A8_EXT_SELECT),
   the ALT/CTRL modifier keys, the hooks called from cpu.c, sdl/main.c and
   sdl/video_gl.c, and the "fake CPU" helpers. The scripting side, which
   embeds QuickJS and defines what a script sees, is ext-js.c. */

#ifndef WITH_EXT
#error "WITH_EXT is expected"
#endif

#include <stdio.h>

/* Where the extensions live, <ext_dir>/<name>/init.js: the -ext-dir option
   or EXT_DIR in the configuration file; "data/ext" unless set. These three
   are called from the platform's own option and configuration handlers. */
extern char ext_dir[];
int ext_initialise(int *argc, char *argv[]);
int ext_read_config(char *option, char *parameters);
void ext_write_config(FILE *fp);

/* Hooks called by the emulator */
void ext_init(void);
void ext_frame(void);
void ext_pre_gl_frame(void);
void ext_post_gl_frame(void);
int ext_handle_code_injection(int pc, int op);

/* Services for the scripting side */
struct ext_extension;
void ext_register(struct ext_extension *ext);

/* TRUE while ALT (extensions off) or CTRL (acceleration off) is held */
int ext_acceleration_disabled(void);

/* TRUE when an extension is active (the video recorder then takes the
   display's picture, which holds what the extension draws) */
int ext_is_active(void);

/* Frame counter display: a change of current_value counts as a new frame */
char *ext_fps_str(int current_value);

/* 6502 opcodes an onCodeInjection hook may return */
#define OP_RTS 0x60
#define OP_NOP 0xEA

/* Run the CPU without side effects on the machine state, from the current
   instruction until reaching an address or an opcode (or just past it), but
   at most max_insns instructions (0: the default, a million), after which
   the run is given up with a warning. Return the opcode the hook should then
   execute (a NOP). */
int ext_fakecpu_until_pc(int end_pc, int max_insns);
int ext_fakecpu_until_op(int end_op, int max_insns);
int ext_fakecpu_until_after_op(int end_op, int max_insns);
int ext_fakecpu_while_in(int lo, int hi, int max_insns);
void ext_set_code_injections(const int *addresses, int count);

/* (ext-cpu.c) TRUE when a script wants to be called at pc; TRUE while the
   fake CPU runs, when no script is called */
int ext_code_injection_wanted(int pc);
extern int ext_cpu_faking;

typedef unsigned char byte;

#define EXT_ERROR(fmt, ...) do { printf("ERROR at %s:%d: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__); exit(2); } while (0)
#define EXT_ASSERT(cond, fmt, ...) do { if (!(cond)) { printf("ASSERTION FAILED: %s\nLOCATION: %s %d\nMSG: " fmt "\n", #cond, __FILE__, __LINE__, __VA_ARGS__); exit(1); } } while (0)
#define EXT_ASSERT_NOT_NULL(val) EXT_ASSERT((val) != NULL, "%s is NULL", #val)
#define EXT_ASSERT_LT(val, exp) EXT_ASSERT((val) < (exp), "%s=%d is not lower than %d", #val, (int) (val), (int) (exp))

#endif /* EXT_H */
