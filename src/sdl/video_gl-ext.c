// Include these just for the editor, it doesn't hurt
#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>

#include <lua/lua.h>
#include <lua/lualib.h>
#include <lua/lauxlib.h>

#include <SDL.h>
#include <SDL_opengl.h>

#define TINYOBJ_LOADER_C_IMPLEMENTATION
char *dynamic_fgets(char **buf, size_t *size, FILE *file);
#include "tinyobj_loader_c.h"

/* gl_texture code ******************************************************************* */

typedef struct gl_texture {
	int width;
	int height;
	int num_pixels;
	size_t num_bytes;

	unsigned char *data;

	GLuint gl_id;
} gl_texture;

static gl_texture gl_texture_load_rgba(const char* fname, int width, int height)
{
	gl_texture t;
	FILE *f;
	size_t res;

	t.width = width;
	t.height = height;
	t.num_pixels = width * height;
	t.num_bytes = t.num_pixels * 4;

	t.data = malloc(t.num_bytes);

	printf("Loading: %s (%d x %d)\n", fname, width, height);
	f = fopen(fname, "rb");
	assert(f);
	res = fread(t.data, 1, t.num_bytes, f);
	assert(res == t.num_bytes);
	fclose(f);

	gl.GenTextures(1, &t.gl_id);

	printf("Texture %s loaded, id=%d\n", fname, t.gl_id);

	return t;
}

static void gl_texture_finalize(gl_texture *t)
{
	gl.BindTexture(GL_TEXTURE_2D, t->gl_id);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, /*border=*/0,
		GL_RGBA, GL_UNSIGNED_BYTE, t->data);

	GLint filtering = SDL_VIDEO_GL_filtering ? GL_LINEAR : GL_NEAREST;
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
}

/* gl_obj code ******************************************************************* */

typedef struct gl_obj {
    size_t num_shapes;
    size_t num_materials;

    tinyobj_shape_t *shapes;
    tinyobj_material_t *materials;
    tinyobj_attrib_t attrib;
} gl_obj;

// Needed by TinyObj
static void gl_obj_load_file(void *ctx, const char * filename, const int is_mtl, const char *obj_filename, char ** buffer, size_t * len)
{
    long string_size = 0, read_size = 0;
    FILE * handler = fopen(filename, "r");

    if (handler) {
        fseek(handler, 0, SEEK_END);
        string_size = ftell(handler);
        rewind(handler);
        *buffer = (char *) malloc(sizeof(char) * (string_size + 1));
        read_size = fread(*buffer, sizeof(char), (size_t) string_size, handler);
        (*buffer)[string_size] = '\0';
        if (string_size != read_size) {
            free(buffer);
            *buffer = NULL;
        }
        fclose(handler);
    }

    *len = read_size;
}

static gl_obj gl_obj_load(const char *path)
{
    gl_obj o;

    tinyobj_attrib_init(&o.attrib);

    // Enter into the file's directory, as we might need to load additional files from there
    char *olddir = alloca(MAXPATHLEN+1);
    char *dname = alloca(strlen(path)+1);
    char *bname = alloca(strlen(path)+1);

    olddir = getcwd(olddir, MAXPATHLEN+1);
    dname = dirname_r(path, dname);
    bname = basename_r(path, bname);


//	const char* filename = "beach-ball.obj";
	const char* filename = "ball-yoomp.obj";
//	const char* filename = "ball-amiga.obj";
//	const char* filename = "ball-amiga-2.obj";

	printf("Loading obj: %s\n", path);

    chdir(dname);

    int result = tinyobj_parse_obj(&o.attrib, &o.shapes, &o.num_shapes, &o.materials, &o.num_materials,
            bname, gl_obj_load_file, NULL, TINYOBJ_FLAG_TRIANGULATE);
	assert(result == TINYOBJ_SUCCESS);

	chdir(olddir);

	printf("%zd shapes, %zd materials\n", o.num_shapes, o.num_materials);
	printf("shape: %s %d %d\n", o.shapes[0].name, o.shapes[0].length, o.shapes[0].face_offset);
	printf("attribs: #v:%d #n:%d #tc:%d #f:%d #fnv: %d\n",
	    o.attrib.num_vertices, o.attrib.num_normals, o.attrib.num_texcoords, o.attrib.num_faces, o.attrib.num_face_num_verts);

    return o;
}

