import random

length = 10000
bit_sequence = ''.join(random.choice('01') for _ in range(length))

with open('random_bits.txt', 'w') as f:
    f.write(bit_sequence)
