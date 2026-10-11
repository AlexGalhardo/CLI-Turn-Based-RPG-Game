--- The impure edge of the TUI: raw keyboard input, terminal size and output.
---
--- Standard Lua can only read whole lines, so the real work is done by the C host (`host/rpg_host.c`: termios + poll
--- on POSIX, the console API on Windows). Under a plain `lua` interpreter there is no host: `start()` returns false
--- and the game falls back to line input.
---
--- A key is a printable character (UTF-8) or one of "enter", "escape", "backspace", "up", "down", "left", "right",
--- "ctrl+c".

local loaded, host = pcall(require, "rpg_host")
if not loaded then
	host = nil
end

local terminal = {}

local started = false

--- Switches to raw mode, the alternate screen and a hidden cursor. Returns false when there is no host or when
--- stdin or stdout is not an interactive terminal (a pipe, a file).
function terminal.start()
	started = host ~= nil and host.terminal_start() or false
	return started
end

--- Restores the terminal. Safe to call when `start()` failed or was never called.
function terminal.stop()
	if started then
		host.terminal_stop()
		started = false
	end
end

--- Columns and rows.
function terminal.size()
	return host.terminal_size()
end

--- Waits up to `timeout_ms` (negative = forever) for one key. Returns the key, nil on a timeout, or false when the
--- input ended.
function terminal.read_key(timeout_ms)
	return host.terminal_read_key(timeout_ms)
end

function terminal.write(text)
	host.terminal_write(text)
end

--- Milliseconds from a monotonic clock (only differences are meaningful).
function terminal.now_ms()
	return host.terminal_now_ms()
end

return terminal
