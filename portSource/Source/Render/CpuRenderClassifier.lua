local classifier = {}

function classifier.isMaterialTransparent(material)
	if type(material) ~= "table" then
		return false
	end
	if material.alphaMode == "BLEND" then
		return true
	end
	local baseFactor = material.baseColorFactor
	if type(baseFactor) == "table" and tonumber(baseFactor[4]) and tonumber(baseFactor[4]) < 0.999 then
		return true
	end
	local diffuseFactor = material.diffuseFactor
	if type(diffuseFactor) == "table" and tonumber(diffuseFactor[4]) and tonumber(diffuseFactor[4]) < 0.999 then
		return true
	end
	return false
end

function classifier.isObjectTransparent(obj)
	if type(obj) ~= "table" then
		return false
	end
	local objectAlpha = tonumber(obj.color and obj.color[4]) or 1
	if objectAlpha < 0.999 then
		return true
	end
	if type(obj.materials) == "table" then
		for _, material in ipairs(obj.materials) do
			if classifier.isMaterialTransparent(material) then
				return true
			end
		end
	end
	if obj.model and type(obj.model.faceColors) == "table" then
		for _, color in ipairs(obj.model.faceColors) do
			if type(color) == "table" and (tonumber(color[4]) or 1) < 0.999 then
				return true
			end
		end
	end
	return false
end

function classifier.shouldCullBackfaces(obj)
	if type(obj) == "table" and obj.cullBackfaces == false then
		return false
	end
	if type(obj) == "table" and type(obj.materials) == "table" then
		for _, material in ipairs(obj.materials) do
			if type(material) == "table" and material.doubleSided then
				return false
			end
		end
	end
	return true
end

return classifier
