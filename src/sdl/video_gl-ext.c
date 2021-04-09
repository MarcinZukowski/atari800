// Include these just for the editor, it doesn't hurt
#include <SDL.h>
#include <SDL_opengl.h>

#include <stdio.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>

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

	printf("Loading: %s\n", fname);
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

static void gl_obj_render(gl_obj *o)
{
	tinyobj_attrib_t *a = &glo_ball.attrib;
	int last_matid = -1;

	for (int f = 0; f < a->num_face_num_verts; f++) {
		assert(a->face_num_verts[f] == 3);
		int matid = a->material_ids[f];
		if (f == 0 || matid != last_matid) {
			if (f) {
				gl.End();
			}
			last_matid = matid;
			tinyobj_material_t mat = glo_ball.materials[matid];
			gl.Color4f(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1);
			gl.Begin(GL_TRIANGLES);
		}
		xx_v3(a, 3 * f + 0);
		xx_v3(a, 3 * f + 1);
		xx_v3(a, 3 * f + 2);
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

void xx_draw_background()
{
	gl.Enable(GL_BLEND);

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

static void xx_v3(tinyobj_attrib_t *a, int idx)
{
	int v_idx = a->faces[idx].v_idx;
	gl.Normal3f(a->normals[3 * v_idx], a->normals[3 * v_idx + 1], a->normals[3 * v_idx + 2]);
	gl.Vertex3f(a->vertices[3 * v_idx], a->vertices[3 * v_idx + 1], a->vertices[3 * v_idx + 2]);
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
	gl.Rotatef(90, 0, 0, 1);

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

void xx_draw()
{
	const Uint8 *state = SDL_GetKeyState(NULL);
	if (!state[SDLK_RETURN]) {
		return;
	}

    xx_draw_background();
	xx_render_ball();
}
