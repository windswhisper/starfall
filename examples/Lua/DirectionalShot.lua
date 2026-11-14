local skill = {}

function skill.onCast(ctx)
    ctx:action("FireWeapon", { Target = ctx.point, Weapon = ctx.params.weapon })
end

return skill
