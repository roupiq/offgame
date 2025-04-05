#include <bits/stdc++.h>
#include "core.h"
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
    // cerr << fixed << setprecision(4);

    GameState game;

    start_time = microseconds();
    ran = mt19937(12);

    game.read_board();
    max_depth = max_round_num - game.round_num;

    preprocess_bullets(game);
    for (auto &b : game.has_bullet)
        b = 0;
    game.bullets.clear();

    auto [a, b] = game.eval_moves(30, min(160, max_round_num - game.round_num - 1));
    // cout << game.kmonte_carlo_eval(500, min(160, max_round_num - game.round_num - 1)) << "\n";
    // cout << a[a.maxSample()] << "\n";
    if(a.maxSample() > 8 || a.maxSample() < 0)
        cerr << a.maxSample() << "\n";
    cout << a.maxSample() << "\n";
    // cerr << a << "\n";
    // cerr << b << "\n";

    // cout << game.kmonte_carlo_eval(300, max_round_num - game.round_num - 1) << "\n";
    // cout << game.kmonte_carlo_eval(300, max_round_num - game.round_num - 1) << "\n";
    
    // auto [a1, b1] = game.nonlethal();
    // cerr << a1 << "\n";
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