local Lyrics=require("./lib/lyric-scroll.luau")
local Fade=require("./lib/motion.luau")
local Spectrum=require("./lib/spectrum-view.luau")
local data=noctalia.state.get("track") or {}
local function trackKey(value)
    return (value.player or "").."\n"..(value.trackId or ((value.title or "").."\n"..(value.artist or "")))
end
local function trackFrame(value)
    local snapshot={};for key,item in pairs(value)do snapshot[key]=item end
    local mode=#(value.lines or {})>0 and "lyrics" or "metadata"
    return {title=trackKey(value).."\n"..tostring(value.lyricsId or 0).."\n"..mode,track=snapshot}
end
local songFade=Fade.new(trackFrame(data))
local presented=songFade.shown.track
local motion=Lyrics.new(presented)
local opened,started=false,false
local bands,available={},false
local leasePath=nil
local dirty=false
local lastTick,lastPaint=noctalia.nowMs(),0
local spectrumKey=""
local function schedule()dirty=true;panel.setNeedsFrameTick(true)end
local lastGranted,wasLeased=0,false
local function clock(seconds)
    seconds=math.max(0,math.floor(seconds or 0))
    return string.format("%d:%02d",math.floor(seconds/60),seconds%60)
end
local function control(action)
    local player=string.gsub(data.player or "","^org%.mpris%.MediaPlayer2%.","")
    if player~="" and data.musicEligible then noctalia.runAsync({"playerctl","--player",player,action}) end
end
local function lease()
    if leasePath then
        local active=opened and data.playing==true and data.musicEligible==true
        local now=noctalia.nowMs()
        if active and wasLeased and now-lastGranted<900 then return end
        if not active and not wasLeased then return end
        local ok=noctalia.writeFile(leasePath,active and tostring(math.floor(now+2500)) or "0")
        if ok then wasLeased=active;lastGranted=now end
    end
end
local function syncPresented()
    local frame=Fade.frame(songFade)
    local shown=frame.track or {}
    if shown~=presented then
        if trackKey(shown)~=trackKey(presented) then motion=Lyrics.new(shown)
        else Lyrics.push(motion,shown) end
        presented=shown
    end
end
local function advance(now)
    local elapsed=now-lastTick
    Lyrics.tick(motion,elapsed);Fade.tick(songFade,elapsed);lastTick=now
    syncPresented()
end
local function render()
    if not opened then return end
    local display=presented
    local _,trackOpacity=Fade.frame(songFade)
    local rows,offset=Lyrics.frame(motion)
    local lyricRows={}
    for _,row in ipairs(rows)do
        table.insert(lyricRows,ui.column({key=row.key,height=44,align="stretch",justify="center",opacity=row.opacity},{
            ui.label({key="text",text=row.text,fontSize=18,fontWeight="medium",color="primary",
                maxWidth=528,maxLines=2,textAlign="center"}),
        }))
    end
    local columns={}
    for i,height in ipairs(Spectrum.heights(bands,available and data.playing)) do
        table.insert(columns,ui.box({key="band-"..i,width=4,height=height,radius=1,
            fill=available and "primary/0.8" or "on_surface/0.12"}))
    end
    local cover=(display.artPath or "")~="" and ui.image({key="art",path=display.artPath,width=128,height=128,radius=16,fit="cover"})
        or ui.row({key="art-placeholder",width=128,height=128,radius=16,fill="primary/0.10",align="center",justify="center"},{
            ui.glyph({name="music",size=48,color="primary"})})
    local position,duration=display.position or 0,display.duration or 0
    panel.render(ui.column({key="player",padding=16,gap=10,align="stretch"},{
        ui.row({key="song-header",gap=16,height=186,align="start",opacity=trackOpacity},{cover,
            ui.column({key="metadata",width=384,gap=5,align="stretch"},{
                ui.label({key="title",text=display.title or "Музыка",fontSize=20,fontWeight="bold",height=44,maxWidth=384,maxLines=2}),
                ui.label({key="artist",text=display.artist or "",fontSize=13,color="on_surface_variant",height=18,maxWidth=384,maxLines=1}),
                ui.row({justify="space_between",height=16},{
                    ui.label({key="elapsed",text=clock(position),fontSize=11,color="on_surface_variant"}),
                    ui.label({key="remaining",text=duration>0 and "−"..clock(duration-position) or "",fontSize=11,color="on_surface_variant"}),
                }),
                ui.column({key="timeline",gap=0,align="stretch"},{
                    ui.progress({key="progress",height=3,progress=duration>0 and math.max(0,math.min(1,position/duration)) or 0,fill="primary",track="on_surface/0.10"}),
                    ui.row({key="spectrum",height=44,gap=2,align="start",justify="center"},columns),
                }),
                ui.row({key="controls",gap=8,justify="center",align="center",height=34},{
                    ui.button({key="open-lyrics",text="Весь текст",glyph="align-left",width=104,height=32,controlSize="sm",variant="ghost",
                        onClick=function()noctalia.togglePanel("ari/lyrics:panel")end}),
                    ui.button({key="previous",glyph="player-skip-back",height=32,width=40,variant="ghost",onClick=function()control("previous")end}),
                    ui.button({key="play",glyph=data.playing and "player-pause" or "player-play",height=34,width=52,variant="primary",onClick=function()control("play-pause")end}),
                    ui.button({key="next",glyph="player-skip-forward",height=32,width=40,variant="ghost",onClick=function()control("next")end}),
                    ui.button({key="open-search",text="Поиск",glyph="search",width=104,height=32,controlSize="sm",variant="ghost",
                        onClick=function()noctalia.togglePanel("ari/lyrics:search")end}),
                }),
            }),
        }),
        ui.scroll({key="lyric-preview",width=528,height=132,gap=0,padding=0,align="stretch",opacity=trackOpacity},{
            -- Native clipping around a fixed strip keeps glyph geometry intact.
            ui.column({key="lyric-strip",width=528,height=132,paddingV=-offset,gap=0,align="stretch"},lyricRows),
        }),
    }))