static void gl_obj_vertex(tinyobj_attrib_t *a, int idx)
{
	int v_idx = a->faces[idx].v_idx;
	gl.Normal3f(a->normals[3 * v_idx], a->normals[3 * v_idx + 1], a->normals[3 * v_idx + 2]);
	gl.Vertex3f(a->vertices[3 * v_idx], a->vertices[3 * v_idx + 1], a->vertices[3 * v_idx + 2]);
}

static void gl_obj_render(gl_obj *o)
{
	tinyobj_attrib_t *a = &o->attrib;
	int last_matid = -1;

	for (int f = 0; f < a->num_face_num_verts; f++) {
		assert(a->face_num_verts[f] == 3);
		int matid = a->material_ids[f];
		if (f == 0 || matid != last_matid) {
			if (f) {
				gl.End();
			}
			last_matid = matid;
			tinyobj_material_t mat = o->materials[matid];
			gl.Color4f(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1);
			gl.Begin(GL_TRIANGLES);
		}
		gl_obj_vertex(a, 3 * f + 0);
		gl_obj_vertex(a, 3 * f + 1);
		gl_obj_vertex(a, 3 * f + 2);
	}
	gl.End();

}

/* main code ******************************************************************* */

static gl_texture glt_background;
static gl_obj glo_ball;

static void xx_load_ball()
{
//	const char* filename = "beach-ball.obj";
	const char* filename = "data/ext/yoomp/ball-yoomp.obj";
//	const char* filename = "ball-amiga.obj";
//	const char* filename = "ball-amiga-2.obj";

    glo_ball = gl_obj_load(filename);
}

static void xx_load_background()
{
	glt_background = gl_texture_load_rgba("data/ext/yoomp/rof.rgba", 476, 476);

    gl_texture t = glt_background;

	float xc = t.width / 2.0f + 7;
	float yc = t.height / 2.0f;
	float rad = 120.0;
	float dark = 0.8 * rad;

	// Fix alphas
	for (int y = 0; y < t.width; y++) {
		for (int x = 0; x < t.width; x++) {
			int idx = y * t.width + x;
			float r = sqrt((x - xc) * (x - xc) + (y - yc) * (y - yc));
			int alpha = 0;
			if (r <= dark) {
				alpha = 255 - 255.0 * r / dark;
			} else if (r <= rad) {
				alpha = 0;
			} else {
				alpha = 255;
			}
			t.data[4 * idx + 3] = alpha;
		}
	}

	gl_texture_finalize(&glt_background);
}

static void xx_draw_background()
{
	float L = -0.77;
	float R = -L;
	float T = 0.9;
	float B = -0.75;

	float TL = 0.18;
	float TR = 0.84;
	float TT = 0.76;
	float TB = 0.25;

	gl.BindTexture(GL_TEXTURE_2D, glt_background.gl_id);

	gl.Disable(GL_DEPTH_TEST);
	gl.Color4f(1.0f, 1.0f, 1.0f, 1.0f);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(
		GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA
	);

	gl.Begin(GL_QUADS);
	gl.TexCoord2f(TL, TB);
	gl.Vertex3f(L, B, -2.0f);
	gl.TexCoord2f(TR, TB);
	gl.Vertex3f(R, B, -2.0f);
	gl.TexCoord2f(TR, TT);
	gl.Vertex3f(R, T, -2.0f);
	gl.TexCoord2f(TL, TT);
	gl.Vertex3f(L, T, -2.0f);
	gl.End();

	gl.Disable(GL_BLEND);
}

static float xx_last = 0;

static void xx_render_ball()
{
	gl.Disable(GL_TEXTURE_2D);

	gl.MatrixMode(GL_MODELVIEW);
	gl.PushMatrix();
	gl.LoadIdentity();

/*
	gl.Enable(GL_LIGHTING);
	gl.Enable(GL_LIGHT0);
	gl.Enable(GL_COLOR_MATERIAL);
	GLfloat light_pos[] = {0, 0.2, 2.5, 0};
	gl.Lightfv(GL_LIGHT0, GL_POSITION, light_pos);
*/
	xx_last++;

	const int EQU_BALL_X = 0x0030;
	const int EQU_BALL_VX = 0x0031;
	const int EQU_BALL_VY = 0x0032;

	float ball_angle = MEMORY_mem[EQU_BALL_X];
	float ball_vx = MEMORY_mem[EQU_BALL_VX];
	float ball_vy = MEMORY_mem[EQU_BALL_VY];

	gl.Translatef(
		(ball_vx - 128 + 4) / 84,
		- (ball_vy - 112 - 8) / 120,
		0);
	gl.Scalef(0.05, 0.07, 0.07);
	gl.Rotatef(ball_angle / 256.0 * 360.0, 0, 0, 1);
	gl.Rotatef(11 * xx_last, 1, 0, 0);

    gl_obj_render(&glo_ball);

	gl.PopMatrix();

	gl.Enable(GL_TEXTURE_2D);
	gl.Color4f(1, 1, 1, 1);
	gl.Disable(GL_LIGHTING);
	gl.Disable(GL_LIGHT0);
}

