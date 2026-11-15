local data = noctalia.state.get("track") or {}
local binary = noctalia.pluginDir() .. "/bin/ari-lyrics"
local Control = require("./lib/control.luau")
local opened = false
local query, message = "", ""
local results, selected = {}, nil
local busy = false
local revision, queryKey = 0, 0
local resultPage, textPage = 0, 0
local pageSize = 4
local boundTrack = ""
local render
local function duration(seconds)
    seconds = math.max(0, math.floor(seconds or 0))
    return string.format("%d:%02d", math.floor(seconds / 60), seconds % 60)
end
local function decode(result)
    local ok, value = pcall(noctalia.json.decode, result.stdout or "")
    return ok and type(value) == "table" and value or {}
end
local function canApply()
    return data.musicEligible == true and data.trackId == boundTrack and data.title and data.title ~= ""
end
local function search()
    if busy or query == "" then return end
    revision = revision + 1
    local request = revision
    busy = true; selected = nil; results = {}; resultPage = 0; textPage = 0
    message = "Загружаю текст и временные метки…"; render()
    local accepted = noctalia.runAsync({binary, "--search", query, "--title", data.title or "",
        "--artist", data.artist or "", "--duration", tostring(data.duration or 0)}, function(result)
        if request ~= revision or not opened then return end
        busy = false
        local value = decode(result)
        results = value.results or {}
        message = value.error and value.error ~= "" and value.error
            or (#results > 0 and ("Найдено: " .. #results .. " · выбери запись для просмотра текста")
                or "Текст не найден. Попробуй название без исполнителя или его оригинальное написание.")
        render()
    end)
    if not accepted then busy = false; message = "Не удалось запустить поиск"; render() end
end
local function choose()
    if busy or not selected or not canApply() then return end
    local target, request = boundTrack, revision + 1
    revision = request; busy = true; message = "Подключаю текст к текущей песне…"; render()
    local accepted = noctalia.runAsync({binary, "--choose-lyrics", target, tostring(selected.id)}, function(result)
        if request ~= revision or not opened or data.trackId ~= target then return end
        busy = false
        local value = decode(result)
        if value.ok then panel.close()
        else message = value.error or "Не удалось загрузить текст"; render() end
    end)
    if not accepted then busy = false; message = "Не удалось подключить текст"; render() end
end
local function reset()
    if busy or not canApply() then return end
    revision = revision + 1
    local request, target = revision, boundTrack
    busy = true; render()
    local accepted = noctalia.runAsync({binary, "--clear-lyrics", target}, function(result)
        if request ~= revision or not opened or data.trackId ~= target then return end
        busy = false
        message = decode(result).ok and "Автоматический поиск включён" or "Не удалось сбросить выбор"
        render()
    end)
    if not accepted then busy = false; message = "Не удалось сбросить выбор"; render() end
end
local function timingFits(entry)
    local length, actual = entry.duration or 0, data.duration or 0
    return length <= 0 or actual <= 0 or math.abs(length - actual) <= 3
end
local function livePreview(entry)
    return entry.synced == true and data.timingAvailable == true and timingFits(entry)
end
local function previewIndex(entry)
    local index, position = 0, (data.position or 0) - (data.offset or 0)
    for i, line in ipairs(entry.lines or {}) do
        if line.time > position then break end
        index = i
    end
    return index
