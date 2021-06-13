ext_register({
	NAME = "BJL LUA HACK by Eru",

	-- We'll look for a memory fingerprint to turn this extension on
	ENABLE_CHECK_ADDRESS = 0x41FC,
	ENABLE_CHECK_FINGERPRINT = { 0x6A, 0x61, 0x67, 0x67, 0x69 },

	-- We'll intercept execution at these points
	CODE_INJECTION_LIST = { 0xb51c, 0x9da7, 0xaf32 },
	CODE_INJECTION_FUNCTION = function(self, pc, op)
		if ext_acceleration_disabled() or self.MENU.ACCEL.CURRENT == 0 then
			return op
		end
		-- Accelerate this one on LOW and HIGH
		if pc == 0xb51c then
			return ext_fakecpu_until_op(OP_RTS)
		end
		-- The others accelerate only on HIGH
		if self.MENU.ACCEL.CURRENT == 2 then
			return ext_fakecpu_until_op(OP_RTS)
		end
		-- No acceleration
		return op
	end,

	-- Show FPS
	PRE_GL_FRAME = function(self)
		if (self.MENU.FPS.CURRENT == 1) then
			ext_print_fps(antic_dlist(), 0x9f, 0x90, 0, -2)
		end
	end,

	-- MENU
	MENU = {
		FPS = { LABEL = "Display FPS:", OPTIONS = { "OFF", "ON" }, CURRENT = 1 },
		ACCEL = { LABEL = "Acceleration:", OPTIONS = { "NO", "LOW", "HIGH" }, CURRENT = 2 },
	}
})
