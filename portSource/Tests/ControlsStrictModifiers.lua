local keyDown = {}
local mouseDown = {}

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
        isDown = function(button)
            return mouseDown[button] and true or false
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
    local defaultValue = controls.getActionMouseAxisValue("walk_look_right", 12, 0, {
        alt = false,
        ctrl = false,
        shift = false
    })
    assertTrue(defaultValue > 0, "baseline strict axis binding should trigger without extra modifiers")

    local extraCtrl = controls.getActionMouseAxisValue("walk_look_right", 12, 0, {
        alt = false,
        ctrl = true,
        shift = false
    })
    assertTrue(extraCtrl > 0, "plain axis binding should remain active while Ctrl is held")

    local extraShift = controls.getActionMouseAxisValue("walk_look_right", 12, 0, {
        alt = false,
        ctrl = false,
        shift = true
    })
    assertTrue(extraShift > 0, "plain axis binding should remain active while Shift is held")

    local extraAlt = controls.getActionMouseAxisValue("walk_look_right", 12, 0, {
        alt = true,
        ctrl = false,
        shift = false
    })
    assertTrue(extraAlt > 0, "plain axis binding should remain active while Alt is held")

    local lookAction = controls.getAction("walk_look_right")
    lookAction.bindings[1].modifiers = { ctrl = true }
    local ctrlStrict = controls.getActionMouseAxisValue("walk_look_right", 12, 0, {
        alt = false,
        ctrl = true,
        shift = false
    })
    assertTrue(ctrlStrict > 0, "explicit modifier axis binding should activate when required modifier is held")

    local ctrlPlusShift = controls.getActionMouseAxisValue("walk_look_right", 12, 0, {
        alt = false,
        ctrl = true,
        shift = true
    })
    assertTrue(ctrlPlusShift == 0, "explicit modifier axis binding should reject extra modifiers")
    controls.resetToDefaults()

    keyDown.rshift = true
    assertTrue(controls.isActionDown("walk_sprint"), "right shift should activate left-shift sprint binding")
    keyDown.rshift = nil

    local sprintTriggered = controls.actionTriggeredByKey("walk_sprint", "rshift", {
        alt = false,
        ctrl = false,
        shift = true
    })
    assertTrue(sprintTriggered, "right shift keypress should match left-shift binding")

    print("Controls strict modifier tests passed")
end

run()
