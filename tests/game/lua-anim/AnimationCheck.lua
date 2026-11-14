local skill = {}
function skill.start(ctx)
    local p = ctx.self:position()
    if not p then return end
    local anim = ctx:spawnAnim("PIFF", p, {loop=true, duration=100, height=2})
    if not anim then return end
    ctx.state.anim = anim
    ctx.state.started = ctx:moveAnim(anim, {x=p.x+4,y=p.y+2}, 8)
    ctx.state.invalid = ctx:moveAnim(anim+1, {x=p.x+1,y=p.y}, 8)
    ctx.state.nilmove = ctx:moveAnim(nil, p, 1)
    ctx.state.nilremove = ctx:removeAnim(nil)
    ctx.state.unknown = ctx:spawnAnim("NO_SUCH_ANIMATION", p, {duration=1}) == nil
    ctx.state.missing = ctx:spawnAnim("SF_AnimMissing", p, {duration=1}) == nil
    local short = ctx:spawnAnim("PIFF", p, {loop=true, duration=2})
    ctx.state.short = short
    ctx:schedule(12, "finish", {anim=anim})
end
function skill.finish(ctx, data)
    ctx.state.removed = ctx:removeAnim(data.anim)
    ctx.state.removedAgain = ctx:removeAnim(data.anim)
end
return skill
