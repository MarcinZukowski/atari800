-- Draw a textured quad on a 2d plane
function ext_gl_draw_quad(
        gl,             -- handle to gl_api
        TL, TR, TT, TB, -- texture left/right/top/bottom coordinates
        L, R, T, B,     -- world left/right/top/bottom coordinates
        Z               -- world Z coordinate
        )
    gl:Begin(gl.GL_QUADS);
    gl:TexCoord2f(TL, TB);
    gl:Vertex3f(L, B, -2.0);
    gl:TexCoord2f(TR, TB);
    gl:Vertex3f(R, B, -2.0);
    gl:TexCoord2f(TR, TT);
    gl:Vertex3f(R, T, -2.0);
    gl:TexCoord2f(TL, TT);
    gl:Vertex3f(L, T, -2.0);
    gl:End();
end
