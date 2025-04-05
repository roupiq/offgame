#include <bits/stdc++.h>
using namespace std;
#define all(x) x.begin(), x.end()
#define len(x) (int)x.size()
#define x first
#define y second
using ld = long double;
using i128 = __int128_t;
using pii = pair<int, int>;
using pid = pair<int, double>;
const long timeout = 140 * 1000;
const int max_round_num = 400;
int max_depth = 35;

long microseconds()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct Bullet
{
    pii pos;
    int dir;
};

pii operator+(pii a, pii b) { return {a.first + b.first, a.second + b.second}; }

array<pii, 9> walks = {pii{-1, 0}, pii{1, 0}, pii{0, -1}, pii{0, 1},
                       pii{0, 0}, pii{0, 0}, pii{0, 0}, pii{0, 0}, pii{0, 0}};

int manhat(pii a, pii b) { return abs(a.x - b.x) + abs(a.y - b.y); }

mt19937 ran;
char player_color = '0';

map<int, char> move_codes = {{0, 'w'}, {1, 's'}, {2, 'a'}, {3, 'd'}, {4, '^'}, {5, 'v'}, {6, '<'}, {7, '>'}, {8, '_'}};
array<array<char, 20>, 15> board;
char boardf(pii pos) { return board[pos.first][pos.second]; }

int start_round = 0;
int n, m;
vector<array<array<int, 20>, 15>> prepro_has_bullet;

struct GameState
{
    int round_num;
    bool r_killed, b_killed;

    array<array<int, 20>, 15> has_bullet;
    vector<Bullet> bullets;

    GameState()
    {
        for (auto &h : has_bullet)
            h.fill(0);
    }

    void print_has_bullet()
    {
        for (int x = 0; x < n; x++)
        {
            for (int y = 0; y < m; y++)
                cerr << has_bullet[x][y] << " ";
            cerr << "\n";
        }
        cerr << "\n";
        for (int x = 0; x < n; x++)
        {
            for (int y = 0; y < m; y++)
                cerr << prepro_has_bullet[round_num - start_round][x][y] << " ";
            cerr << "\n";
        }
        cerr << "\n";
        cerr << "\n";
    }

    bool pos_has_bullet(pii pos) const
    {
        // for (int i = 0; i < len(bullets); i++)
        //     if (bullets[i].pos == pos)
        //         return true;
        // return false;
        // cout << round_num << " " << start_round << "\n";
        return prepro_has_bullet[round_num - start_round][pos.x][pos.y] || has_bullet[pos.x][pos.y];
    }

    pii r_pos, b_pos;

    void move_bullet(int i)
    {
        Bullet &bullet = bullets[i];
        has_bullet[bullet.pos.x][bullet.pos.y]--;
        if (boardf(bullet.pos + walks[bullet.dir]) == '#')
            bullet.dir ^= 1;
        else
            bullet.pos = bullet.pos + walks[bullet.dir];
        has_bullet[bullet.pos.x][bullet.pos.y]++;
    }
    void add_bullet(Bullet bullet)
    {
        bullets.push_back(bullet);
        has_bullet[bullet.pos.x][bullet.pos.y]++;
    }
    void move_bullets()
    {
        round_num++;

        for (int i = 0; i < len(bullets); i++)
            move_bullet(i);
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
            add_bullet(b_bullet);
        }
        if (4 <= move_r && move_r < 8)
        {
            r_bullet.pos = r_pos;
            r_bullet.dir = move_r - 4;
            if (boardf(r_bullet.pos + walks[r_bullet.dir]) == '#')
                r_bullet.dir ^= 1;
            else
                r_bullet.pos = r_bullet.pos + walks[r_bullet.dir];
            add_bullet(r_bullet);
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
    }

    GameState next_board(int move_r, int move_b) const
    {
        GameState next = *this;
        next.move_bullets();
        next.move_players(move_r, move_b);
        return next;
    }