void xx_init()
{
    xx_load_background();
	xx_load_ball();
}

void lua_draw_background();

void xx_draw()
{
    lua_draw_background();

	const Uint8 *state = SDL_GetKeyState(NULL);
	if (!state[SDLK_RETURN]) {
		return;
	}

    xx_draw_background();
	xx_render_ball();
}

/* Lua extensions - texture ******************************************************************* */

#define LE_GLT "glt"

static int gl_le_glt_get(lua_State *L)
{
    gl_texture* glt = *(gl_texture**)luaL_checkudata(L, 1, LE_GLT);
    int index = luaL_checkinteger(L, 2);
    lua_pushnumber(L, glt->data[index]);
    return 1;
}

static int gl_le_glt_set(lua_State *L)
{
    gl_texture* glt = *(gl_texture**)luaL_checkudata(L, 1, LE_GLT);
    int index = luaL_checkinteger(L, 2);
    int value = luaL_checkinteger(L, 3);
    glt->data[index] = value;
    return 0;
}

static int gl_le_glt_width(lua_State *L)
{
    gl_texture* glt = *(gl_texture**)luaL_checkudata(L, 1, LE_GLT);
    lua_pushnumber(L, glt->width);
    return 1;
}

static int gl_le_glt_height(lua_State *L)
{
    gl_texture* glt = *(gl_texture**)luaL_checkudata(L, 1, LE_GLT);
    lua_pushnumber(L, glt->height);
    return 1;
}

static int gl_le_glt_num_pixels(lua_State *L)
{
    gl_texture* glt = *(gl_texture**)luaL_checkudata(L, 1, LE_GLT);
    lua_pushnumber(L, glt->num_pixels);
    return 1;
}

static int gl_le_glt_num_bytes(lua_State *L)
{
    gl_texture* glt = *(gl_texture**)luaL_checkudata(L, 1, LE_GLT);
    lua_pushnumber(L, glt->num_bytes);
    return 1;
}

static int gl_le_glt_gl_id(lua_State *L)
{
    gl_texture* glt = *(gl_texture**)luaL_checkudata(L, 1, LE_GLT);
    lua_pushnumber(L, glt->gl_id);
    return 1;
}

static int gl_le_glt_finalize(lua_State *L)
{
    gl_texture* glt = *(gl_texture**)luaL_checkudata(L, 1, LE_GLT);
    gl_texture_finalize(glt);
    return 0;
}

static int gl_le_glt_load_rgba(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);
    int width = luaL_checkinteger(L, 2);
    int height = luaL_checkinteger(L, 3);

    gl_texture** parray = lua_newuserdata(L, sizeof(gl_texture**));
    gl_texture *glt = malloc(sizeof(gl_texture));
    assert(glt);
    *glt = gl_texture_load_rgba(filename, width, height);
    *parray = glt;
    luaL_getmetatable(L, LE_GLT);
    lua_setmetatable(L, -2);
    printf("ok!\n");

    return 1;
}

