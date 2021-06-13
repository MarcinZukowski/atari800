ext_register({
	NAME = "ALT.REAL. LUA HACK by Eru",

	-- We'll look for a memory fingerprint to turn this extension on
	ENABLE_CHECK_ADDRESS = 0x29B6,
	ENABLE_CHECK_FINGERPRINT = { 0x44, 0x75, 0x6E, 0x67, 0x65, 0x6F, 0x6E },

	-- Use this one for detecting FPS
	CALLS_7856 = 0,

	-- We'll intercept execution at these points
	CODE_INJECTION_LIST = { 0x7856, 0x0090, 0x4A69, 0x3884, 0x7858, 0x7A1F, 0x7F1B },
	CODE_INJECTION_FUNCTION = function(self, pc, op)
		if (pc == 0x7856) then -- Moving into font memory? This is once per drawn frame
			self.CALLS_7856 = self.CALLS_7856 + 1
		end

		if ext_acceleration_disabled() or self.MENU.ACCEL.CURRENT == 0 then
			return op
		end

		-- This one on LOW and HIGH
		if (pc == 0x90) then  -- Drawing?
			return ext_fakecpu_until_pc(0x00D5);
		end

		if self.MENU.ACCEL.CURRENT == 1 then
			return op;
		end

		-- These on HIGH only
		if (pc == 0x4A69) then  -- ??
			return ext_fakecpu_until_pc(0x4A82);
		end

		if (pc == 0x3884) then  -- ??
			return ext_fakecpu_until_pc(0x38CE);
		end

		if (pc == 0x7858) then  -- Moving into font memory?
			return ext_fakecpu_until_pc(0x7887);
		end

		if (pc == 0x7A1F) then  -- ??
			return ext_fakecpu_until_pc(0x7A36);
		end

		if (pc == 0x7F1B) then -- ??
			return ext_fakecpu_until_pc(0x7F4A);
		end

		return op;
	end,
	-- Show FPS
	PRE_GL_FRAME = function(self)
		if (self.MENU.FPS.CURRENT == 1) then
			ext_print_fps(self.CALLS_7856, 0x9f, 0x90, 0, -2)
		end
	end,

	-- MENU
	MENU = {
		FPS = { LABEL = "Display FPS:", OPTIONS = { "OFF", "ON" }, CURRENT = 1 },
		ACCEL = { LABEL = "Acceleration:", OPTIONS = { "NO", "LOW", "HIGH" }, CURRENT = 1 },
	}
})
