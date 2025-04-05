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
    GameState game;

    start_time = microseconds();
    ran = mt19937(10);

    game.read_board();
    max_depth = max_round_num - game.round_num;

    preprocess_bullets(game);
    for (auto &b : game.has_bullet)
        b.fill(0);
    game.bullets.clear();

    cout << setprecision(6) << std::fixed;

    cout << game.distance_to_enemy() << "\n";
    cout << game.square_distance_to_bullets() << "\n";
    cout << game.enemy_square_distance_to_bullets() << "\n";
    cout << game.number_of_bullets() << "\n";
    cout << game.achievable_squares() << "\n";
    cout << game.achievable_squares_enemy() << "\n";
    cout << game.round_number() << "\n";
    cout << game.distance_to_center() << "\n";
    cout << game.enemy_distance_to_center() << "\n";
 
    // cout << move << "\n";
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