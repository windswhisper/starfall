return {
    cast = function(ctx)
        return ctx:action("JumpTo", {Target=ctx.self, Destination=ctx.point, Range=6, Height=3, Duration=15,
            TakeoffAnim="SMKPUFF", TakeoffSound="HornetTakeoff", LandAnim="DROPLAND", LandSound="HornetLanding"})
    end,
}
