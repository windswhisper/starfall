return {
    show = function(ctx)
        local p = ctx.self:position()
        p.x = p.x + 1
        assert(ctx:actions({
            {Action="PlayAnim", Anim="EXPLOSML", Target=p},
            {Action="PlaySound", Sound="GrizzlyTankAttack", Target=p},
            {Action="SpawnParticleSystem", ParticleSystem="SmallGreySSys", Target=p},
            {Action="ShakeScreen", ShakeX=2, ShakeY=3, Target=p},
        }))
    end,
}
