local skill = {}
local tau = 2 * math.pi

local function centerAt(data, step)
    -- Older saved casts have a fixed center and no endpoint.
    if not data.finish then return data.center end
    local t = step / data.steps
    return {
        x = data.center.x + (data.finish.x - data.center.x) * t,
        y = data.center.y + (data.finish.y - data.center.y) * t,
    }
end

local function orbit(data, victim, step)
    local t = (step - (victim.joined or 0)) / data.steps
    local r = victim.radius + (data.core - victim.radius) * t
    local a = victim.angle + tau * data.turns * t
    local center = centerAt(data, step)
    return center.x + r * math.cos(a), center.y + r * math.sin(a)
end

-- nearbyUnits takes a unit as its origin. Query a covering circle, then
-- filter against the current fractional position of the travelling eye.
local function inArea(ctx, center, radius, types, nextCenter)
    local caster = ctx.self:position()
    if not caster then return {} end
    local sx, sy = caster.x - center.x, caster.y - center.y
    local vx = nextCenter and nextCenter.x - center.x or 0
    local vy = nextCenter and nextCenter.y - center.y or 0
    local span = vx*vx + vy*vy
    local scan = math.min(128, math.ceil(math.sqrt(sx*sx + sy*sy) + math.sqrt(span)) + radius + 2)
    local found = {}
    for _, unit in ipairs(ctx:nearbyUnits(ctx.self, scan, {
        houses = "All", types = types, includeSelf = true,
    })) do
        local pos = unit:position()
        if pos then
            local dx, dy = pos.x - center.x, pos.y - center.y
            if span > 0 then
                local t = math.min(1, math.max(0, (dx*vx + dy*vy) / span))
                dx, dy = dx - t*vx, dy - t*vy
            end
            if dx*dx + dy*dy <= radius*radius then found[#found + 1] = unit end
        end
    end
    return found
end

local function collect(ctx, data, center)
    local held, candidates = {}, {}
    local survivors = {}
    for _, victim in ipairs(data.victims) do
        if victim.unit:position() then
            held[victim.id] = true
            survivors[#survivors + 1] = victim
        end
    end
    data.victims = survivors
    if #survivors >= data.limit then return end
    for _, unit in ipairs(inArea(ctx, center, data.radius, { "@Infantry", "@Vehicle" }, centerAt(data, data.step + 1))) do
        local id = unit:id()
        if not held[id] then
            local pos = unit:position()
            local dx, dy = pos.x - center.x, pos.y - center.y
            local r2 = dx*dx + dy*dy
            candidates[#candidates + 1] = {
                unit = unit, id = id, joined = data.step, radius = math.sqrt(r2),
                angle = r2 == 0 and tau * (id % 16) / 16 or math.atan(dy, dx),
            }
        end
    end
    table.sort(candidates, function(a, b)
        if a.radius == b.radius then return a.id < b.id end
        return a.radius < b.radius
    end)
    -- Only units that accept the move occupy a slot (see spin).
    for _, candidate in ipairs(candidates) do
        survivors[#survivors + 1] = candidate
    end
end

function skill.spin(ctx, data)
    local step = data.step
    local center = centerAt(data, step)
    if data.anim then
        ctx:moveAnim(data.anim, centerAt(data, step + 1), data.interval)
    end
    if data.finish then collect(ctx, data, center) end
    local pulse = step % (data.segmentsPerPulse or 2) == 0
    if pulse then
        local buildings = inArea(ctx, center, data.radius or 4, { "@Building" })
        for _, building in ipairs(buildings) do
            ctx:action("Damage", { Target = building, Amount = data.damage })
        end
    end
    local remaining = {}
    for _, victim in ipairs(data.victims) do
        if data.limit and #remaining >= data.limit then break end
        local unit = victim.unit
        local pos = unit:position()
        if pos then
            local prevX, prevY = orbit(data, victim, step)
            local nextX, nextY = orbit(data, victim, step + 1)
            -- position() reports whole cells. Keep fractional motion on open terrain
            -- and correct whole-cell drift if the last chord hit an obstacle.
            local dx = nextX - prevX + math.floor(prevX + 0.5) - pos.x
            local dy = nextY - prevY + math.floor(prevY + 0.5) - pos.y
            local k = data.horizon
            local age = step - (victim.joined or 0)
            local oldLift = data.peak * age / data.steps
            local newLift = data.peak * (age + 1) / data.steps
            -- Replace each arc after 1/k of its duration, before it falls.
            -- ForceMove inherits the current lift from the preceding arc.
            local arc = (newLift - oldLift * (1 - 1/k)) / (4/k * (1 - 1/k))
            if ctx:action("ForceMove", {
                Target = unit, Offset = { x = dx*k, y = dy*k },
                Height = arc, Duration = data.interval*k, Tilt = true,
                BlowWeight = ctx.params.blowweight, AffectsIronCurtain = ctx.params.affectsironcurtain,
            }) then
                if pulse then
                    ctx:action("Damage", { Target = unit, Amount = data.damage })
                end
                if unit:alive() then remaining[#remaining + 1] = victim end
            end
        end
    end
    data.victims = remaining
    data.step = step + 1
    local callback = data.step == data.steps and "land" or "spin"
    if callback == "spin" or #remaining > 0 or data.anim then
        ctx:schedule(data.interval, callback, data)
    end
end

function skill.land(ctx, data)
    if data.anim then ctx:schedule(data.landing, "finishVisual", { anim = data.anim }) end
    for _, victim in ipairs(data.victims) do
        if victim.unit:position() then
            -- Inherit the peak height and descend in place. One lepton is the
            -- smallest positive Height accepted for a purely vertical motion.
            ctx:action("ForceMove", {
                Target = victim.unit, Offset = { x = 0, y = 0 },
                Height = 1/256, Duration = data.landing, Tilt = true,
                BlowWeight = ctx.params.blowweight, AffectsIronCurtain = ctx.params.affectsironcurtain,
            })
        end
    end
end

function skill.finishVisual(ctx, data)
    if data.anim then ctx:removeAnim(data.anim) end
end

function skill.onCast(ctx)
    local p, start, finish = ctx.params, ctx.self:position(), ctx.point
    if not start or not finish then return end
    local radius = p.radius
    local dx, dy = finish.x - start.x, finish.y - start.y
    local length = math.sqrt(dx*dx + dy*dy)
    if length == 0 then return end
    if p.sound and p.sound ~= "" then
        ctx:action("PlaySound", { Target = ctx.self, Sound = p.sound })
    end
    -- Form just ahead of the launcher. The endpoint is clipped by the engine
    -- at the map edge; the cast keeps that heading even if the launcher moves.
    local lead = math.min(radius + 1, length) / length
    local center = { x = start.x + dx*lead, y = start.y + dy*lead }
    local limit = math.max(1, math.min(24, math.floor(p.maxtargets)))
    local segments = math.max(2, math.min(8, math.floor(p.segmentsperpulse or 2)))
    local steps = segments * math.max(1, math.min(32, math.floor(p.pulses)))
    local data = {
        center = center, finish = finish, radius = radius, victims = {}, limit = limit, step = 0, steps = steps,
        segmentsPerPulse = segments,
        horizon = 1 + math.ceil(math.sqrt(steps)), interval = math.max(1, math.floor(p.stepframes)),
        damage = p.damage, turns = p.turns, peak = p.peakheight,
        core = math.min(radius, p.coreradius), landing = math.max(1, math.floor(p.landframes)),
    }
    if p.anim and p.anim ~= "" then
        data.anim = ctx:spawnAnim(p.anim, center, {
            loop = true, height = p.animheight or 0,
            duration = math.min(54000, data.steps * data.interval + data.landing + 2),
        })
    end
    skill.spin(ctx, data)
end

return skill
