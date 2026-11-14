local skill = {}

function skill.pulse(ctx, left)
    local units = ctx:nearbyUnits(ctx.point, ctx.params.radius, {
        houses = "Enemies", types = { "@Vehicle", "@Infantry" },
    })
    for _, unit in ipairs(units) do
        local p = unit:position()
        if p then
            ctx:action("ForceMove", {
                Target = unit, Offset = {x = (ctx.point.x-p.x)*0.4, y = (ctx.point.y-p.y)*0.4},
                Height = 0.4, Duration = ctx.params.interval, IgnoreCollision = false,
                BlowWeight = ctx.params.blowweight, AffectsIronCurtain = ctx.params.affectsironcurtain,
            })
            ctx:action("Damage", {Target = unit, Amount = ctx.params.damage, Warhead = ctx.params.warhead})
        end
    end
    if left > 1 then ctx:schedule(ctx.params.interval, "pulse", left-1) end
end

function skill.onLaunch(ctx)
    skill.pulse(ctx, ctx.params.pulses)
end

return skill
