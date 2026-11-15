local Island = {}
local function identity(data)
    return (data.player or '')..'\n'..(data.trackId or data.title or '')..'\n'..(data.artist or '')
end
function Island.new(data,now)
    local m={data={},key='',lastLine='',pausedAt=nil}
    Island.observe(m,data or {},now)
    return m
end
function Island.observe(m,data,now)
    data=data or {}
    local key=identity(data)
    if key~=m.key then m.lastLine='';m.pausedAt=nil;m.key=key end
    local active=data.musicEligible==true and (data.title or '')~=''
    if not active then
        m.lastLine='';m.pausedAt=nil
    elseif data.playing then
        m.pausedAt=nil
        m.lastLine=(data.line~='♪' and data.line) or ''
    else
        if m.pausedAt==nil then m.pausedAt=now end
        if data.line and data.line~='' and data.line~='♪' then m.lastLine=data.line end
    end
    m.data=data
end
function Island.content(m,now,clock)
    local d=m.data
    if d.musicEligible~=true or (d.title or '')=='' or (m.pausedAt and now-m.pausedAt>=300000) then
        return {mode='clock',text=clock}
    end
    local text=m.lastLine or ''
    if text=='' then
        text=d.title
        if (d.artist or '')~='' then text=text..' · '..d.artist end
    end
    return {mode='music',text=text}
end
function Island.animation(width)
    return {width=width,from=width,to=width,shown={mode='clock',text=''},target={mode='clock',text=''},elapsed=2200,duration=2200,active=false}
end
function Island.target(a,content,width)
    if content.mode==a.target.mode and content.text==a.target.text and width==a.to then return end
    -- Preserve width and the in-flight fade when high-frequency lyrics arrive.
    if not a.active or content.mode~=a.target.mode then
        local _,alpha,shown=Island.frame(a)
        a.startAlpha=alpha;a.shown=shown
        a.duration=content.mode~=a.target.mode and 2200 or 560
        a.from=a.width;a.elapsed=0;a.active=true
    end
    a.target=content;a.to=width
end
function Island.tick(a,dt)
    if not a.active then return end
    a.elapsed=a.elapsed+math.max(0,math.min(dt,1000))
    local t=math.min(1,a.elapsed/a.duration)
    -- Zero velocity and acceleration at both ends of the slow opening.
    local p=t*t*t*(t*(6*t-15)+10)
    a.width=a.from+(a.to-a.from)*p
    if a.elapsed>=a.duration then a.width=a.to;a.shown=a.target;a.active=false end
end
function Island.frame(a)
    local alpha,shown=1,a.shown
    if a.active then
        local fadeOut=a.duration==2200 and 220 or 140
        local fadeIn=a.duration==2200 and 1800 or 340
        if a.elapsed<fadeOut then alpha=(a.startAlpha or 1)*(1+math.cos(math.pi*a.elapsed/fadeOut))/2
        else shown=a.target;alpha=(1-math.cos(math.pi*math.min(1,(a.elapsed-fadeOut)/fadeIn)))/2 end
    end
    return a.width,alpha,shown
end
return Island
