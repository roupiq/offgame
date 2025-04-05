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
mt19937 ran;
char player_color = '0';

map<int, char> move_codes =
{
    {0, 'w'},
    {1, 's'},
    {2, 'a'},
    {3, 'd'},
    {4, '^'},
    {5, 'v'},
    {6, '<'},
    {7, '>'},
    {8, '_'},
};

struct GameState
{
    int n, m;
    int round_num;
    bool r_killed;
    bool b_killed;

    array<array<char, 20>, 15> board;
    // array<array<int, 20>, 15> has_bullet;
    vector<Bullet> bullets;
    bool pos_has_bullet(pii pos) const
    {
        for (int i = 0; i < len(bullets); i++)
            if(bullets[i].pos == pos)
                return true;
        return false;
        // return has_bullet[pos.x][pos.y];
    }
    char boardf(pii pos) { return board[pos.first][pos.second]; }

    pii r_pos;
    pii b_pos;
    GameState next_board(int move_r, int move_b) const
    {
        GameState next = *this;
        Bullet r_bullet, b_bullet;

        for (int i = 0; i < len(next.bullets); i++)
        {
            if (next.boardf(next.bullets[i].pos + walks[next.bullets[i].dir]) == '#')
                next.bullets[i].dir ^= 1;
            else
                next.bullets[i].pos = next.bullets[i].pos + walks[next.bullets[i].dir];
        }

        if (4 <= move_b && move_b < 8)
        {
            b_bullet.pos = b_pos;
            b_bullet.dir = move_b - 4;
            if (next.boardf(b_bullet.pos + walks[b_bullet.dir]) == '#')
                b_bullet.dir ^= 1;
            else
                b_bullet.pos = b_bullet.pos + walks[b_bullet.dir];
            next.bullets.push_back(b_bullet);
        }
        if (4 <= move_r && move_r < 8)
        {
            r_bullet.pos = r_pos;
            r_bullet.dir = move_r - 4;
            if (next.boardf(r_bullet.pos + walks[r_bullet.dir]) == '#')
                r_bullet.dir ^= 1;
            else
                r_bullet.pos = r_bullet.pos + walks[r_bullet.dir];

            next.bullets.push_back(r_bullet);
        }
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
        next.r_killed = next.pos_has_bullet(next.r_pos);
        next.b_killed = next.pos_has_bullet(next.b_pos);

        next.round_num = round_num + 1;
        return next;
    }

    void move_bullets()
    {
        for (int i = 0; i < len(bullets); i++)
        {
            if (boardf(bullets[i].pos + walks[bullets[i].dir]) == '#')
                bullets[i].dir ^= 1;
            else
                bullets[i].pos = bullets[i].pos + walks[bullets[i].dir];
        }
    }

    void move_players(int move_r, int move_b)
    {
        Bullet r_bullet, b_bullet;

        if (4 <= move_b && move_b < 8)
        {
            b_bullet.pos = b_pos;
            b_bullet.dir = move_b - 4;
            if (boardf(b_bullet.pos + walks[b_bullet.dir]) == '#')
                b_bullet.dir ^= 1;
            else
                b_bullet.pos = b_bullet.pos + walks[b_bullet.dir];
            bullets.push_back(b_bullet);
        }
        if (4 <= move_r && move_r < 8)
        {
            r_bullet.pos = r_pos;
            r_bullet.dir = move_r - 4;
            if (boardf(r_bullet.pos + walks[r_bullet.dir]) == '#')
                r_bullet.dir ^= 1;
            else
                r_bullet.pos = r_bullet.pos + walks[r_bullet.dir];

            bullets.push_back(r_bullet);
        }
        pii nr_pos = r_pos + walks[move_r];
        pii nb_pos = b_pos + walks[move_b];

        if (boardf(nb_pos) == '#')
            nb_pos = b_pos;
        if (boardf(nr_pos) == '#')
            nr_pos = r_pos;

        if (nr_pos == nb_pos)
        {
            nr_pos = r_pos;
            nb_pos = b_pos;
        }

        r_pos = nr_pos;
        b_pos = nb_pos;
        r_killed = pos_has_bullet(r_pos);
        b_killed = pos_has_bullet(b_pos);

        round_num = round_num + 1;
    }

