local function onCast(ctx)
    local unit = ctx.self
    assert(unit:currentOrder() == nil)
    assert(unit:startOrder("NavalBombardment", ctx.area))
    assert(unit:currentOrder() == "NavalBombardment")
    assert(not unit:startOrder("Missing", ctx.area))
    assert(not unit:cancelOrder("Missing"))
    assert(unit:cancelOrder("NavalBombardment"))
    assert(unit:currentOrder() == nil)
end

return {onCast = onCast}
