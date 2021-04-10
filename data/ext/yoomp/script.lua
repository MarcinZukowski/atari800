function yoomp_initialize()
    print("yoomp_initialize: START\n");
    glt_background = glt_load_rgba("data/ext/yoomp/rof.rgba", 476, 476);
    print(glt_background);

    local height = glt_background:height()
    local width = glt_background:width()

	local xc = width / 2.0 + 7;
	local yc = height / 2.0;
	local rad = 120.0;
	local dark = 0.8 * rad;

    for y = 0, height - 1
    do
        for x = 0, width - 1
        do
            local idx = y * width + x;
			local r = math.sqrt((x - xc) * (x - xc) + (y - yc) * (y - yc));
			local alpha = 0;
			if r <= dark then
				alpha = 255 - 255.0 * r / dark;
            elseif r <= rad then
				alpha = 0;
			else
				alpha = 255;
            end
            alpha = math.floor(alpha)
            glt_background:set(4 * idx + 3, alpha);
        end
    end

    glt_background:finalize();

    gl = gl_api();
    print(gl_api);
    print("yoomp_initialize: DONE\n");
end

function yoomp_draw_background()
    local L = -0.77;
    local R = -L;
    local T = 0.9;
    local B = -0.75;

    local TL = 0.18;
    local TR = 0.84;
    local TT = 0.76;
    local TB = 0.25;

    print(glt_background);
    print(gl);
    print(gl.GL_TEXTURE_2D);

    gl:BindTexture(gl.GL_TEXTURE_2D, glt_background:gl_id());

    gl:Disable(gl.GL_DEPTH_TEST);
    gl:Color4f(1.0, 1.0, 1.0, 1.0);
    gl:Enable(gl.GL_BLEND);
    gl:BlendFunc(
        gl.GL_SRC_ALPHA,
        gl.GL_ONE_MINUS_SRC_ALPHA
    );

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

    gl:Disable(gl.GL_BLEND);
end

print("Loaded");