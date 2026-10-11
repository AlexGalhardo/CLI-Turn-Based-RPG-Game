-- Lua 5.5 keeps the 5.4 syntax this project uses; luacheck 1.2.0 knows 5.4.
std = "lua54"
max_line_length = 120
-- Methods take `self` even when a particular one does not use it.
self = false
exclude_files = { "build/" }
