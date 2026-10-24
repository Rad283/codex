-- This script runs in the target process's independent Luau VM.
-- Roblox's game/workspace/Instance APIs are not available.
local squares: {number} = {}
for i = 1, 5 do
    table.insert(squares, i * i)
end
print("Host process:", _HOST_PID)
print("Squares:", table.concat(squares, ", "))