end
local function textSegments(entry)
    local words = entry.plain or ""
    if words == "" then
        local lines = {}; for _, line in ipairs(entry.lines or {}) do table.insert(lines, line.text) end
        words = table.concat(lines, "\n")
    end
    local segments = {}
    for line in string.gmatch((words ~= "" and words or "Инструментальная композиция") .. "\n", "(.-)\n") do
        local part, columns = {}, 0
        for _, code in utf8.codes(line) do
            local width = code >= 0x1100 and 2 or 1
            if columns + width > 62 then table.insert(segments, table.concat(part)); part = {}; columns = 0 end
            table.insert(part, utf8.char(code)); columns = columns + width
        end
        table.insert(segments, #part > 0 and table.concat(part) or " ")
    end
    return segments
end
render = function()
    if not opened then return end
    local content, pages = {}, 1
    local live = selected and livePreview(selected)
    if selected then
        table.insert(content, ui.label({text=selected.title .. " · " .. selected.artist, fontSize=16,
            fontWeight="bold", maxWidth=518, maxLines=2, height=42}))
        local description = selected.synced and "Текст с временными метками" or "Только текст · без подсветки по времени"
        if selected.synced and not timingFits(selected) then description = "Другая длина записи · синхронизация отключена"
        elseif selected.synced and not data.timingAvailable then description = "Временные метки есть · плеер пока не передаёт позицию"
        elseif live then description = "Предпросмотр с текущего момента песни" end
        table.insert(content, ui.label({text=duration(selected.duration) .. " · " .. description,
            fontSize=11, maxWidth=518, maxLines=2, height=30, color="on_surface_variant"}))
        if live then
            local lines, current = selected.lines or {}, previewIndex(selected)
            if current == 0 then table.insert(content, ui.label({text="♪ Вступление", fontSize=16, height=36, color="primary"})) end
            local first = math.max(1, current - 1)
            for i=first, math.min(#lines, first + (current == 0 and 2 or 3)) do
                table.insert(content, ui.label({key="live-line-" .. i, text=lines[i].text ~= "" and lines[i].text or "♪",
                    height=36, fontSize=i == current and 17 or 13, maxWidth=518, maxLines=2,
                    color=i == current and "primary" or "on_surface_variant", fontWeight=i == current and "bold" or "normal"}))
            end
        else
            local segments = textSegments(selected)
            pages = math.max(1, math.ceil(#segments / pageSize)); textPage = math.min(textPage, pages - 1)
            for i=textPage * pageSize + 1, math.min(#segments, (textPage + 1) * pageSize) do
                table.insert(content, ui.label({key="preview-" .. i, text=segments[i], height=36, fontSize=14, maxWidth=518, maxLines=2}))
            end
        end
    else
        pages = math.max(1, math.ceil(#results / pageSize))
        for i=resultPage * pageSize + 1, math.min(#results, (resultPage + 1) * pageSize) do
            local entry = results[i]
            local badge = entry.synced and "С временными метками" or "Обычный текст"
            if entry.matchesCurrent then badge = badge .. " · совпадает с песней"
            elseif entry.durationFits then badge = badge .. " · длина совпадает" end
            table.insert(content, ui.column({key="record-" .. tostring(entry.id), height=57, padding=7, gap=3, radius=7,
                fill="surface_variant/0.4", onClick=function() selected=entry; textPage=0; render() end}, {
                ui.label({text=entry.title .. " · " .. entry.artist, fontSize=13, fontWeight="medium", maxWidth=518, maxLines=1}),
                ui.label({text=duration(entry.duration) .. " · " .. badge, fontSize=10, color="on_surface_variant", maxWidth=518, maxLines=1}),
            }))
        end
        if #content == 0 then table.insert(content, ui.label({text=busy and "Загружаю слова…"
            or "Поиск загрузит слова сюда. Записи с временными метками следуют за позицией песни.",
            maxWidth=518, maxLines=4, fontSize=14, color="on_surface_variant"})) end
    end
    panel.render(ui.column({key="search", width=572, height=532, padding=12, gap=8, align="stretch"}, {
        ui.row({height=24, justify="space_between", align="center"}, {
            ui.label({text="Текст текущей песни", fontSize=18, fontWeight="bold"}),
            ui.row({width=24, height=24, align="center", justify="center", onClick=function() panel.close() end}, {ui.glyph({name="close", size=16})}),
        }),
        ui.label({key="track", text=((data.title or "") ~= "" and data.title or "Включи YouTube Music") .. " · " .. (data.artist or ""),
            fontSize=12, maxWidth=548, maxLines=1, height=18}),
        ui.row({height=26, gap=10, align="center"}, {
            ui.label({text=data.timingAvailable and (duration(data.position) .. " / " .. duration(data.duration))
                or "Плеер пока не передаёт время", fontSize=11, color="on_surface_variant"}),
            ui.progress({progress=(data.duration or 0) > 0 and math.min(1, (data.position or 0) / data.duration) or 0, height=3, flexGrow=1}),
            ui.button({glyph="refresh", controlSize="sm", visible=Control.needsClock(data),
                tooltip="Получить время Firefox короткой паузой и продолжением", onClick=function() Control.restoreClock(data) end}),
        }),
        ui.row({gap=8, height=32, align="center"}, {
            ui.input({key="query-" .. queryKey, value=query, placeholder="Название песни и исполнитель", controlSize="sm", flexGrow=1,
                focus=true, onChange=function(value) query=value end, onSubmit=search}),
            ui.button({text="Найти текст", glyph="search", controlSize="sm", enabled=not busy, onClick=search}),
        }),
        ui.label({key="message", text=message, fontSize=11, maxWidth=548, maxLines=2, height=30, color="on_surface_variant"}),
        -- Bounded pages prevent API24 scroll containers growing past the window.
        ui.column({key=selected and "preview-page" or "results-page", height=264, width=548, gap=6, align="stretch"}, content),
        ui.row({key="pager", height=24, gap=8, justify="center", align="center"}, {
            ui.row({width=24, height=24, align="center", justify="center", onClick=function()
                if live then return end
                if selected then textPage=math.max(0, textPage-1) else resultPage=math.max(0, resultPage-1) end; render()
            end}, {ui.glyph({name="chevron-left", size=16, color="primary"})}),
            ui.label({text=live and "Следует за воспроизведением" or tostring((selected and textPage or resultPage)+1) .. " / " .. pages,
                fontSize=11, color="on_surface_variant"}),
            ui.row({width=24, height=24, align="center", justify="center", onClick=function()
                if live then return end
                if selected then textPage=math.min(pages-1, textPage+1) else resultPage=math.min(pages-1, resultPage+1) end; render()
            end}, {ui.glyph({name="chevron-right", size=16, color="primary"})}),
        }),
        ui.row({gap=7, height=32, justify="space_between", align="center"}, {
            ui.button({text="Назад", glyph="arrow-left", controlSize="sm", visible=selected ~= nil, onClick=function() selected=nil; render() end}),
            ui.button({text="Показывать этот текст", controlSize="sm", visible=selected ~= nil, enabled=not busy and canApply(), onClick=choose}),
            ui.button({text="Автопоиск", controlSize="sm", variant="ghost", visible=data.manualMatch == true, enabled=not busy and canApply(), onClick=reset}),
        }),
    }))
end
noctalia.state.watch("track", function(value)
    local previous = data
    data = value or {}
    if not opened then return end
    if data.trackId ~= boundTrack then
        revision=revision+1; boundTrack=data.trackId or ""; busy=false; selected=nil; results={}
        query=(data.title or "") .. ((data.artist or "") ~= "" and (" " .. data.artist) or "")
        queryKey=queryKey+1; message="Песня сменилась · найди текст для новой песни"
        render(); return
    end
    if math.floor(previous.position or 0) ~= math.floor(data.position or 0) or previous.playing ~= data.playing
        or previous.state ~= data.state or previous.offset ~= data.offset or previous.manualMatch ~= data.manualMatch
        or previous.duration ~= data.duration then render() end
end)
function onOpen()
    opened=true; data=noctalia.state.get("track") or {}; boundTrack=data.trackId or ""
    query=(data.title or "") .. ((data.artist or "") ~= "" and (" " .. data.artist) or "")
    selected=nil; results={}; message=""; busy=false; revision=revision+1; queryKey=queryKey+1
    resultPage=0; textPage=0; render()
    if query ~= "" then search() end
end
function onClose() opened=false; revision=revision+1; busy=false end
function update() end
