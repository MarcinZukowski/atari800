#include "sdl/video_gl-ext.h"
#include "sdl/video_gl-common.h"

#include <libgen.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/param.h>

#include "memory.h"

#define TINYOBJ_LOADER_C_IMPLEMENTATION
char *dynamic_fgets(char **buf, size_t *size, FILE *file);
#include "3rd-party/tinyobj_loader_c.h"

/* gl_texture code ******************************************************************* */

gl_texture gl_texture_new(int width, int height)
{
	gl_texture t;

	t.width = width;
	t.height = height;
	t.num_pixels = width * height;
	t.num_bytes = t.num_pixels * 4;

	t.data = malloc(t.num_bytes);
	assert(t.data);

	gl.GenTextures(1, &t.gl_id);

	return t;
}

gl_texture gl_texture_load_rgba(const char* fname, int width, int height)
{
	FILE *f;
	size_t res;

	gl_texture t = gl_texture_new(width, height);

	printf("Loading: %s (%d x %d)\n", fname, width, height);
	f = fopen(fname, "rb");
	assert(f);
	res = fread(t.data, 1, t.num_bytes, f);
	assert(res == t.num_bytes);
	fclose(f);

	printf("Texture %s loaded, id=%d\n", fname, t.gl_id);

	return t;
}

void gl_texture_finalize(gl_texture *t)
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

void gl_texture_draw(gl_texture *t,
		float tex_l, float tex_r, float tex_t, float tex_b,
		float scr_l, float scr_r, float scr_t, float scr_b,
		float z)
{
	gl.BindTexture(GL_TEXTURE_2D, t->gl_id);

	gl.Begin(GL_QUADS);
	gl.TexCoord2f(tex_l, tex_b);
	gl.Vertex3f(scr_l, scr_b, z);
	gl.TexCoord2f(tex_r, tex_b);
	gl.Vertex3f(scr_r, scr_b, z);
	gl.TexCoord2f(tex_r, tex_t);
	gl.Vertex3f(scr_r, scr_t, z);
	gl.TexCoord2f(tex_l, tex_t);
	gl.Vertex3f(scr_l, scr_t, z);
	gl.End();
}


/* gl_obj code ******************************************************************* */

typedef struct gl_obj {
	size_t num_shapes;
	size_t num_materials;

	tinyobj_shape_t *shapes;
	tinyobj_material_t *materials;
	tinyobj_attrib_t attrib;
} gl_obj;

/* Needed by TinyObj */
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

struct gl_obj* gl_obj_load(const char *path)
{
	gl_obj* o = malloc(sizeof(struct gl_obj));
	assert(o);

	tinyobj_attrib_init(&o->attrib);

	/* Enter into the file's directory, as we might need to load additional files from there */
	char *olddir = alloca(MAXPATHLEN+1);
	char *dname = alloca(strlen(path)+1);
	char *bname = alloca(strlen(path)+1);

	olddir = getcwd(olddir, MAXPATHLEN+1);
	dname = dirname_r(path, dname);
	bname = basename_r(path, bname);

	printf("Loading obj: %s\n", path);

	chdir(dname);

	int result = tinyobj_parse_obj(&o->attrib, &o->shapes, &o->num_shapes, &o->materials, &o->num_materials,
			bname, gl_obj_load_file, NULL, TINYOBJ_FLAG_TRIANGULATE);
	assert(result == TINYOBJ_SUCCESS);

	chdir(olddir);

	printf("%zd shapes, %zd materials\n", o->num_shapes, o->num_materials);
	printf("shape: %s %d %d\n", o->shapes[0].name, o->shapes[0].length, o->shapes[0].face_offset);
	printf("attribs: #v:%d #n:%d #tc:%d #f:%d #fnv: %d\n",
		o->attrib.num_vertices, o->attrib.num_normals, o->attrib.num_texcoords, o->attrib.num_faces, o->attrib.num_face_num_verts);

	return o;
}

/* Helper function */
static void gl_obj_vertex(tinyobj_attrib_t *a, int idx)
{
	int v_idx = a->faces[idx].v_idx;
	gl.Normal3f(a->normals[3 * v_idx], a->normals[3 * v_idx + 1], a->normals[3 * v_idx + 2]);
	gl.Vertex3f(a->vertices[3 * v_idx], a->vertices[3 * v_idx + 1], a->vertices[3 * v_idx + 2]);
}

void gl_obj_render_colorized(gl_obj *o, float multR, float multG, float multB)
{
	tinyobj_attrib_t *a = &o->attrib;
	int last_matid = -1;

	int f;
	for (f = 0; f < a->num_face_num_verts; f++) {
		assert(a->face_num_verts[f] == 3);
		int matid = a->material_ids[f];
		if (f == 0 || matid != last_matid) {
			if (f) {
				gl.End();
			}
			last_matid = matid;
			tinyobj_material_t mat = o->materials[matid];
			gl.Color4f(mat.diffuse[0] * multR, mat.diffuse[1] * multG, mat.diffuse[2] * multB, 1);
			gl.Begin(GL_TRIANGLES);
		}
		gl_obj_vertex(a, 3 * f + 0);
		gl_obj_vertex(a, 3 * f + 1);
		gl_obj_vertex(a, 3 * f + 2);
	}
	gl.End();
}

void gl_obj_render(gl_obj *o)
{
	gl_obj_render_colorized(o, 1.0f, 1.0f, 1.0f);
}
