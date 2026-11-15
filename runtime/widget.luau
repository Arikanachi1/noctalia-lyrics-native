local Island = require("./lib/island.luau")
local clockWidth,musicWidth=72,380
local data = noctalia.state.get("track") or {}
local model = Island.new(data, noctalia.nowMs())
local animation = Island.animation(clockWidth)
local lastTick = noctalia.nowMs()
local mode = "clock"
local lastPresentation=nil
local lastInterval=nil
local function render()
    local now = noctalia.nowMs()
    -- Keep the clock compact and readable throughout the music transition.
    local clock = noctalia.formatTime("{:%H:%M}")
    local content = Island.content(model, now, '')
    mode = content.mode
    Island.target(animation, content, mode == "music" and musicWidth or clockWidth)
    local width, alpha, shown = Island.frame(animation)
    local spread=math.max(0,math.min(1,(width-clockWidth)/(musicWidth-clockWidth)))
    local signature=string.format("%.2f|%.3f|%s|%s|%s",width,alpha,shown.mode,shown.text,clock)
    if signature~=lastPresentation then
        lastPresentation=signature
        -- Background, padding and hover are owned by a single native capsule.
        -- Its twelve-pixel padding on both sides is included in target width.
        local children={}
        if width>clockWidth+.05 then
            -- A column constrains label width to the remaining horizontal space.
            table.insert(children,ui.column({key="music-slot",flexGrow=1,height=24,align="center",justify="center",opacity=alpha},{
                ui.label({key="island-content",text=shown.mode=="music" and shown.text or "",
                    maxWidth=math.max(1,width-108),maxLines=1,textAlign="center",fontSize=13,
                    fontWeight="medium",color="primary"}),
            }))
        end
        -- Keep the same shaped HH:MM label in both states. The growing music
        -- slot moves it to the trailing edge without separating hours/minutes.
        table.insert(children,ui.row({key="clock-pill",height=22,paddingH=6*spread,gap=0,
            radius=7,fill=string.format("on_surface/%.3f",.055*spread),
            opacity=1-.2*spread,align="center",justify="center"},{
            ui.label({key="clock-full",text=clock,fontSize=13,fontWeight="semibold",color="on_surface"}),
        }))
        barWidget.render(ui.row({key="island",width=math.max(1,width-24),height=24,gap=8*spread,
            align="center",justify="center"},children))
    end
    local interval=animation.active and 16 or 1000
    if interval~=lastInterval then lastInterval=interval;noctalia.setUpdateInterval(interval) end
end
noctalia.state.watch("track", function(value)
    local now=noctalia.nowMs()
    Island.tick(animation,now-lastTick);lastTick=now
    data = value or {}
    Island.observe(model, data, now)
    render()
end)
function update()
    local now=noctalia.nowMs()
    Island.tick(animation, now-lastTick); lastTick=now
    render()
end
function onClick()
    if mode=="music" then noctalia.togglePanel("ari/lyrics:player")
    else noctalia.runAsync({"noctalia","msg","panel-toggle","control-center"}) end
end
function onHover(entered)
    -- Time stays in the capsule itself, including while the pointer is over it.
    render()
end
function onRightClick()
    local player=string.gsub(data.player or "", "^org%.mpris%.MediaPlayer2%.", "")
    if player~="" then noctalia.runAsync({"playerctl","--player",player,"play-pause"}) end
end
function onScroll(axis,steps)
    if axis=="vertical" then
        noctalia.runAsync({"noctalia","msg",steps<0 and "volume-up" or "volume-down","5"})
    end
end
barWidget.clearTooltip()
render()
