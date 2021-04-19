#ifndef LUA_EXT_H_
#define LUA_EXT_H_


struct lua_State;

void lua_ext_init(void);

struct lua_State* lua_ext_get_state(void);

int lext_run_str(const char *str);

#endif  /* LUA_EXT_H */
