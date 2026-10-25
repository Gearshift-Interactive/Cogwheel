ITERATIONS: int = 10000000
summ: int = 0
i: int = 0
value: int = 0

while i < ITERATIONS:
    summ = summ + value

    value = value + 1
	if value == 100:
        value = 0

    i = i + 1

print("Arithmetic checksum: " + sum)