static void gl_le_glt_create_type(lua_State* L) {
    static const struct luaL_Reg funcs[] = {
        { "get", gl_le_glt_get },
        { "set", gl_le_glt_set },
        { "width", gl_le_glt_width },
        { "height", gl_le_glt_height },
        { "num_pixels", gl_le_glt_num_pixels },
        { "num_bytes", gl_le_glt_num_bytes },
        { "gl_id", gl_le_glt_gl_id },
        { "finalize", gl_le_glt_finalize },
        NULL, NULL
    };
    luaL_newmetatable(L, LE_GLT);
    luaL_setfuncs(L, funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    lua_register(L, "glt_load_rgba", gl_le_glt_load_rgba);
}

/* Lua extensions - gl api ******************************************************************* */

#define LE_GL "gl_api"

static int gl_le_gl_Enable(lua_State *L)
{
    luaL_checkudata(L, 1, LE_GL);
    int value = luaL_checkinteger(L, 2);
    gl.Enable(value);
    return 0;
}

static int gl_le_gl_Disable(lua_State *L)
{
    luaL_checkudata(L, 1, LE_GL);
    int value = luaL_checkinteger(L, 2);
    gl.Disable(value);
    return 0;
}

static int gl_le_gl_Begin(lua_State *L)
{
    luaL_checkudata(L, 1, LE_GL);
    int value = luaL_checkinteger(L, 2);
    gl.Begin(value);
    return 0;
}

static int gl_le_gl_End(lua_State *L)
{
    luaL_checkudata(L, 1, LE_GL);
    gl.End();
    return 0;
}

static int gl_le_gl_Color4f(lua_State *L)
{
    luaL_checkudata(L, 1, LE_GL);
    float cr = luaL_checknumber(L, 2);
    float cg = luaL_checknumber(L, 3);
    float cb = luaL_checknumber(L, 4);
    float ca = luaL_checknumber(L, 5);
    gl.Color4f(cr, cg, cb, ca);
    return 0;
}

static int gl_le_gl_TexCoord2f(lua_State *L)
{
    luaL_checkudata(L, 1, LE_GL);
    float cx = luaL_checknumber(L, 2);
    float cy = luaL_checknumber(L, 3);
    gl.TexCoord2f(cx, cy);
    return 0;
}

static int gl_le_gl_Vertex3f(lua_State *L)
{
    luaL_checkudata(L, 1, LE_GL);
    float cx = luaL_checknumber(L, 2);
    float cy = luaL_checknumber(L, 3);
    float cz = luaL_checknumber(L, 4);
    gl.Vertex3f(cx, cy, cz);
    return 0;
}

static int gl_le_gl_BlendFunc(lua_State *L)
{
    luaL_checkudata(L, 1, LE_GL);
    int v1 = luaL_checkinteger(L, 2);
    int v2 = luaL_checkinteger(L, 3);
    gl.BlendFunc(v1, v2);
    return 0;
}

static int gl_le_gl_BindTexture(lua_State *L)
{
    luaL_checkudata(L, 1, LE_GL);
    int v1 = luaL_checkinteger(L, 2);
    int v2 = luaL_checkinteger(L, 3);
    gl.BindTexture(v1, v2);
    return 0;
}


static int gl_le_gl_api(lua_State* L) {
    printf("%s: gl=%p\n", __FUNCTION__, &gl);
    void** parray = lua_newuserdata(L, sizeof(void**));
    *parray = &gl;
    luaL_getmetatable(L, LE_GL);
    lua_setmetatable(L, -2);
    return 1;
}

#define push_integer_value(_id) \
    lua_pushinteger(L, _id); \
    lua_setfield(L, -2, #_id);

static void gl_le_gl_create_type(lua_State* L) {
    static const struct luaL_Reg funcs[] = {
        { "Enable", gl_le_gl_Enable },
        { "Disable", gl_le_gl_Disable },
        { "Begin", gl_le_gl_Begin },
        { "End", gl_le_gl_End },
        { "Color4f", gl_le_gl_Color4f },
        { "BlendFunc", gl_le_gl_BlendFunc },
        { "BindTexture", gl_le_gl_BindTexture },
        { "TexCoord2f", gl_le_gl_TexCoord2f },
        { "Vertex3f", gl_le_gl_Vertex3f },
        NULL, NULL
    };
    luaL_newmetatable(L, LE_GL);
    luaL_setfuncs(L, funcs, 0);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    push_integer_value(GL_BLEND);
    push_integer_value(GL_TEXTURE_2D);
    push_integer_value(GL_DEPTH_TEST);
    push_integer_value(GL_SRC_ALPHA);
    push_integer_value(GL_ONE_MINUS_SRC_ALPHA);
    push_integer_value(GL_QUADS);

    lua_register(L, "gl_api", gl_le_gl_api);
}

void gl_lua_ext_init(lua_State *L)
{
    gl_le_glt_create_type(L);
    gl_le_gl_create_type(L);
}