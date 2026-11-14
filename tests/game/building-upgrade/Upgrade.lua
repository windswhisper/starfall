return {
    onUpgrade = function(ctx)
        assert(ctx.target and ctx.self and ctx.target:id() == ctx.self:id())
        assert(ctx.target:kind() == "building")
        assert(ctx:action("ChangeMaxHealth", { Target = ctx.target, Amount = 50 }))
    end
}
