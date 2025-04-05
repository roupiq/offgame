#include <bits/stdc++.h>
using namespace std;
#define all(x) x.begin(), x.end()
#define len(x) (int)x.size()
#define x first
#define y second
using ld = long double;
using i128 = __int128_t;
using pii = pair<int, int>;

#define pii pair<int, int>
#define pid pair<int, double>

long microseconds()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct Bullet
{
    pii pos;
    int dir;
};

pii operator+(pii a, pii b)
{
    return pii{a.first + b.first, a.second + b.second};
}

array<pii, 9> walks = {pii{-1, 0}, pii{1, 0}, pii{0, -1}, pii{0, 1},
                       pii{0, 0}, pii{0, 0}, pii{0, 0}, pii{0, 0}, pii{0, 0}};

int manhat(pii a, pii b) { return abs(a.x - b.x) + abs(a.y - b.y); }

const int max_round_num = 400;

struct GameState
{
    int n, m;
    int round_num;
    bool r_killed;
    bool b_killed;

    array<array<char, 20>, 15> board;
    vector<Bullet> bullets;
    bool pos_has_bullet(pii pos) const
    {
        for (int i = 0; i < len(bullets); i++)
        {
            if (bullets[i].pos == pos)
                return true;
        }

        return false;
    }
    char boardf(pii pos) { return board[pos.first][pos.second]; }

    pii r_pos;
    pii b_pos;
    GameState move(int move_r, int move_b) const
    {
        GameState next = *this;
        Bullet r_bullet, b_bullet;

        pii nr_pos = r_pos + walks[move_r];
        pii nb_pos = b_pos + walks[move_b];

        if (next.boardf(nb_pos) == '#')
            nb_pos = b_pos;
        if (next.boardf(nr_pos) == '#')
            nr_pos = r_pos;

        if (nr_pos == nb_pos)
        {
            nr_pos = r_pos;
            nb_pos = b_pos;
        }

        next.r_pos = nr_pos;
        next.b_pos = nb_pos;

        if (4 <= move_b && move_b < 8)
        {
            b_bullet.pos = b_pos;
            b_bullet.dir = move_b - 4;
            next.bullets.push_back(b_bullet);
        }
        if (4 <= move_r && move_r < 8)
        {
            r_bullet.pos = r_pos;
            r_bullet.dir = move_r - 4;
            next.bullets.push_back(r_bullet);
        }

        for (int i = 0; i < len(next.bullets); i++)
        {
            if (next.boardf(next.bullets[i].pos + walks[next.bullets[i].dir]) == '#')
            {
                next.bullets[i].dir ^= 1;
            }
            else
            {
                next.bullets[i].pos = next.bullets[i].pos + walks[next.bullets[i].dir];
            }
        }

        next.round_num = round_num + 1;
        next.r_killed = next.pos_has_bullet(next.r_pos);
        next.b_killed = next.pos_has_bullet(next.b_pos);
        return next;
    }

    void read_board()
    {
        cin >> n >> m;
        char c;
        char P;

        scanf("\n");
        for (int i = 0; i < n; i++)
        {
            for (int j = 0; j < 4 * m; j++)
            {
                scanf("%c", &c);
                if (c == '#')
                {
                    board[i][j / 4] = '#';
                }
                else if (c == ' ' && board[i][j / 4] == 0)
                {
                    board[i][j / 4] = ' ';
                }
                else if (c == 'R')
                {
                    r_pos = pii{i, j / 4};
                }
                else if (c == 'B')
                {
                    b_pos = pii{i, j / 4};
                }
                else if (c == '^')
                {
                    bullets.push_back(Bullet{pii{i, j / 4}, 0});
                }
                else if (c == 'v')
                {
                    bullets.push_back(Bullet{pii{i, j / 4}, 1});
                }
                else if (c == '<')
                {
                    bullets.push_back(Bullet{pii{i, j / 4}, 2});
                }
                else if (c == '>')
                {
                    bullets.push_back(Bullet{pii{i, j / 4}, 3});
                }
            }
            scanf("\n");
        }

        cin >> round_num;
        cin >> P;
        if (P == 'B')
            swap(b_pos, r_pos);
    }
};

string input_line;
GameState game;
mt19937 ran;

uint64_t start_time;
long timeout = 4 * 1000;
int get_move(GameState state)
{
    vector<int> move_eval(9);
    for (int k = 0; k < 50000; k++)
    {
        if (start_time + timeout < microseconds())
        {
            // cerr << "1: " << k << "\n";

            break;
        }
        for (int i = 0; i < 9; i++)
        {
            GameState monte = state;
            int p_move = i;
            int e_move = (int)(ran() % 9);
            for (int j = 0; j < 50 && monte.round_num <= max_round_num; j++)
            {
                monte = monte.move(p_move, e_move);
                if (monte.r_killed == 1)
                    move_eval[i] -= 1;
                if (monte.b_killed == 1)
                    move_eval[i] += 1;

                if (monte.r_killed || monte.r_killed)
                    break;

                vector<int> possible_moves_p = {8};
                vector<int> possible_moves_e = {8};
                for (int ii = 0; ii < 4; ii++)
                {
                    if (monte.boardf(monte.r_pos + walks[ii]))
                        possible_moves_p.push_back(ii);
                    if (monte.boardf(monte.b_pos + walks[ii]))
                        possible_moves_e.push_back(ii);
                }
                for (int ii = 4; ii < 8; ii++)
                {
                    if (monte.boardf(monte.r_pos + walks[ii]))
                        possible_moves_p.push_back(ii);
                    if (monte.boardf(monte.b_pos + walks[ii]))
                        possible_moves_e.push_back(ii);
                }

                e_move = possible_moves_e[ran() % len(possible_moves_e)];
                p_move = possible_moves_p[ran() % len(possible_moves_p)];

                // p_move = (int)(ran() % 9);
                // e_move = (int)(ran() % 9);
            }
            // cerr << monte.r_killed << " " << monte.b_killed << "\n";
            // cerr << "\n";
        }
    }

    int opt = -1e9;
    vector<int> move;
    for (int i = 0; i < 9; i++)
    {
        if (opt == move_eval[i])
        {
            move.push_back({i});
        }
        if (opt < move_eval[i])
        {
            move = {i};
            opt = move_eval[i];
        }
    }

    // for (int i = 0; i < 9; i++)
    // {
    //     cerr << i << ": " << move_eval[i] << "\n";
    // }
    // cerr << "\n";
    return move[ran() % len(move)];
}

int main()
{
    game.read_board();
    
    start_time = microseconds();
    ran = mt19937(start_time);
    
    int move = get_move(game);
    cout << move << "\n";
    // cerr << move << "\n";
}

/*
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