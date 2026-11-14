return {
    cast = function(ctx)
        assert(ctx:action("ForceMove", {
            Target = ctx.self, Offset = { x = 0.25, y = 0 }, Duration = 12, IgnoreCollision = false, Tilt = false, Easing = "EaseOut",
        }))
        assert(not ctx:action("ForceMove", {
            Target = ctx.target, Mode = "Launch", Duration = 4, BlowWeight = 4.25, AffectsIronCurtain = true,
        }))
        assert(not ctx:action("ForceMove", {
            Target = ctx.target, Mode = "Launch", Duration = 4, BlowWeight = 5, AffectsIronCurtain = false,
        }))
        assert(ctx:action("ForceMove", {
            BlowWeight = 4.5, AffectsIronCurtain = true,
            Target = ctx.target, Offset = { x = 3, y = -1.5 }, Height = 2, Duration = 12,
        }))
    end,
}