    pair<vector<int>, vector<int>> get_nonlethal_moves()
    {
        move_bullets();

        bool can_shoot_a = !pos_has_bullet(r_pos),
             can_shoot_b = !pos_has_bullet(b_pos);
        vector<int> a, b;
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
    pair<int, int> get_random_nonlethat_move()
    {
        auto [a, b] = get_nonlethal_moves();
        return {a[ran() % len(a)], b[ran() % len(b)]};
    }

    void read_board()
    {
        stringstream o;
        cin >> n >> m;
        o << n << " " << m << "\n";
        char c;
        char P;

        ignore = scanf("\n");
        for (int i = 0; i < n; i++)
        {
            for (int j = 0; j < 4 * m; j++)
            {
                ignore = scanf("%c", &c);
                o << c;
                if (c == '#')
                    board[i][j / 4] = '#';
                else if (c == ' ' && board[i][j / 4] == 0)
                    board[i][j / 4] = ' ';
                else if (c == 'R')
                    r_pos = pii{i, j / 4};
                else if (c == 'B')
                    b_pos = pii{i, j / 4};
                else if (c == '^')
                    add_bullet(Bullet{pii{i, j / 4}, 0});
                else if (c == 'v')
                    add_bullet(Bullet{pii{i, j / 4}, 1});
                else if (c == '<')
                    add_bullet(Bullet{pii{i, j / 4}, 2});
                else if (c == '>')
                    add_bullet(Bullet{pii{i, j / 4}, 3});
            }
            ignore = scanf("\n");
            o << "\n";
        }

        cin >> round_num;
        o << round_num << "\n";
        start_round = round_num;

        cin >> P;
        player_color = P;
        mt19937 ran(microseconds());
        if (P == 'B')
        {
            swap(b_pos, r_pos);
        }
        else if (round_num > 20 and ran() % 30 == 0)
        {
            ofstream file("current_game/round_" + to_string(round_num) + "_" + to_string(microseconds() % 1'000'000) + ".in");
            file << o.str();
        }
    }

    double kmonte_carlo_eval(int r, int d) const
    {
        double res = 0;
        if (this->r_killed)
            res -= 1;
        if (this->b_killed)
            res += 1;

        if (this->r_killed || this->b_killed)
            return res;

        for (int k = 0; k < r; k++)
        {
            GameState monte = *this;

            for (int j = 0; j < d && monte.round_num <= max_round_num; j++)
            {
                if (monte.r_killed)
                    res -= 1;
                if (monte.b_killed)
                    res += 1;

                if (monte.r_killed || monte.b_killed)
                    break;

                auto [m1, m2] = monte.get_random_nonlethat_move();
                monte.move_players(m1, m2);
            }
        }

        return res / r;
    }
};

void preprocess_bullets(GameState game)
{
    prepro_has_bullet.resize(min(max_depth, 400 - start_round) + 1);
    for (int j = 0; j < len(prepro_has_bullet); j++)
    {
        prepro_has_bullet[j] = game.has_bullet;
        game.move_bullets();
    }
}

string input_line;

uint64_t start_time;
int get_move(GameState state)
{
    vector<double> move_eval(9, -3.0);

    GameState check = state;
    auto [r_nonlethal, b_nonlethal] = check.get_nonlethal_moves();
    for (auto ns : r_nonlethal)
    {
        move_eval[ns] = 0.0;
    }

    int k = 0;
    for (; k < 1e3; k++)
    {
        // if (start_time + timeout < microseconds())
        // {
        //     // cerr << "3: " << k << "\n";
        //     break;
        // }
        for (auto i : r_nonlethal)
        {
            GameState monte = check;
            int p_move = i;
            int e_move = r_nonlethal[ran() % len(r_nonlethal)];
            monte.move_players(p_move, e_move);
            move_eval[i] += monte.kmonte_carlo_eval(1, 23);


        }
    }
    for (auto &m : move_eval)
        m /= k;

    double opt = -2.0;
    vector<int> move;
    for (auto i : r_nonlethal)
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
        cerr << move_codes[i] << ": " << move_eval[i] << "\n";
    cerr << "\n";
#endif
    return move[ran() % len(move)];
}

int main()
{
    GameState game;

    start_time = microseconds();
    ran = mt19937(10);

    game.read_board();
    preprocess_bullets(game);
    for (auto &b : game.has_bullet)
        b.fill(0);
    game.bullets.clear();

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