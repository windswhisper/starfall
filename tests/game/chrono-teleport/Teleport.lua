return {
    cast = function(ctx)
        local target = ctx.target
        local original = target:position()
        assert(not ctx:action("ChronoTeleport", {
            Target = target, Destination = { x = 50, y = 65 }, SearchRadius = 0,
        }))
        local after = target:position()
        assert(after.x == original.x and after.y == original.y)
        assert(ctx:runAction("WarpResult", { Target = target, Destination = { x = 50, y = 70 } }))
        assert(ctx:action("ChronoTeleport", { Target = ctx.self, Destination = { x = 50, y = 65 } }))
    end,
}
