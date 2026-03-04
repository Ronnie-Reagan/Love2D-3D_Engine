-- Quaternion utilities
-- Reference: https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles

local q = {}

-- Identity quaternion (no rotation)
function q.identity()
    return { w = 1, x = 0, y = 0, z = 0 }
end

-- Create quaternion from axis-angle, normalizing the axis
function q.fromAxisAngle(axis, angle)
    local x, y, z = axis[1], axis[2], axis[3]
    local len = math.sqrt(x*x + y*y + z*z)
    if len == 0 then error("q.fromAxisAngle: axis vector cannot be zero") end

    -- Normalize axis
    x, y, z = x / len, y / len, z / len

    local half = angle * 0.5
    local s = math.sin(half)
    return {
        w = math.cos(half),
        x = x * s,
        y = y * s,
        z = z * s
    }
end

-- Quaternion multiplication: q1 * q2
function q.multiply(a, b)
    return {
        w = a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
        x = a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        y = a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        z = a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
    }
end

-- Normalize a quaternion
function q.normalize(quat)
    local len = math.sqrt(quat.w^2 + quat.x^2 + quat.y^2 + quat.z^2)
    if len == 0 then return q.identity() end
    return {
        w = quat.w / len,
        x = quat.x / len,
        y = quat.y / len,
        z = quat.z / len
    }
end

-- Conjugate of a quaternion
function q.conjugate(quat)
    return {
        w = quat.w,
        x = -quat.x,
        y = -quat.y,
        z = -quat.z
    }
end

-- Rotate a vector by a quaternion
function q.rotateVector(quat, v)
    -- v' = q * v * q^-1, with v = (0, v)
    local qv = { w = 0, x = v[1], y = v[2], z = v[3] }
    local qConj = q.conjugate(quat)
    local res = q.multiply(q.multiply(quat, qv), qConj)
    return { res.x, res.y, res.z }
end

local function normalizeBasisVector(v)
    local x = tonumber(v and v[1]) or 0
    local y = tonumber(v and v[2]) or 0
    local z = tonumber(v and v[3]) or 0
    local len = math.sqrt(x * x + y * y + z * z)
    if len <= 1e-12 then
        return { 0, 0, 0 }
    end
    return { x / len, y / len, z / len }
end

function q.getBasis(rot)
    local basisRot = rot or q.identity()
    return normalizeBasisVector(q.rotateVector(basisRot, { 0, 0, 1 })), -- forward (+Z)
           normalizeBasisVector(q.rotateVector(basisRot, { 1, 0, 0 })), -- right
           normalizeBasisVector(q.rotateVector(basisRot, { 0, 1, 0 }))  -- up
end

return q
