local rewards = {
    {"SFRT_Credits"},
    {"SFRT_Research"},
    {"SFRT_SilentCredits", "SFRT_SilentResearch"},
}

return {
    start = function(ctx)
        -- Allow the opening ownership transfer to finish before selecting the tank.
        ctx:schedule(45, "begin", ctx.self)
    end,

    begin = function(ctx, tank)
        if not tank or not tank:alive() or tank:type() ~= "MTNK" then
            local cell = {x = ctx.params.x, y = ctx.params.y}
            tank = ctx:nearbyUnits(cell, 1, {houses = "Self", types = "MTNK"})[1]
        end
        if tank then
            ctx:schedule(1, "pay", {tank = tank, phase = 1})
        end
    end,

    pay = function(ctx, data)
        if not data.tank:alive() then return end
        for _, section in ipairs(rewards[data.phase]) do
            ctx:runAction(section, {Target = data.tank})
        end
        ctx:schedule(75, "pay", {tank = data.tank, phase = data.phase % #rewards + 1})
    end,
}
