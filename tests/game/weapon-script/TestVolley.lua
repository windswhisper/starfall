return {
    onDetonate = function(ctx)
        if ctx.damage ~= 17 then return end
        assert(ctx.warhead == "SA" and ctx.self == nil and ctx.target == nil and ctx.superWeapon == nil)
        assert(ctx.point and ctx.owner ~= nil and ctx.castId > 0)
        assert(ctx:action("DetonateWarhead", {Target=ctx.point, Warhead=ctx.warhead, Amount=1}))
        assert(ctx:action("GiveResearchPoints", {Amount=ctx.params.bonus}))
        assert(ctx:action("FireWeapon", {Weapon="PrismShot", Target=ctx.point,
            Origin={x=ctx.point.x+0.25, y=ctx.point.y, height=3}}))
    end,
    onFire = function(ctx)
        assert(ctx.self:alive())
        assert(ctx.point.x == ctx.event.targetPoint.x and ctx.point.y == ctx.event.targetPoint.y)
        ctx.state.count = (ctx.state.count or 0) + 1
        if ctx.state.count == 1 then
            local p = ctx.point
            assert(ctx:action("FireWeapon", {Weapon="105mm", Target=ctx.target,
                Origin={x=p.x+0.25, y=p.y-0.25, height=4.5}}))
            assert(ctx:runAction("OriginProbeShot", {Target=ctx.target, Origin=ctx.self}))
            assert(ctx:actions({{Action="FireWeapon", Weapon="105mm", Target=ctx.self,
                Origin={x=p.x, y=p.y, height=2}}}))
        end
        ctx.state.target = ctx.target and ctx.target:id() or 0
        ctx.state.x = ctx.point.x
        ctx.state.y = ctx.point.y
        ctx:schedule(6, "after", {x = ctx.point.x, y = ctx.point.y})
    end,
    after = function(ctx, data)
        ctx.state.finished = (ctx.state.finished or 0) + 1
        ctx.state.lastX = data.x
        ctx.state.lastY = data.y
        if ctx.state.finished == 1 then
            assert(ctx:action("FireWeapon", {Weapon="105mm", Target=data}))
            ctx.state.followUp = 1
        end
    end
}