end
local function startSpectrum()
    if started then return end
    local runtime=noctalia.getenv("XDG_RUNTIME_DIR")
    if not runtime or runtime=="" then return end
    local directory=runtime.."/ari-lyrics"
    if not noctalia.mkdirAll(directory) then return end
    leasePath=directory.."/spectrum-"..tostring(math.floor(noctalia.nowMs()))..".lease"
    -- Publish before spawning: the companion's first read can start capture
    -- immediately instead of waiting for a later lease polling interval.
    lease()
    local function quote(value)return "'"..string.gsub(value,"'","'\\''").."'"end
    local command="exec "..quote(noctalia.pluginDir().."/bin/ari-lyrics-spectrum").." --lease "..quote(leasePath)
    started=noctalia.runStream(command,function(line)
        if not opened then return end
        local ok,value=pcall(noctalia.json.decode,line)
        if ok and type(value)=="table" then
            local key=tostring(value.available)..":"..table.concat(value.bands or {},",")
            if key~=spectrumKey then
                spectrumKey=key;bands=value.bands or {};available=value.available==true;schedule()
            end
        end
    end)
end
noctalia.state.watch("track",function(value)
    data=value or {}
    if opened then
        local now=noctalia.nowMs()
        advance(now)
        Fade.push(songFade,trackFrame(data),true);syncPresented();lease();schedule()
    end
end)
function onOpen()
    opened=true;data=noctalia.state.get("track") or {}
    songFade=Fade.new(trackFrame(data));presented=songFade.shown.track;motion=Lyrics.new(presented)
    lastTick=noctalia.nowMs();lastPaint=lastTick;dirty=false
    startSpectrum();lease();panel.setNeedsFrameTick(false);render()
end
function onClose()
    opened=false;available=false;bands={};spectrumKey="";dirty=false;lease();panel.setNeedsFrameTick(false)
end
function onExit()
    opened=false;lease()
end
function onFrameTick(dt)
    if not opened then return end
    local now=noctalia.nowMs()
    local wasActive=motion.active or songFade.active
    advance(now)
    local active=motion.active or songFade.active
    if (dirty or wasActive or active) and (now-lastPaint>=16 or (wasActive and not active)) then
        render();dirty=false;lastPaint=now
    end
    panel.setNeedsFrameTick(dirty or active)
end
function update()
    if opened then lease() end
end
noctalia.setUpdateInterval(1000)
