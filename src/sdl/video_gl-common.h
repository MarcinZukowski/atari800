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
	void(APIENTRY*LineWidth)(GLfloat);
	void(APIENTRY*GetIntegerv)(GLenum, GLint*);
	const GLubyte*(APIENTRY*GetString)(GLenum);
	GLuint(APIENTRY*GenLists)(GLsizei);
	void(APIENTRY*DeleteLists)(GLuint, GLsizei);
	void(APIENTRY*NewList)(GLuint, GLenum);
	void(APIENTRY*EndList)(void);
	void(APIENTRY*CallList)(GLuint);
	void(APIENTRY*GenBuffers)(GLsizei, GLuint*);
	void(APIENTRY*DeleteBuffers)(GLsizei, const GLuint*);
	void(APIENTRY*BindBuffer)(GLenum, GLuint);
	void(APIENTRY*BufferData)(GLenum, GLsizeiptr, const GLvoid*, GLenum);
	void*(APIENTRY*MapBuffer)(GLenum, GLenum);
	GLboolean(APIENTRY*UnmapBuffer)(GLenum);
};

extern struct glapi gl;

extern int SDL_VIDEO_GL_filtering;

#endif  /* SDL_VIDEO_GL_COMMON_H */
