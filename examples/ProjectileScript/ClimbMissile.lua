return {
    onLaunch = function(p)
        p:setVelocity(0, 0, 0.20)
        return 12
    end,
    onTick = function(p)
        p:steerTo(p:targetPoint(), p:weaponSpeed() / 256, 8)
    end,
}
