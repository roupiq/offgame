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

array<array<bool, 20>, 15> visited;

bool visitedf(pii pos) { return visited[pos.x][pos.y]; }

int manhat(pii a, pii b) { return abs(a.x - b.x) + abs(a.y - b.y); }

const int max_round_num = 400;
mt19937 ran;
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

    pair<vector<int>, vector<int>> get_not_stupid_moves()
    {
        GameState next = *this;
        next = next.move(8, 8);
        bool can_shoot_a = !next.r_killed;
        bool can_shoot_b = !next.b_killed;
        vector<int> a;
        vector<int> b;
        if(can_shoot_a)
            a.push_back(8);
        if(can_shoot_b)
            b.push_back(8);
        
        for (int ii = 0; ii < 4; ii++)
        {
            if (next.boardf(next.r_pos + walks[ii]) != '#' && !next.pos_has_bullet(next.r_pos + walks[ii]))
                a.push_back(ii);
            if (next.boardf(next.b_pos + walks[ii]) != '#' && !next.pos_has_bullet(next.b_pos + walks[ii]))
                b.push_back(ii);
            if (next.boardf(next.r_pos + walks[ii]) != '#' && can_shoot_a)
                a.push_back(ii + 4);
            if (next.boardf(next.b_pos + walks[ii]) != '#' && can_shoot_b)
                b.push_back(ii + 4);
        }
        if(len(a) == 0)
        {
            // cerr <<"It's a trap\n";
            a = {8};
        }
        if(len(b) == 0)
        {
            // cerr <<"Hihi haha\n";
            b = {8};
        }
        
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
    auto moves = state.get_not_stupid_moves();
    // cerr << "moves:\n";
    // for(auto m : moves.x)
    //     cerr << m << " ";
    // cerr << "\n";
    // for(auto m : moves.y)
    //     cerr << m << " ";
    // cerr << "\n";
    return state.get_random_not_stupid_move().x;
}

int main()
{
    start_time = microseconds();
    ran = mt19937(start_time);
    game.read_board();
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