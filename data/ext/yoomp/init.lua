ext_register({
	NAME = "Yoomp! LUA HACK by Eru",

	-- We'll look for a memory fingerprint to turn this extension on
	ENABLE_CHECK_ADDRESS = 0x3600,
	ENABLE_CHECK_FINGERPRINT = { 0x20, 0x00, 0xB0, 0x20, 0xBC, 0x3D },

	-- Have we been initialized already,
	initialized = false,
	-- Handle to OpenGL API
	gl = nil,
	-- 3D ball objects - filled in inside initialize()
	balls = { nil, nil, nil, nil, nil, nil },
	-- Names of files with 3D balls
	ball_files = {
		nil,
		"data/ext/yoomp/ball-yoomp-bw.obj",
		"data/ext/yoomp/ball-yoomp.obj",
		"data/ext/yoomp/ball-amiga.obj",
		"data/ext/yoomp/ball-amiga-2.obj",
		"data/ext/yoomp/beach-ball.obj",
	},
	-- Handle for the background
	background = nil,

	-- Initialization function, lazily called
	init = function(self)
		if self.initialized then
			return
		end

		print("Loading Yoomp! resources")

		self.gl = gl_api();

		-- Load balls
		for i = 2, 6 do
			self.balls[i] = glo_load(self.ball_files[i]);
		end

		-- Load background
		self.background = glt_load_rgba("data/ext/yoomp/rof-gray.rgba", 476, 476);

		-- Fix alphas in the background bitmap
		local height = self.background:height()
		local width = self.background:width()

	   -- We make the center semi-transparent
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
				self.background:set(4 * idx + 3, alpha);
			end
		end

		-- Finalize texture
		self.background:finalize();

		print("Yoomp! Lua script initialized")
		self.initialized = true
	end,

	yoomp_draw_background = function(self)
		if self.MENU.BKG.CURRENT == 0 then
			return
		end

		local gl = self.gl;
		local a8mem = a8_memory();

	   -- Screen coordinates
		local L = -0.77;
		local R = -L;
		local T = 0.9;
		local B = -0.75;

	   --  Texture coordinates
		local TL = 0.18;
		local TR = 0.84;
		local TT = 0.76;
		local TB = 0.25;

		gl:BindTexture(gl.GL_TEXTURE_2D, self.background:gl_id());

		gl:Disable(gl.GL_DEPTH_TEST);
		gl:Enable(gl.GL_BLEND);
		gl:BlendFunc(
			gl.GL_SRC_ALPHA,
			gl.GL_ONE_MINUS_SRC_ALPHA
		);

		-- Colorize the texture to try to match the current tunnel
	   -- 4F60 has background color
		local color = a8mem:get(0x4F60) | 0x0F;  -- brightest
		local colR = a8_Colours_GetR(color) / 255.0;
		local colG = a8_Colours_GetG(color) / 255.0;
		local colB = a8_Colours_GetB(color) / 255.0;

		gl:Disable(gl.GL_DEPTH_TEST);
		gl:Color4f(colR, colG, colB, 1.0);

	   -- Draw the texture
		ext_gl_draw_quad(gl, TL, TR, TT, TB, L, R, T, B, -2.0);

	   -- Clean up
		gl:Color4f(1.0, 1.0, 1.0, 1.0);
		gl:Disable(gl.GL_BLEND);
	end,

	-- counter we use for ball animation
	ball_counter = 0,

	-- ball drawing logic
	yoomp_draw_ball = function(self)
		local ball_nr = self.MENU.BALL.CURRENT;

		if ball_nr == 0 then
		  -- Original Atari ball, don't draw anything
			return
		end

		local gl = self.gl;
		local a8mem = a8_memory();

	   -- Our balls don't have textures
	   gl:Disable(gl.GL_TEXTURE_2D);

	   -- Determine the ball location
		gl:MatrixMode(gl.GL_MODELVIEW);
		gl:PushMatrix();
		gl:LoadIdentity();

	   -- Mark time progress
	   self.ball_counter = self.ball_counter + 1;

	   -- Read ball position from Atari memory
		local EQU_BALL_X = 0x0030;
		local EQU_BALL_VX = 0x0031;
		local EQU_BALL_VY = 0x0032;

		local ball_angle = a8mem:get(EQU_BALL_X);
		local ball_vx = a8mem:get(EQU_BALL_VX);
		local ball_vy = a8mem:get(EQU_BALL_VY);

	   --  Set the ball location and rotation
		gl:Translatef(
			(ball_vx - 128 + 4) / 84,
			- (ball_vy - 112 - 8) / 120,
			0);
		gl:Scalef(0.05, 0.07, 0.07);
		gl:Rotatef(ball_angle / 256.0 * 360.0, 0, 0, 1);
		gl:Rotatef(11 * self.ball_counter, 1, 0, 0);

		local colR, colG, colB;
		if ball_nr == 1 then
		  -- Try to match the color of the ball
			-- 4F5c has ball color
			local color = a8mem:get(0x4F5c) | 0x0c;  -- brightness
			colR = a8_Colours_GetR(color) / 255.0;
			colG = a8_Colours_GetG(color) / 255.0;
			colB = a8_Colours_GetB(color) / 255.0;
		else
		  -- Use the original model colors
			colR = 1.0;
			colG = 1.0;
			colB = 1.0;
		end

	   -- Render the current ball
		local ball = self.balls[ball_nr + 1]
		ball:render_colorized(colR, colG, colB);

		-- Clean up
		gl:PopMatrix();
		gl:Enable(gl.GL_TEXTURE_2D);
		gl:Color4f(1, 1, 1, 1);
	end,

	-- This is called every Atari frame, after the frame is rendered
	POST_GL_FRAME = function(self)
		self.init(self)

		-- Only display during the game
		if antic_dlist() ~= 0xCA00 then
			return
		end

		self.yoomp_draw_background(self)
		self.yoomp_draw_ball(self)
	end,  -- POST_GL_FRAME

	-- MENU
	MENU = {
		BKG = { LABEL = "Nicer background:", OPTIONS = { "OFF", "ON" }, CURRENT = 1 },
		BALL = { LABEL = "Ball type:",
			-- 1..6
			OPTIONS = { "ORIGINAL", "Yoomp-like-colorized", "Yoomp-like-green", "Amiga V1", "Amiga V2", "Beach Ball" },
			CURRENT = 1 }
	}
})
