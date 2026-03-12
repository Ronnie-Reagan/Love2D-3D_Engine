local keyDown = {}

package.loaded["love"] = {
	keyboard = {
		isDown = function(...)
			local keys = { ... }
			for i = 1, #keys do
				if keyDown[keys[i]] then
					return true
				end
			end
			return false
		end
	},
	mouse = {
		isDown = function()
			return false
		end
	}
}

package.loaded["Source.Input.Controls"] = nil
local controls = require("Source.Input.Controls")

local function assertTrue(value, message)
	if not value then
		error(message or "assertion failed", 2)
	end
end

local function run()
	local action = controls.getAction("walk_forward")
	assertTrue(type(action) == "table", "walk_forward action should exist")

	action.bindings[1] = controls.bindKey("i")

	keyDown.w = true
	assertTrue(not controls.isActionDown("walk_forward"), "legacy key should stop triggering after remap")
	keyDown.w = nil

	keyDown.i = true
	assertTrue(controls.isActionDown("walk_forward"), "new key should trigger after remap")
	keyDown.i = nil

	controls.resetToDefaults()
	local resetAction = controls.getAction("walk_forward")
	assertTrue(resetAction and resetAction.bindings and resetAction.bindings[1], "default bindings should restore")

	keyDown.w = true
	assertTrue(controls.isActionDown("walk_forward"), "default key should trigger again after reset")
	keyDown.w = nil

	print("Controls rebind tests passed")
end

run()
