local skill = {}
function skill.cast(ctx)
    ctx.state.casts = (ctx.state.casts or 0) + 1
    ctx.state.roll = ctx:randomInt(-1000000, 1000000)
    assert(ctx:action("GiveCredits", {Amount=37}))
    assert(ctx:action("ForceMove", {
        Target=ctx.target, Offset={x=0.25,y=0}, Duration=24, IgnoreCollision=false, Tilt=false,
    }))
    local anim = ctx:spawnAnim("PIFF", ctx.point, {loop=true,duration=35})
    assert(anim and ctx:moveAnim(anim, {x=ctx.point.x+1,y=ctx.point.y}, 20))
    ctx:schedule(5, "pulse", {left=4})
end
function skill.pulse(ctx, data)
    ctx.state.pulses = (ctx.state.pulses or 0) + 1
    ctx.state.roll = ctx:randomInt(-1000000, 1000000)
    assert(ctx:action("Damage", {Target=ctx.target,Amount=3}))
    if data.left > 1 then ctx:schedule(5, "pulse", {left=data.left-1}) end
end
return skill
