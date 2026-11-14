return {
    onDeploy = function(ctx)
        local allies = ctx:nearbyUnits(ctx.self, ctx.params.radius, {
            houses = { "Self", "Allies" },
            types = { "@Infantry", "@Vehicle", "@Aircraft" },
            includeSelf = false,
        })
        for _, unit in ipairs(allies) do
            if unit:trainable() and unit:rank() < 2 then
                ctx:action("Promote", { Target = unit, Levels = ctx.params.levels })
            end
        end
    end,
}