    pair<vector<int>, vector<int>> get_not_stupid_moves()
    {
        move_bullets();

        bool can_shoot_a = !pos_has_bullet(r_pos);
        bool can_shoot_b = !pos_has_bullet(b_pos);
        vector<int> a;
        vector<int> b;
        if (can_shoot_a)
            a.push_back(8);
        if (can_shoot_b)
            b.push_back(8);

        for (int ii = 0; ii < 4; ii++)
        {
            if (boardf(r_pos + walks[ii]) != '#' && !pos_has_bullet(r_pos + walks[ii]))
                a.push_back(ii);
            if (boardf(b_pos + walks[ii]) != '#' && !pos_has_bullet(b_pos + walks[ii]))
                b.push_back(ii);
            if (boardf(r_pos + walks[ii]) != '#' && can_shoot_a)
                a.push_back(ii + 4);
            if (boardf(b_pos + walks[ii]) != '#' && can_shoot_b)
                b.push_back(ii + 4);
        }
        if (len(a) == 0)
        {
            // cerr << "It's a trap\n";
            a = {8};
        }
        if (len(b) == 0)
        {
            // cerr << "Hihi haha\n";
            b = {8};
        }

        // cerr << "1:\n";
        // for(auto e : a)
        // {
        //     cerr << move_codes[e] << " ";
        // }
        // cerr << "\n";
        // cerr << "2:\n";
        // for(auto e : b)
        // {
        //     cerr << move_codes[e] << " ";
        // }
        // cerr << "\n";

        return {a, b};
    }
    pair<int, int> get_random_not_stupid_move()
    {
        auto _m = get_not_stupid_moves();
        return {_m.x[ran() % len(_m.x)], _m.y[ran() % len(_m.y)]};
    }

    void read_board()
    {
        cin >> n >> m;
        char c;
        char P;

        ignore = scanf("\n");
        for (int i = 0; i < n; i++)
        {
            for (int j = 0; j < 4 * m; j++)
            {
                ignore = scanf("%c", &c);
                if (c == '#')
                    board[i][j / 4] = '#';
                else if (c == ' ' && board[i][j / 4] == 0)
                    board[i][j / 4] = ' ';
                else if (c == 'R')
                    r_pos = pii{i, j / 4};
                else if (c == 'B')
                    b_pos = pii{i, j / 4};
                else if (c == '^')
                    bullets.push_back(Bullet{pii{i, j / 4}, 0});
                else if (c == 'v')
                    bullets.push_back(Bullet{pii{i, j / 4}, 1});
                else if (c == '<')
                    bullets.push_back(Bullet{pii{i, j / 4}, 2});
                else if (c == '>')
                    bullets.push_back(Bullet{pii{i, j / 4}, 3});
            }
            ignore = scanf("\n");
        }

        cin >> round_num;
        cin >> P;
        player_color = P;
        if (P == 'B')
            swap(b_pos, r_pos);
    }
};

string input_line;
GameState game;

uint64_t start_time;
long timeout = 4 * 1000;
int get_move(GameState state)
{
    vector<int> move_eval(9);
    for (int k = 0; k < 1e6; k++)
    {
        if (start_time + timeout < microseconds())
        {
            // cerr << "2: " << k << "\n";
            break;
        }
        for (int i = 0; i < 9; i++)
        {
            GameState monte = state;
            int p_move = i;
            int e_move = monte.get_random_not_stupid_move().second;

            for (int j = 0; j < 50 && monte.round_num <= max_round_num; j++)
            {
                monte.move_players(p_move, e_move);
                if (monte.r_killed == 1)
                    move_eval[i] -= 1;
                if (monte.b_killed == 1)
                    move_eval[i] += 1;

                if (monte.r_killed || monte.r_killed)
                    break;

                auto [m1, m2] = monte.get_random_not_stupid_move();
                p_move = m1;
                e_move = m2;
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

#ifdef _GLIBCXX_DEBUG
    for (int i = 0; i < 9; i++)
    {
        cerr << i << ": " << move_eval[i] << "\n";
    }
    cerr << "\n";
#endif
    return move[ran() % len(move)];
}

int main()
{
    ran = mt19937(10);
    game.read_board();
    start_time = microseconds();

    int move = get_move(game);
    cout << move << "\n";
#ifdef _GLIBCXX_DEBUG
    cerr << player_color << ": " << move_codes[move] << "\n";
#endif
}

/*
7 7
#   #   #   #   #   #   #   
#   R                   #   
#                       #   
#       #       #       #   
#                       #   
#                   B   #   
#   #   #   #   #   #   #   
0
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