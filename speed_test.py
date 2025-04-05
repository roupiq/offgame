import os
import time
import random
from concurrent.futures import ThreadPoolExecutor


def compile(prog, out):
    os.system(f"g++ -O3 -static -std=c++20 {prog} -o {out}")


red_score = 0
blue_score = 0
ties = 0

def wykonaj_funkcje_wielowatkowo(funkcja, liczba_wykonan, threads=14):
    with ThreadPoolExecutor(max_workers=threads) as executor:
        for i in range(liczba_wykonan):
            executor.submit(funkcja, i)

n_games = len(os.listdir("rounds"))
n_games = 1000

def test_speed():
    def gen_features(id):
        print(id)
        with open(f"rounds/{id}.in", "r") as f:
            round = f.read()
            if random.randint(0, 1):
                round += "B"
            else:
                round += "A"
            with open(f"trash/{id}.in", "w") as i:
                i.write(round)
            res = os.system(f"./b < trash/{id}.in > evaluations/{id}.txt")
            if res:
                print(res, id, "\a")
                exit(1)

    print(n_games)
    a = "current"
    compile("bots/" + a + ".cpp", "b")
    begin = time.time()

    wykonaj_funkcje_wielowatkowo(gen_features, n_games, threads=14)

    print(f"{time.time() - begin} s")


test_speed()