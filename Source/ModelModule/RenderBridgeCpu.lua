local cpuBridge = {}

function cpuBridge.getCapabilityProfile(safeMode)
    if safeMode then
        return "CPU_SAFE"
    end
    return "CPU_FULL"
end

function cpuBridge.decorateObjectForRender(obj, asset, role)
    if not obj or not asset then
        return false, "invalid decorate target"
    end
    obj.model = asset.model
    obj.materials = asset.materials
    obj.images = asset.images
    obj.submeshes = asset.geometry and asset.geometry.submeshes or nil
    obj.faceMaterials = asset.geometry and asset.geometry.faceMaterials or nil
    obj.activeRenderRole = role
    return true
end

return cpuBridge
