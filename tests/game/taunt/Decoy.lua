return {
    apply = function(ctx)
        local decoys = ctx:nearbyUnits(ctx.self, 8, {houses={"Self"}, types={"HTNK"}})
        assert(#decoys == 1)
        assert(ctx:action("GrantPassive", {
            Target=ctx.target, Passive="DecoyLock", Duration=15, TauntTarget=decoys[1]
        }))
    end
}
