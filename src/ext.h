#ifndef EXT_H
#define EXT_H

// Forward declare
struct UI_tMenuItem;

typedef struct ext_state {
    // Extension name
    const char* name;
    // Test if applicable and initialize. Return 1 if matches, 0 if not.
    int (*initialize)(void) ;
    // Called before each GL frame is rendered
    void (*pre_gl_frame)(void);
    // Called after each GL frame is rendered
    void (*post_gl_frame)(void);
    // Called when opcode injection is detected, returns the opcode to process
    int (*code_injection)(int pc, int op);
    // List of addresses where to use code injection.
    // If not provided, code_injection is called every time.
    // Needs to end with -1
    int *injection_list;
    // Return config items
    struct UI_tMenuItem* (*get_config)(void);
    // Handle config choice
    void (*handle_config)(int);
    // Custom state
    void *custom_state;
} ext_state;

ext_state* ext_state_alloc(void);

void ext_init(void);

// Register an extension
void register_ext(struct ext_state *ext_state);

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
#endif   /* EXT_H */
