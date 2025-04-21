#include <bits/stdc++.h>
#include "coreold2.h"
// #include "core.h"
using namespace std;
#define all(x) x.begin(), x.end()
#define len(x) (int)x.size()
#define x first
#define y second
using ld = long double;
using i128 = __int128_t;
using pii = pair<int, int>;
using pid = pair<int, double>;

int main()
{
    cerr << fixed << setprecision(4);
    start_time = microseconds();
    ran = mt19937(12);

    GameState game;
    game.read_board();
    preprocess_bullets(game);
    game.bullets.clear();
    for(auto &b : game.has_bullet)
        b = 0;

    max_depth = max_round_num - game.round_num;

    auto [a, b] = game.eval_moves(50, min(150, max_round_num - game.round_num - 1), 2);
    // cout << a << "\n";

    cout << a.maxSample() << "\n";
    // cerr << a << "\n";
}

/*
7 7
#   #   #   #   #   #   #
#   R                   #
#    v                  #
#       #       #       #
#                       #
#                 < B   #
#   #   #   #   #   #   #
1
R

15 10
#   #   #   #   #   #   #   #   #   #
#           #           ^           #
#           #                       #
#                                   #
#               #       ^    v      #
#                   R   B    v      #
#         <   <      v              #
#          >   >     v              #
#                    v >            #
#                   ^v<   <   <   <>#
#                   #               #
#                                   #
#                       #           #
#                       #           #
#   #   #   #   #   #   #   #   #   #
30
R

5 7
#   #   #   #   #   #   #
#               #   B   #
#           R      >    #
#       #               #
#   #   #   #   #   #   #
4
B

5 7
#   #   #   #   #   #   #
#               B      >#
#           R           #
#                       #
#   #   #   #   #   #   #
11
B

5 7
#   #   #   #   #   #   #
#       R           #   #
#        v< ^           #
#   #              >B   #
#   #   #   #   #   #   #
22
B

6 5
#   #   #   #   #
#               #
#   R   #       #
#       #       #
#     <     B   #
#   #   #   #   #
2
R

*/