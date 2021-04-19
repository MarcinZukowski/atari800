#ifndef EXT_H
#define EXT_H

typedef struct ext_state {
    // Extension name
    const char* name;
    // Test if applicable and initialize
    int (*initialize)(void) ;
    // Called after each frame is rendered
    void (*render_frame)(void);
    // Called when opcode injection is detected, returns the opcode to process
    int (*code_injection)(int pc, int op);
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
void ext_gl_frame(void);

typedef unsigned char byte;

#define OP_RTS 0x60
#define OP_NOP 0xEA

#endif   /* EXT_H */
