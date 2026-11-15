-- One native backend owns MPRIS and lyrics. Presentation belongs to the bar;
-- the old floating mini-panel is no longer opened automatically.
local binary=noctalia.pluginDir().."/bin/ari-lyrics"
local quoted="'"..string.gsub(binary,"'","'\\''").."'"
local started=false
noctalia.state.set("track",{state="idle",musicEligible=false,playing=false,lines={}})
function update()
    if started then return end
    started=noctalia.runStream("exec "..quoted,function(line)
        local ok,data=pcall(noctalia.json.decode,line)
        if ok and type(data)=="table" then noctalia.state.set("track",data) end
    end)
    if not started then
        noctalia.state.set("track",{state="error",musicEligible=false,message="Не удалось запустить C++ модуль",lines={}})
    end
end
noctalia.setUpdateInterval(1000)
update()
