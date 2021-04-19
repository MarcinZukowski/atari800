#ifndef EXT_H
#define EXT_H

typedef struct ext_state {
    // Extension name
    const char* name;
    // Test if applicable and initialize. Return 1 if matches, 0 if not.
    int (*initialize)(void) ;
    // Called after each frame is rendered
    void (*render_frame)(void);
    // Called when opcode injection is detected, returns the opcode to process
    int (*code_injection)(int pc, int op);
    // List of addresses where to use code injection.
    // If not provided, code_injection is called every time.
    // Needs to end with -1
    int *injection_list;
    // Add items to config
    void (*add_to_config)(void);
    //
    void (*handle_config)(void);
    // Custom state
    void *custom_state;
} ext_state;

ext_state* ext_state_alloc(void);

void ext_init(void);

// Register an extension
void register_ext(struct ext_state *ext_state);

int ext_handle_code_injection(int pc, int op);
void ext_frame(void);
void ext_gl_frame(void);

typedef unsigned char byte;

#define OP_RTS 0x60
#define OP_NOP 0xEA

#endif   /* EXT_H */
