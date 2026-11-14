return {
    id = "Test_Pulse", trigger = "Active", units = { "MTNK" }, cooldown = 120,
    onCast = function(ctx)
        ctx.state.pulses = (ctx.state.pulses or 0) + 1
    end,
}
