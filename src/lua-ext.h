#ifndef LUA_EXT_H_
#define LUA_EXT_H_

void lua_ext_init();

int lua_ext_handle_code_injection(int pc, int op);

#endif  /* LUA_EXT_H */