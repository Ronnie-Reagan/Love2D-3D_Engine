local vector3 = {}

function vector3.cross(a, b)
    return {
        a[2] * b[3] - a[3] * b[2],
        a[3] * b[1] - a[1] * b[3],
        a[1] * b[2] - a[2] * b[1]
    }
end

function vector3.sub(a, b)
    return {
        a[1] - b[1],
        a[2] - b[2],
        a[3] - b[3]
    }
end

function vector3.add(a, b)
    return {
        a[1] + b[1],
        a[2] + b[2],
        a[3] + b[3]
    }
end

function vector3.normalizeVec(v)
    local len = math.sqrt(v[1]^2 + v[2]^2 + v[3]^2)
    return len == 0 and {0, 0, 0} or {v[1] / len, v[2] / len, v[3] / len}
end

function vector3.dot(a, b)
    return a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
end

function vector3.length(v)
    return math.sqrt(v[1]^2 + v[2]^2 + v[3]^2)
end

return vector3
