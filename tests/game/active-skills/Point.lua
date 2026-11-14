return {
    id = "Test_Point", trigger = "Active", units = { "MTNK" }, cooldown = 120,
    targeting = { kind = "Point", castRange = 4 },
    onCast = function(ctx)
        ctx.state.point = ctx.point
        ctx.state.value = ctx.params.value
    end,
}
