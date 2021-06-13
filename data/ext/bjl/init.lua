ext_register({
	NAME = "BJL LUA HACK by Eru",

	-- We'll look for a memory fingerprint to turn this extension on
	ENABLE_CHECK_ADDRESS = 0x41FC,
	ENABLE_CHECK_FINGERPRINT = { 0x6A, 0x61, 0x67, 0x67, 0x69 },

	-- We'll intercept execution at these points
	CODE_INJECTION_LIST = { 0xb51c, 0x9da7, 0xaf32 },
	CODE_INJECTION_FUNCTION = function(pc, op)
		if ext_acceleration_disabled() then
			return op
		end
		-- For all entry points, just run until RTS
		return ext_fakecpu_until_op(OP_RTS)
	end,

	-- Show FPS
	PRE_GL_FRAME = function()
		ext_print_fps(antic_dlist(), 0x9f, 0x90, 0, -2)
	end
})