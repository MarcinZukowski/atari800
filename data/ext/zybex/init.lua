ext_register({
	NAME = "ZYBEX LUA HACK by Eru",

	-- We'll look for a memory fingerprint to turn this extension on
	ENABLE_CHECK_ADDRESS = 0x3000,
	ENABLE_CHECK_FINGERPRINT = { 0x18, 0x69, 0x14, 0xA8, 0xC0, 0x50 },

	INITIALIZED = false,

	INIT = function(self)
		if self.INITIALIZED then
			return
		end
		print("Loading Zybex textures")

		local gl = gl_api();

		self.TEXTURE_COLOR = glt_load_rgba("data/ext/zybex/bkg1-512x512.rgba", 512, 512);
		self.TEXTURE_COLOR:finalize()
		gl:TexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_S, gl.GL_REPEAT);
		gl:TexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_T, gl.GL_REPEAT);

		self.TEXTURE_GRAY = glt_load_rgba("data/ext/zybex/bkg1-gs-512x512.rgba", 512, 512);
		self.TEXTURE_GRAY:finalize()
		gl:TexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_S, gl.GL_REPEAT);
		gl:TexParameteri(gl.GL_TEXTURE_2D, gl.GL_TEXTURE_WRAP_T, gl.GL_REPEAT);

		self.INITIALIZED = true
	end,

	PRE_GL_FRAME = function(self)
		-- Show FPS
		if (self.MENU.FPS.CURRENT == 1) then
			ext_print_fps(antic_dlist(), 0x9f, 0x90, 0, -2)
		end
	end,

	last_hscrol = 0,
	texture_hscrol = 0,

	POST_GL_FRAME = function(self)
		self.INIT(self)

		if self.MENU.BKG.CURRENT == 0 then
			return
		end

		-- Draw background

		local gl = gl_api();

		-- Screen coords
		local L = -0.95;
		local R = -L;
		local T = 0.78;
		local B = -0.65;

		-- If hscrol changes, the screen moved, adjust texture
		if (antic_hscrol() ~= self.last_hscrol) then
			self.texture_hscrol = self.texture_hscrol + 1;
			self.last_hscrol = ANTIC_HSCROL;
		end

		-- Texture coords
		local TL = 0.0015 * self.texture_hscrol;
		local TR = TL + 0.5;
		local TT = 0.70;
		local TB = 0.30;

		-- Texture id
		local texture = (self.MENU.BKG.CURRENT == 1) and self.TEXTURE_GRAY or self.TEXTURE_COLOR
		local texture_id = texture:gl_id()

		gl:BindTexture(gl.GL_TEXTURE_2D, texture_id);

		gl:Disable(gl.GL_DEPTH_TEST);
		gl:Color4f(0.2, 0.2, 0.2, 0.5);
		gl:Enable(gl.GL_BLEND);
		gl:BlendFunc(gl.GL_ONE_MINUS_DST_COLOR, gl.GL_ONE);

		ext_gl_draw_quad(gl, TL, TR, TT, TB, L, R, T, B, Z);

		gl:Color4f(1.0, 1.0, 1.0, 1.0);
		gl:Disable(gl.GL_BLEND);
	end,  -- POST_GL_FRAME

	-- MENU
	MENU = {
		FPS = { LABEL = "Display FPS:", OPTIONS = { "OFF", "ON" }, CURRENT = 1 },
		BKG = { LABEL = "Background:", OPTIONS = { "OFF", "GRAYSCALE", "COLOR" }, CURRENT = 2 },
	}
})
