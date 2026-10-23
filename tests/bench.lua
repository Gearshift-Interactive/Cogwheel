local ITERATIONS = 10000000
local sum = 0
local i = 0
local value = 0

while i < ITERATIONS do
    sum = sum + value

    value = value + 1
    if value == 100 then
        value = 0
    end

    i = i + 1
end

print("Arithmetic checksum: " .. sum)
