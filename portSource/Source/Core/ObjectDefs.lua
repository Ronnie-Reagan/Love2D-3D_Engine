local obj = {}
-- defualt testing cube
obj.cubeModel = {
    vertices = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
    faces = { { 4, 3, 2, 1 }, { 5, 6, 7, 8 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 4, 8, 7 }, { 4, 1, 5, 8 } }
}

-- Octahedron puff used for volumetric cloud clusters.
obj.cloudPuffModel = {
    vertices = {
        { 0, 1, 0 },
        { 1, 0, 0 },
        { 0, 0, 1 },
        { -1, 0, 0 },
        { 0, 0, -1 },
        { 0, -1, 0 }
    },
    faces = {
        { 1, 2, 3 },
        { 1, 3, 4 },
        { 1, 4, 5 },
        { 1, 5, 2 },
        { 6, 3, 2 },
        { 6, 4, 3 },
        { 6, 5, 4 },
        { 6, 2, 5 }
    }
}


return obj
