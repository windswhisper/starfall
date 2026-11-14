return {
    id = "Test_Mark", trigger = "Active", units = { "MTNK" }, cooldown = 120,
    targeting = { kind = "Unit", houses = { "Self" }, castRange = 12 },
    onCast = function(ctx)
        ctx:action("GiveCredits", { Target = ctx.target, Amount = 15 })
    end,
}
