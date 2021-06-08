#ifndef EXT_LUA_H_
#define EXT_LUA_H_

#ifndef WITH_EXT_LUA
#error "WITH_EXT_LUA is expected"
#endif

struct lua_State;

void ext_lua_init(void);

struct lua_State* lua_ext_get_state(void);

int ext_lua_run_str(const char *str);

#endif  /* EXT_LUA_H */
