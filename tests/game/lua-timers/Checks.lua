return {
    start = function(ctx)
        local data = { unit = ctx.self, values = { false, 0, -1, 1.25, "a\0b" }, [9] = "ok" }
        ctx.state.task = ctx:schedule(40, "afterLoad", data)
        data.values[2] = 99
        ctx.state.saved = { n = 15 }
        local localCopy = ctx.state.saved
        localCopy.n = -10
        ctx.state.cancel = ctx:schedule(55, "forbidden", {})
        ctx.state.roll = ctx:randomInt(-100, 100)
    end,
    afterLoad = function(ctx, data)
        assert(ctx.taskId == ctx.state.task and data.unit == ctx.self and ctx.target:alive())
        assert(data.values[1] == false and data.values[2] == 0 and data.values[3] == -1)
        assert(data.values[4] == 1.25 and data.values[5] == "a\0b" and data[9] == "ok")
        assert(ctx.state.saved.n == 15 and ctx.state.roll >= -100 and ctx.state.roll <= 100)
        assert(ctx:cancel(ctx.state.cancel) and not ctx:cancel(ctx.state.cancel))
        ctx.state.done = true
        ctx.state.cancel = nil
    end,
    forbidden = function(ctx)
        ctx.state.failed = true
    end,
}
