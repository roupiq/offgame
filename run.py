import os

blue_score = 0
red_score = 0
ties = 0

def compile(prog, out):
    os.system(f"g++ -O3 -static -std=c++20 {prog} -o {out}")

def run_game(id):
    global red_score, blue_score, ties
    print(id)
    os.system(f"python3 gra.py -r ./r -b ./b --silent --nice-print -n 15 -m 20 -w 15 --round-count 200 --id {id} --timeout 10000")
    with open(f"results{id}", "r") as file:
        r = file.read()
        if r == "RED":
            red_score += 1
        if r == "BLUE":
            blue_score += 1
        if r == "TIE":
            ties += 1
        os.remove(f"results{id}")
# a = "template"
# a = "roupiq3.5-turbo"
# a = "roupiq1.2"
# a = "roupiq1.1"
# a = "roupiq1.1"
a = "roupiq1.1"
b = "current"

"""
oldbest : 0.000000%
andr729 : 30.000000%
TIES    : 70.000000%

roupiq3.5-turbo : 5.000000%
andr729         : 10.000000%
TIES            : 85.000000%

roupiq3.5-turbo : 0.000000%
oldbest         : 5.000000%
TIES            : 95.000000%
"""

compile("bots/" + a + ".cpp", "r")
compile("bots/" + b + ".cpp", "b")

import random

seed = random.randint(0, 1000)
# os.system(f"python3 gra.py -r ./r -b ./b -n 7 -m 7 -w 5 --round-count 400 --wait 20 --seed {seed}")
# os.system(f"python3 gra.py -r ./r -b ./b -n 15 -m 20 -w 7 --round-count 400 --wait 200 --seed {seed}")

# while True:
#     seed = random.randint(0, 1000)
#     os.system(f"python3 gra.py --nice-print -r ./r -b ./b -n 15 -m 20 -w 15 --round-count 400 --wait 100 --seed {seed} --timeout 100000")
#     # os.system(f"python3 gra.py -r ./r -b ./b -n 7 -m 7 -w 5 --round-count 400 --wait 20 --seed {seed}")
#     # os.system(f"python3 gra.py -r ./r -b ./b --nice-print -n 15 -m 20 -w 15 --round-count 400 --wait 20 --seed {seed}")
#     print("seed: ", seed)
# exit(0)

n_games = 30

import time
begin = time.time() 

threads = 15

from concurrent.futures import ThreadPoolExecutor

def wykonaj_funkcje_wielowatkowo(funkcja, liczba_wykonan, max_watkow_na_raz=14):
    with ThreadPoolExecutor(max_workers=max_watkow_na_raz) as executor:
        for i in range(liczba_wykonan):
            executor.submit(funkcja, i)

wykonaj_funkcje_wielowatkowo(run_game, n_games, threads)

print(f'{time.time() - begin} s')

l = max(len(a), len(b), 4)
print(f"""{a:{" "}{'<'}{l}} : {red_score / n_games * 100:3f}%
{b:{" "}{'<'}{l}} : {blue_score / n_games * 100:3f}%
{"TIES":{" "}{'<'}{l}} : {ties / n_games * 100:3f}%""")
# os.system(f"python3 gra.py -r ./{a} -b ./{b} -n 5 -m 7 -w 7 --round-count 400 --wait 100")

