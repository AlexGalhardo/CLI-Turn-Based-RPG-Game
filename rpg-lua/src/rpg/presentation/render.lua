--- Framework-independent rendering helpers (bars, colours, list keys), specified in docs/tui.md.

local render = {}

render.BAR_WIDTH = 25
render.MIN_COLUMNS = 100
render.MIN_ROWS = 30
render.LIST_KEYS = "123456789abcdefghijklmnopqrstuvwxyz"

render.ELEMENT_COLORS = {
	physical = "white",
	fire = "red",
	ice = "cyan",
	energy = "magenta",
	earth = "green",
	holy = "yellow",
	death = "bright_black",
}

render.RARITY_COLORS = {
	common = "white",
	rare = "dodger_blue1",
	legendary = "orange1",
	mythic = "medium_purple1",
}

-- Semantic colours used by the equipment screen (docs/tui.md): empty slots, score/stat gains and losses.
render.STYLE_WARNING = "warning"
render.STYLE_GAIN = "gain"
render.STYLE_LOSS = "loss"
render.STYLE_DIM = "dim"
render.STYLE_COLORS = { warning = "yellow", gain = "green", loss = "red", dim = "bright_black" }

--- `█` filled / `░` empty. A living creature always shows at least one filled cell.
function render.bar(current, maximum, width)
	width = width or render.BAR_WIDTH
	if maximum <= 0 then
		return string.rep("░", width)
	end
	local filled = width * math.max(0, math.min(current, maximum)) // maximum
	if current > 0 then
		filled = math.max(1, filled)
	end
	return string.rep("█", filled) .. string.rep("░", width - filled)
end

function render.hp_color(current, maximum)
	if maximum > 0 and current * 100 > maximum * 50 then
		return "green"
	end
	if maximum > 0 and current * 100 > maximum * 25 then
		return "yellow"
	end
	return "red"
end

function render.format_delta(delta)
	return delta > 0 and ("+" .. delta) or tostring(delta)
end

--- `gain`, `loss` or nil.
function render.delta_style(delta)
	if delta > 0 then
		return render.STYLE_GAIN
	end
	if delta < 0 then
		return render.STYLE_LOSS
	end
	return nil
end

--- Key of the list entry at `index` (counting from 0): `1`-`9`, then `a`-`z`.
function render.list_key(index)
	return render.LIST_KEYS:sub(index + 1, index + 1)
end

function render.list_index(key)
	if #key ~= 1 then
		return nil
	end
	local position = render.LIST_KEYS:find(key, 1, true)
	return position ~= nil and position - 1 or nil
end

return render
