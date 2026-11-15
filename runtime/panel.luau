local data = noctalia.state.get("track") or {}
local Control = require("./lib/control.luau")
local lastPresentation=nil
local showAll = false
local opened = false
local function render()
    if not opened then return end
    -- Let the native panel animate its opening/closing surface. Rebuilding a
    -- long text tree on every animation frame made glyph layout visibly stutter.
    local display=data
    local emptyMessage=(#(data.lines or {})==0 and (data.plain or "")=="") and (data.message or "") or ""
    local parts={data.title or "",data.artist or "",data.player or "",emptyMessage,
        data.plain or "",tostring(data.index),tostring(data.playing),tostring(data.timingAvailable),
        tostring(data.offset),tostring(data.duration),tostring(showAll),tostring(math.floor(data.position or 0))}
    for _,line in ipairs(data.lines or {})do table.insert(parts,line.text or "")end
    local signature=table.concat(parts,"\0")
    if signature==lastPresentation then return end
    lastPresentation=signature
    local rows = {}
    local lines = display.lines or {}
    if #lines > 0 then
        local current = (display.index or -1) + 1
        local first = showAll and 1 or math.max(1, current - 3)
        local last = showAll and #lines or math.min(#lines, math.max(7, current + 4))
        for i = first, last do
            local text = lines[i].text
            table.insert(rows, ui.label({
                key="line-"..i,text=text ~= "" and text or "♪",
                color=i == current and "primary" or "on_surface_variant",
                fontWeight="medium",
                fontSize=18,
                maxWidth=570, maxLines=3, textAlign="center",
            }))
        end
    elseif display.plain and display.plain ~= "" then
        table.insert(rows, ui.label({text=display.plain, maxWidth=570, maxLines=200, fontSize=17}))
    else
        table.insert(rows, ui.label({text=display.message or "Открой YouTube Music и включи музыку", maxWidth=570, maxLines=4, color="on_surface_variant"}))
    end
    local position = data.position or 0
    local duration = data.duration or 0
    local restore=Control.needsClock(data)
    local offsets=data.timingAvailable==true and data.trackId~=nil
    panel.render(ui.column({key="lyrics-panel",width=640,height=540,gap=12,padding=16,align="stretch"}, {
        ui.row({gap=10,height=64,align="center"}, {
            ui.glyph({name="music", size=25, color="primary"}),
            ui.column({gap=3,flexGrow=1}, {
                ui.label({text=data.title and data.title ~= "" and data.title or "Текст песни", fontSize=20, fontWeight="bold", maxWidth=400, maxLines=2}),
                ui.label({text=data.artist or "", color="on_surface_variant", maxWidth=400, maxLines=1}),
            }),
            ui.button({key="open-search",text="Поиск текста",glyph="search",width=144,height=36,onClick="openSearch"}),
        }),
        ui.progress({progress=duration > 0 and math.min(1,position/duration) or 0, height=3}),
        ui.scroll({key="lyrics-scroll",flexGrow=1,align="stretch"}, {
            ui.column({key="lyrics-body",gap=0,align="stretch"}, {
                ui.spacer({height=12, flexGrow=0}),
                ui.column({gap=12, align="center"}, rows),
                ui.spacer({height=12, flexGrow=0}),
            }),
        }),
        ui.column({key="lyrics-footer",gap=8,height=(restore or offsets) and 80 or 36,align="stretch"},{
            ui.row({key="lyrics-transport",gap=8,height=36,align="center",justify="center"}, {
                ui.button({glyph="player-skip-back",width=40,height=36,onClick="previous"}),
                ui.button({glyph=data.playing and "player-pause" or "player-play",width=40,height=36,onClick="playPause"}),
                ui.button({glyph="player-skip-forward",width=40,height=36,onClick="next"}),
                ui.button({key="lyrics-mode",text=showAll and "Следить за песней" or "Весь текст",width=168,height=36,onClick="toggleAll"}),
            }),
            ui.row({key="restore-row",height=36,justify="center",visible=restore},{
                ui.button({key="restore-clock",text="Восстановить время",glyph="refresh",width=204,height=36,controlSize="sm",
                    onClick=function() Control.restoreClock(data) end}),
            }),
            ui.row({key="offset-row",gap=7,height=36,justify="center",visible=offsets},{
                ui.button({text="Раньше",width=96,height=36,controlSize="sm",onClick="earlier"}),
                ui.button({text=string.format("%+.2f с",data.offset or 0),width=96,height=36,controlSize="sm",onClick="resetOffset"}),
                ui.button({text="Позже",width=96,height=36,controlSize="sm",onClick="later"}),
            }),
        }),
    }))
end
noctalia.state.watch("track", function(value)
    data=value or {}
    if opened then
        render()
    end
end)
function onOpen()
    opened=true;lastPresentation=nil
    data=noctalia.state.get("track") or {}
    panel.setNeedsFrameTick(false)
    render()
end
function onClose() opened=false; panel.setNeedsFrameTick(false) end
function onFrameTick(deltaMs)
    -- Native surface animation owns frame ticks; text layout is retained.
    panel.setNeedsFrameTick(false)
end
function update() render() end
local function control(action)
    local player = string.gsub(data.player or "", "^org%.mpris%.MediaPlayer2%.", "")
    if player ~= "" then noctalia.runAsync({"playerctl", "--player", player, action}) end
end
function previous() control("previous") end
function playPause() control("play-pause") end
function next() control("next") end
function openSearch()noctalia.togglePanel("ari/lyrics:search")end
local function adjust(value)
    if data.trackId then noctalia.runAsync({noctalia.pluginDir().."/bin/ari-lyrics","--adjust-offset",data.trackId,tostring(value)}) end
end
function earlier()adjust(-0.25)end
function later()adjust(0.25)end
function resetOffset()adjust(-(data.offset or 0))end
function toggleAll()
    showAll=not showAll
    panel.setNeedsFrameTick(false)
    render()
end
noctalia.setUpdateInterval(1000)
