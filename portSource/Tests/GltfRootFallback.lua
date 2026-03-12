local gltfImporter = require("Source.ModelModule.Importers.GltfImporter")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function base64Encode(data)
    local alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    local out = {}
    local len = #data
    local i = 1
    while i <= len do
        local a = data:byte(i) or 0
        local b = data:byte(i + 1) or 0
        local c = data:byte(i + 2) or 0
        local triple = a * 65536 + b * 256 + c

        local s1 = math.floor(triple / 262144) % 64
        local s2 = math.floor(triple / 4096) % 64
        local s3 = math.floor(triple / 64) % 64
        local s4 = triple % 64

        out[#out + 1] = alphabet:sub(s1 + 1, s1 + 1)
        out[#out + 1] = alphabet:sub(s2 + 1, s2 + 1)
        if (i + 1) <= len then
            out[#out + 1] = alphabet:sub(s3 + 1, s3 + 1)
        else
            out[#out + 1] = "="
        end
        if (i + 2) <= len then
            out[#out + 1] = alphabet:sub(s4 + 1, s4 + 1)
        else
            out[#out + 1] = "="
        end

        i = i + 3
    end
    return table.concat(out)
end

local function buildMinimalHierarchyNoSceneGltf()
    local positions = string.char(
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,

        0, 0, 128, 63,
        0, 0, 0, 0,
        0, 0, 0, 0,

        0, 0, 0, 0,
        0, 0, 128, 63,
        0, 0, 0, 0
    )
    local b64 = base64Encode(positions)

    return table.concat({
        "{",
        '"asset":{"version":"2.0"},',
        '"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,' .. b64 .. '"}],',
        '"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],',
        '"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],',
        '"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],',
        '"nodes":[{"children":[1]},{"mesh":0}]',
        "}"
    })
end

local function run()
    local gltf = buildMinimalHierarchyNoSceneGltf()
    local parsed, err = gltfImporter.loadFromBytes(gltf, "inline.gltf", { baseDir = "." })
    assertTrue(parsed ~= nil, "glTF parse failed: " .. tostring(err))
    local faceCount = #(parsed.model and parsed.model.faces or {})
    assertTrue(faceCount == 1, "root fallback traversal should avoid duplicate mesh emission")
    print("glTF root fallback tests passed")
end

run()
