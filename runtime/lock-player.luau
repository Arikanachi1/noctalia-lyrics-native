-- The same live C++ track channel as the main lyrics widget, on the lock surface.
local Motion = require("./lib/motion.luau")
local data = noctalia.state.get("track") or {}
local motion = Motion.new(data)
local function clock(seconds)
    seconds = math.max(0, math.floor(seconds or 0))
    return string.format("%d:%02d", math.floor(seconds / 60), seconds % 60)
end
local function control(action, value)
    local player = string.gsub(data.player or "", "^org%.mpris%.MediaPlayer2%.", "")
    if player == "" or data.musicEligible ~= true then return end
    local args = {"playerctl", "--player", player, action}
    if value then table.insert(args, value) end
    noctalia.runAsync(args)
end
local function render()
    local shown, alpha = Motion.frame(motion)
    local available = data.musicEligible == true and (data.title or "") ~= ""
    local title = available and data.title or "YouTube Music"
    local artist = available and (data.artist or "") or "Включи музыку в своём плеере"
    local line = shown.line or ""
    if line == "♪" then line = "Инструментальная пауза" end
    if line == "" then
        line = available and (shown.state == "loading" and "Ищу слова…" or "Текст пока недоступен")
            or "Здесь появятся слова текущей песни"
    end
    local function button(key, glyph, action, value, primary)
        return ui.button({key=key, glyph=glyph, width=primary and 52 or 44, height=44,
            variant=primary and "primary" or "ghost", enabled=available,
            onClick=function() control(action,value) end})
    end
    local position, duration = data.position or 0, data.duration or 0
    desktopWidget.render(ui.row({key="lock-player",width=960,height=224,padding=20,gap=20,
        align="center",radius=24,fill="surface/0.94",border="on_surface/0.10",borderWidth=1},{
        ui.column({key="track",width=390,height=184,gap=6,align="stretch"},{
            ui.label({key="title",text=title,fontSize=28,fontWeight="bold",height=64,maxWidth=390,maxLines=2}),
            ui.label({key="artist",text=artist,fontSize=21,height=27,maxWidth=390,maxLines=1,color="on_surface_variant"}),
            ui.row({key="controls",gap=8,height=44,align="center"},{
                button("rewind","rewind-backward-10","position","10-"),
                button("previous","player-skip-back","previous"),
                button("play",data.playing and "player-pause" or "player-play","play-pause",nil,true),
                button("next","player-skip-forward","next"),
                button("forward","rewind-forward-10","position","10+"),
                ui.spacer({flexGrow=1}),
                ui.label({text=clock(position).." / "..clock(duration),fontSize=12,color="on_surface_variant",maxLines=1}),
            }),
            ui.progress({key="progress",width=390,height=3,progress=duration>0 and math.min(1,position/duration) or 0,
                fill="primary",track="on_surface/0.10"}),
        }),
        ui.separator({key="divider",orientation="vertical",height=150,width=1,color="on_surface/0.12"}),
        ui.column({key="lyrics",width=489,height=184,gap=14,justify="center",align="stretch"},{
            ui.row({gap=8,align="center"},{ui.glyph({name="music",size=15,color="primary"}),
                ui.label({text="СЛОВА ПЕСНИ",fontSize=11,fontWeight="medium",color="on_surface_variant"})}),
            ui.label({key="current-line",text=line,fontSize=25,fontWeight="medium",maxWidth=489,maxLines=3,
                height=108,color=available and "primary" or "on_surface_variant",opacity=alpha,textAlign="start"}),
            ui.label({text=available and (data.playing and "Сейчас играет" or "На паузе") or "",
                fontSize=12,color="on_surface_variant",height=16,maxLines=1}),
        }),
    }))
end
noctalia.state.watch("track",function(value)
    data=value or {};Motion.push(motion,data);desktopWidget.setNeedsFrameTick(motion.active);render()
end)
function onFrameTick(deltaMs)
    Motion.tick(motion,deltaMs);render()
    if not motion.active then desktopWidget.setNeedsFrameTick(false) end
end
function update()render()end
noctalia.setUpdateInterval(1000)
render()
