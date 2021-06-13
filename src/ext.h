#ifndef EXT_H
#define EXT_H

#ifndef WITH_EXT
#error "WITH_EXT is expected"
#endif

// Forward declare
struct UI_tMenuItem;

typedef struct ext_state {
    // Extension name
    const char* name;
    // Test if applicable and initialize. Return 1 if matches, 0 if not.
    int (*initialize)(struct ext_state *self);
    // Called before each GL frame is rendered
    void (*pre_gl_frame)(struct ext_state *self);
    // Called after each GL frame is rendered
    void (*post_gl_frame)(struct ext_state *self);
    // Called when opcode injection is detected, returns the opcode to process
    int (*code_injection)(struct ext_state *self, int pc, int op);
    // List of addresses where to use code injection.
    // If not provided, code_injection is called every time.
    // Needs to end with -1
    int *injection_list;
    // Return config items
    struct UI_tMenuItem* (*get_config)(struct ext_state *self);
    // Handle config choice - the option param is the number of the menu that has changed
    void (*handle_config)(struct ext_state *self, int option);
    // Internal state
    void *internal_state;
} ext_state;

ext_state* ext_state_alloc(void);

void ext_init(void);

// Register an extension
void ext_register_ext(ext_state *ext_state);

int ext_handle_code_injection(int pc, int op);
void ext_frame(void);
void ext_pre_gl_frame(void);
void ext_post_gl_frame(void);

typedef unsigned char byte;

#define OP_RTS 0x60
#define OP_NOP 0xEA

// Helper tools
int ext_fakecpu_until_pc(int end_pc);
int ext_fakecpu_until_op(int end_op);
int ext_fakecpu_until_after_op(int end_op);

char *ext_fps_str(int previous_value);

#define EXT_ASSERT(cond, fmt, args...) if (!(cond)) {printf("ASSERTION FAILED: %s\nLOCATION: %s %d\nMSG: " fmt "\n", #cond, __FILE__, __LINE__, args); exit(-1);}
#define EXT_ASSERT_BETWEEN(val, lo, hi) EXT_ASSERT(val >= lo && val <= hi, "Value of %s=%g not between %g and %g", \
        #val, (float)val, (float) lo, (float)hi)
#define EXT_ASSERT_NOT_NULL(val) EXT_ASSERT(val != NULL, "Value of %s is NULL", #val)
#define EXT_ASSERT_EQ(val, exp) EXT_ASSERT(val == exp, "Value of %s=%g not equal to %g", #val, (float)val, (float)exp)
#define EXT_ASSERT_NE(val, exp) EXT_ASSERT(val != exp, "Value of %s=%g equal to %g", #val, (float)val, (float) exp)
#define EXT_ASSERT_LT(val, exp) EXT_ASSERT(val < exp, "Value of %s=%g not lower than %g", #val, (float)val, (float) exp)
#define EXT_ASSERT_GT(val, exp) EXT_ASSERT(val > exp, "Value of %s=%g not greater than %g", #val, (float)val, (float) exp)

#define EXT_ERROR(fmt, args...) do { printf("ERROR at %s:%d: " fmt "\n", __FILE__, __LINE__, args); exit(-2); } while (0)
#define EXT_ERROR0(fmt) EXT_ERROR(fmt "%s", "")
#define EXT_TRACE(fmt, args...) do { printf("%s:%d:%s: " fmt "\n", __FILE__, __LINE__, __FUNCTION__, args); fflush(stdout);} while (0)
#define EXT_TRACE0(fmt) EXT_TRACE(fmt "%s", "")

// Acceleration is disabled on CTRL, can be used by extensions
int ext_acceleration_disabled(void);

typedef struct {
    // Internal data structure for sounds, not exposed
    void* data;
} ext_sound;

ext_sound* ext_sound_load(const char* fname);
void ext_sound_play(ext_sound *snd);


#endif   /* EXT_H */
