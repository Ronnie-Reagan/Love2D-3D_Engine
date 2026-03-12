function love.conf(t)
	t.console = true
	t.modules.window = false
	t.modules.graphics = false
	t.modules.audio = false
	t.modules.sound = false
	t.modules.video = false
	t.modules.mouse = false
	t.modules.joystick = false
	t.modules.touch = false
	netMode = "dedicated"
end
