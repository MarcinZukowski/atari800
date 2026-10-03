#ifndef SDL_VIDEO_GL_COMMON_H
#define SDL_VIDEO_GL_COMMON_H

#include <SDL.h>
#include <SDL_opengl.h>

/* Pointers to OpenGL functions, loaded dynamically during initialisation. */
struct glapi
{
	void(APIENTRY*Viewport)(GLint,GLint,GLsizei,GLsizei);
	void(APIENTRY*Scissor)(GLint,GLint,GLsizei,GLsizei);
	void(APIENTRY*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
	void(APIENTRY*Clear)(GLbitfield);
	void(APIENTRY*Enable)(GLenum);
	void(APIENTRY*Disable)(GLenum);
	void(APIENTRY*GenTextures)(GLsizei, GLuint*);
	void(APIENTRY*DeleteTextures)(GLsizei, const GLuint*);
	void(APIENTRY*BindTexture)(GLenum, GLuint);
	void(APIENTRY*TexParameteri)(GLenum, GLenum, GLint);
	void(APIENTRY*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
	void(APIENTRY*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
	void(APIENTRY*TexCoord2f)(GLfloat, GLfloat);
	void(APIENTRY*Vertex3f)(GLfloat, GLfloat, GLfloat);
	void(APIENTRY*Normal3f)(GLfloat, GLfloat, GLfloat);
	void(APIENTRY*Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
	void(APIENTRY*BlendFunc)(GLenum,GLenum);
	void(APIENTRY*MatrixMode)(GLenum);
	void(APIENTRY*Ortho)(GLdouble,GLdouble,GLdouble,GLdouble,GLdouble,GLdouble);
	void(APIENTRY*Frustum)(GLdouble,GLdouble,GLdouble,GLdouble,GLdouble,GLdouble);
	void(APIENTRY*LoadIdentity)(void);
	void(APIENTRY*Begin)(GLenum);
	void(APIENTRY*End)(void);
	void(APIENTRY*PushMatrix)(void);
	void(APIENTRY*PopMatrix)(void);
	void(APIENTRY*PushAttrib)(GLbitfield);
	void(APIENTRY*PopAttrib)(void);
	void(APIENTRY*Rotatef)(GLfloat, GLfloat, GLfloat, GLfloat);
	void(APIENTRY*Translatef)(GLfloat, GLfloat, GLfloat);
	void(APIENTRY*Scalef)(GLfloat, GLfloat, GLfloat);
	void(APIENTRY*Lightfv)(GLenum, GLenum, const GLfloat*);
	void(APIENTRY*Fogf)(GLenum, GLfloat);
	void(APIENTRY*Fogfv)(GLenum, const GLfloat*);
	void(APIENTRY*PolygonMode)(GLenum, GLenum);
	void(APIENTRY*CullFace)(GLenum);
	void(APIENTRY*FrontFace)(GLenum);
	void(APIENTRY*LineWidth)(GLfloat);
	void(APIENTRY*GetIntegerv)(GLenum, GLint*);
	const GLubyte*(APIENTRY*GetString)(GLenum);
	GLuint(APIENTRY*GenLists)(GLsizei);
	void(APIENTRY*DeleteLists)(GLuint, GLsizei);
	void(APIENTRY*NewList)(GLuint, GLenum);
	void(APIENTRY*EndList)(void);
	void(APIENTRY*CallList)(GLuint);
	void(APIENTRY*GenBuffersARB)(GLsizei, GLuint*);
	void(APIENTRY*DeleteBuffersARB)(GLsizei, const GLuint*);
	void(APIENTRY*BindBufferARB)(GLenum, GLuint);
	void(APIENTRY*BufferDataARB)(GLenum, GLsizeiptr, const GLvoid*, GLenum);
	void*(APIENTRY*MapBuffer)(GLenum, GLenum);
	GLboolean(APIENTRY*UnmapBuffer)(GLenum);
#if SDL2
	GLenum (APIENTRY* GetError)(void);
	GLuint (APIENTRY* CreateShader)(GLenum);
	void   (APIENTRY* ShaderSource)(GLuint shader, GLsizei count, GLchar* const* string, const GLint* length);
	GLuint (APIENTRY* CreateProgram)(void);
	void   (APIENTRY* CompileShader)(GLuint shader);
	void   (APIENTRY* GetShaderiv)(GLuint shader, GLenum pname, GLint* params);
	void   (APIENTRY* GetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
	void   (APIENTRY* AttachShader)(GLuint program, GLuint shader);
	void   (APIENTRY* LinkProgram)(GLuint program);
	void   (APIENTRY* GetProgramiv)(GLuint program, GLenum pname, GLint* params);
	void   (APIENTRY* GetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
	void   (APIENTRY* DeleteShader)(GLuint shader);
	void   (APIENTRY* UseProgram)(GLuint program);
	void   (APIENTRY* Uniform1f)(GLint location, GLfloat v0);
	void   (APIENTRY* Uniform2f)(GLint location, GLfloat v0, GLfloat v1);
	void   (APIENTRY* Uniform1i)(GLint location, GLint v0);
	void   (APIENTRY* UniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
	void   (APIENTRY* ActiveTexture)(GLenum texture);
	void   (APIENTRY* VertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
	void   (APIENTRY* EnableVertexAttribArray)(GLuint index);
	void   (APIENTRY* GenVertexArrays)(GLsizei n, GLuint* arrays);
	void   (APIENTRY* BindVertexArray)(GLuint array);
	void   (APIENTRY* GenBuffers)(GLsizei n, GLuint* buffers);
	void   (APIENTRY* BufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
	void   (APIENTRY* BindBuffer)(GLenum target, GLuint buffer);
	GLint  (APIENTRY* GetUniformLocation)(GLuint program, const GLchar* name);
	GLint  (APIENTRY* GetAttribLocation)(GLuint program, const GLchar* name);
	void   (APIENTRY* DrawElements)(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);
#endif
};

extern struct glapi gl;

extern int SDL_VIDEO_GL_filtering;

#endif  /* SDL_VIDEO_GL_COMMON_H */
