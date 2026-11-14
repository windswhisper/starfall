local skill = {}

function skill.strike(ctx, data)
    local point = ctx:randomPoint(data.area)
    ctx:action("DetonateWarhead", { Target = point, Warhead = ctx.params.warhead, Amount = ctx.params.damage })
    if data.shot < ctx.params.shots then
        data.shot = data.shot + 1
        ctx:schedule(ctx.params.delay, "strike", data)
    end
end

function skill.onFire(ctx)
    local center = ctx.event.targetPoint or (ctx.target and ctx.target:position())
    if center then
        skill.strike(ctx, { area = { center = center, radius = ctx.params.radius }, shot = 1 })
    end
end

return skill
