import os
import time
import random
from concurrent.futures import ThreadPoolExecutor


def compile(prog, out):
    os.system(f"g++ -O3 -static -std=c++20 {prog} -o {out}")


red_score = 0
blue_score = 0
ties = 0


def run_game(id):
    global red_score, blue_score, ties
    print(id)
    os.system(
        f"python3 gra.py -r ./r -b ./b --silent -n 15 -m 20 -w 15 --round-count 400 --id {id} --timeout 10000"
    )
    # print("pomidor")
    with open(f"results{id}", "r") as file:
        r = file.read()
        if r == "RED":
            red_score += 1
        if r == "BLUE":
            blue_score += 1
        if r == "TIE":
            ties += 1
        # print("wololo2")
        os.remove(f"results{id}")
        # print("wololo")


def wykonaj_funkcje_wielowatkowo(funkcja, liczba_wykonan, threads=14):
    with ThreadPoolExecutor(max_workers=threads) as executor:
        for i in range(liczba_wykonan):
            executor.submit(funkcja, i)


def gen_games():
    def gen_test_case(id):
        # os.system('rm current_games/*.in')
        # os.system(f"mkdir current_game/{id}")
        run_game(id)

    choosen_rounds = 0
    filenames = []
    for filename in os.listdir("current_game"):
        filenames.append(filename)
    print(filenames)
    for filename in filenames:
        os.system(f"mv current_game/{filename} rounds/{choosen_rounds}.in")
        choosen_rounds += 1

    a = "roupiq3.5-turbo"
    b = "andr729"
    compile("bots/" + a + ".cpp", "r")
    compile("bots/" + b + ".cpp", "b")
    begin = time.time()

    n_games = 1000
    wykonaj_funkcje_wielowatkowo(gen_test_case, n_games, threads=14)

    print(f"{time.time() - begin} s")


# gen_games()
# l = max(len(a), len(b), 4)
# print(
#     f"""{a:{" "}{'<'}{l}} : {red_score / n_games * 100:3f}%
# {b:{" "}{'<'}{l}} : {blue_score / n_games * 100:3f}%
# {"TIES":{" "}{'<'}{l}} : {ties / n_games * 100:3f}%"""
# )
# # os.system(f"python3 gra.py -r ./{a} -b ./{b} -n 5 -m 7 -w 7 --round-count 400 --wait 100")

n_games = len(os.listdir("rounds"))
n_games = 1000

def gen_metafeatures():
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
            res2 = os.system(f"./r < trash/{id}.in > features/{id}.txt")
            if res or res2:
                print(res, res2, id, "\a")

    # dirs = []
    # for f in os.listdir("current_game"):
    #     dirs.append("current_game/" + f)

    # for i, f in enumerate(dirs):
    #     os.system(f"mv {f} rounds/{i}.in")

    print(n_games)
    a = "eval"
    b = "current"
    compile("bots/" + a + ".cpp", "b")
    compile("bots/" + b + ".cpp", "r")
    begin = time.time()

    wykonaj_funkcje_wielowatkowo(gen_features, n_games, threads=14)

    print(f"{(time.time() - begin) / 1000} games/s")


gen_metafeatures()


tests = []

# save to json
for i in range(n_games):
    with open(f"evaluations/{i}.txt", "r") as f:
        z = f.read()
        r = [float(z)]
        tests.append(r)

    with open(f"features/{i}.txt", "r") as f:
        z = f.read()
        z = z.split()
        for j in range(0, len(z)):
            r.append(float(z[j]))
        print(i, r)
        tests.append(r)
    

import pickle

with open('features.pkl', 'wb') as fp:
    pickle.dump(tests, fp)