"""

WOJTEK      : 28%
roupiq      : 25%
TIES        : 47%

46.13912320137024 s
WOJTEK      : 12%
roupiq      : 33%
TIES        : 55%

40.05271077156067 s
WOJTEK      : 12%
roupiq      : 35%
TIES        : 53%

35.912309646606445 s
WOJTEK      : 11%
roupiq      : 26%
TIES        : 61%

421.9196081161499 s
WOJTEK      : 15%
roupiq      : 53%
TIES        : 31%

500 ms na ruch
417.4000608921051 s
WOJTEK      : 14%
roupiq      : 57%
TIES        : 28%

50 ms na ruch
71.68465995788574 s
WOJTEK      : 22%
roupiq      : 54%
TIES        : 20%

154.844957113266 s
roupiq       :  11%
roupiq2      :  11%
TIES         :  76%

mądre ruchy
108.24176001548767 s
roupiq       :  15%
roupiq2      :  19%
TIES         :  66%

300 gier mądre ruchy
roupiq       : 21.0000%
roupiq2      : 11.6667%
TIES         : 65.3333%

118.83672785758972 s
roupiq       : 27.10%
roupiq2      : 13.80%
TIES         : 55.80%

wojtek   : 79.00%
template : 11.00%
TIES     : 7.00%

Mądre ruchy dalej nie działają
108.43405723571777 s
roupiq       : 15.00%
roupiq2      : 10.00%
TIES         : 74.00%

Sprawdzę przeciwko Wojtkowi
57.4377384185791 s
wojtek       : 27.00%
roupiq2      : 59.00%
TIES         : 12.00%

wojtek      : 20.00%
roupiq      : 61.00%
TIES        : 18.00%

Krótsze ścierzki do sprawdzania w roupiq2
112.86992812156677 s
roupiq       : 12.00%
roupiq2      : 13.00%
TIES         : 73.00%

Krótsze ścierzki do sprawdzania w roupiq2 i roupiq     
112.99028134346008 s
roupiq       : 13.00%
roupiq2      : 15.00%
TIES         : 72.00%

1163.2926802635193 s
roupiq       : 8.00%
roupiq2      : 9.00%
TIES         : 83.00%

25
172.2620952129364 s
roupiq       : 21.33%
roupiq2      : 12.33%
TIES         : 64.00%

bug z roupiq2 i roupiq3
roupiq       : 5.20%
roupiq     2 : 20.20%
TIES         : 73.00%

wojtek       : 21.00%
roupiq     2 : 64.00%
TIES         : 12.00%

.04s
60.10012483596802 s
wojtek  : 17.00%
roupiq2 : 61.00%
TIES    : 21.00%

.004s
26.118939638137817 s
wojtek  : 30.00%
roupiq2 : 52.00%
TIES    : 16.00%

.004s
13.844738006591797 s
roupiq2 : 8.00%
roupiq3 : 15.00%
TIES    : 77.00%

.004s
22.05403757095337 s
wojtek  : 21.00%
roupiq3 : 63.00%
TIES    : 16.00%

.04s
55.58983635902405 s
wojtek  : 6.00%
roupiq3 : 74.00%
TIES    : 18.00%

NAPRAWIONY BUG Z REMISEM
-=-=-=-=-=-=-=-=-=-=--=-=-=-

.4s
387.34967970848083 s
wojtek  : 1.00%
roupiq3 : 95.00%
TIES    : 4.00%

.4s
964.0209360122681 s
roupiq2 : 1.00%
roupiq3 : 3.00%
TIES    : 96.00%

.004s
roupiq2 : 7.00%
roupiq3 : 15.00%
TIES    : 78.00%

bug z nie czyszczoną tablicą
36.65944576263428 s
roupiq3.5   : 9.33%
roupiq3.5.1 : 15.00%
TIES        : 75.666667%

.004s
36.712454080581665 s
roupiq3.5       : 8.33%
roupiq3.5-turbo : 15.666667%
TIES            : 76.00%

73.62357425689697 s
wojtek          : 19.33%
roupiq3.5-turbo : 74.33%
TIES            : 6.33%

34.579805850982666 s
roupiq2         : 3.333333%
roupiq3.5-turbo : 26.333333%
TIES            : 70.333333%

74.51962900161743 s
roupiq2 : 56.000000%
wojtek  : 32.000000%
TIES    : 12.000000%

roupiq3.5-turbo : 81.000000%
wojtek          : 13.666667%
TIES            : 5.333333%
"""