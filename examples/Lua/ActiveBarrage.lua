local skill = {}

function skill.strike(ctx, data)
    ctx:action("DetonateWarhead", {
        Target = ctx:randomPoint(data.area), Warhead = ctx.params.warhead, Amount = ctx.params.damage,
    })
    if data.left > 1 then
        data.left = data.left - 1
        ctx:schedule(ctx.params.delay, "strike", data)
    end
end

function skill.onCast(ctx)
    skill.strike(ctx, { area = ctx.area, left = ctx.params.shots })
end

return skill
