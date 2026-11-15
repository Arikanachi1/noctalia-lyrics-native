local Control = {}
function Control.needsClock(data)
    return data.musicEligible == true and data.playing == true and data.timingAvailable ~= true
        and string.find(data.player or "", "org.mpris.MediaPlayer2.firefox.", 1, true) == 1
end
-- Explicit user action for Firefox's missing-position bug. Never called by
-- the service, song search, line changes or the fullscreen visibility logic.
function Control.restoreClock(data)
    if not Control.needsClock(data) then return end
    local player = string.gsub(data.player, "^org%.mpris%.MediaPlayer2%.", "")
    noctalia.runAsync({"playerctl", "--player", player, "pause"}, function(result)
        if result.exitCode == 0 then
            noctalia.runAsync({"/bin/sh", "-c", "sleep 0.3\nexec playerctl --player \"$1\" play", "ari-lyrics-clock", player})
        end
    end)
end
return Control
