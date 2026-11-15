-- Keep the text baseline fixed. Moving fractional layout spacers gets rounded
-- by the native text renderer, producing a visible one-pixel staircase.
-- A cosine fade changes lines softly without continuously relaying out text.
local Motion = {}
function Motion.new(initial)
    return {shown=initial or {}, target=initial or {}, elapsed=1000, active=false}
end
local function identity(data)
    return (data.player or "") .. "\n" .. (data.title or "") .. "\n" .. tostring(data.index or -1)
        .. "\n" .. (data.line or "") .. "\n" .. (data.state or "")
end
function Motion.push(m, data, preserveVisible)
    data = data or {}
    if identity(data) ~= identity(m.target) then
        -- Rapid word-level timestamps must not continually restart fade-out
        -- and leave the displayed line stuck forever. Finish the current
        -- transition while its target follows the latest incoming line.
        if not m.active then
            m.shown = m.target
            m.startAlpha = 1
            m.elapsed = 0
            m.active = true
        elseif preserveVisible and m.elapsed >= 160 then
            -- Preserve a half-visible song until it has faded out completely.
            m.shown, m.startAlpha = Motion.frame(m)
            m.elapsed = 0
        end
        m.target = data
    else
        m.target = data
        if not m.active then m.shown = data end
    end
end
function Motion.tick(m, dt)
    dt = math.max(0, math.min(dt, 1000))
    m.elapsed = m.elapsed + dt
    if m.elapsed >= 480 then m.shown=m.target; m.active=false end
end
function Motion.frame(m)
    local alpha, shown = 1, m.shown
    if m.active then
        if m.elapsed < 160 then
            alpha = (m.startAlpha or 1) * (1 + math.cos(math.pi * m.elapsed / 160)) / 2
        else
            shown = m.target
            local p = math.min(1, (m.elapsed - 160) / 320)
            alpha = (1 - math.cos(math.pi * p)) / 2
        end
    end
    return shown, math.max(0, math.min(1, alpha)), 0
end
return Motion
