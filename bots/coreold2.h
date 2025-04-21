#include <bits/stdc++.h>
#include "wypisywanie.h"
using namespace std;
#define all(x) x.begin(), x.end()
#define len(x) (int)x.size()
#define x first
#define y second
using ld = long double;
using i128 = __int128_t;
using pii = pair<int, int>;
using pid = pair<int, double>;

long microseconds()
{
    return chrono::duration_cast<chrono::microseconds>(chrono::system_clock::now().time_since_epoch()).count();
}

struct Bullet
{
    pii pos;
    int dir;
};

pii operator+(pii a, pii b) { return {a.first + b.first, a.second + b.second}; }
int manhat(pii a, pii b) { return abs(a.x - b.x) + abs(a.y - b.y); }

const long timeout = 2'000 * 1000;
const int max_round_num = 400;
int max_depth = 400;

const int moves_num = 9;
const map<int, char> move_codes = {{0, 'w'}, {1, 's'}, {2, 'a'}, {3, 'd'}, {4, '^'}, {5, 'v'}, {6, '<'}, {7, '>'}, {8, '_'}};
const array<pii, 9> walks = {pii{-1, 0}, pii{1, 0}, pii{0, -1}, pii{0, 1}, pii{0, 0}, pii{0, 0}, pii{0, 0}, pii{0, 0}, pii{0, 0}};

mt19937 ran;
mt19937 ndran(microseconds());
char player_color = '0';

array<array<char, 20>, 15> board;
char boardf(pii pos) { return board[pos.first][pos.second]; }

int start_round = 0;
int n, m;
vector<array<bitset<20>, 15>> prepro_has_bullet;
int start_bullets_n = 0;
uint64_t start_time;
std::uniform_real_distribution<> dist(0.0, 1.0);

struct Moves
{
    array<double, moves_num> evaluations; // [0, 1]
    Moves()
    {
        evaluations.fill(0);
    }
    double operator[](int i) const
    {
        return evaluations[i];
    }
    double &operator[](int i)
    {
        return evaluations[i];
    }
    int sample() const
    {
        double sum_of_weight = 0;
        for (int i = 0; i < moves_num; i++)
        {
            sum_of_weight += evaluations[i];
        }
        if (sum_of_weight < 1e-9)
            return int((long)ran() % moves_num);
        double rnd = dist(ran) * sum_of_weight;
        // cerr << rnd << " " << sum_of_weight << "\n";
        for (int i = 0; i < moves_num; i++)
        {
            if (rnd < evaluations[i])
                return i;
            rnd -= evaluations[i];
        }
        assert(!"should never get here");
    }
    int maxSample() const
    {
        int res = -1;
        double maxx = -INFINITY;

        for (int i = 0; i < moves_num; i++)
        {
            if (evaluations[i] > maxx)
            {
                maxx = evaluations[i];
                res = i;
            }
        }
        return res;
    }
};

ostream &operator<<(ostream &o, const Moves &t)
{
    for (auto u : t.evaluations)
        o << u << " ";
    o << "\n";
    return o;
}

pair<vector<double>, double> solve_nash(vector<vector<double>> matrix)
{
    vector<double> res;
    vector<double> weight(len(matrix[0]), 1);
    double weight_sum = len(matrix[0]);

    for (int i = 0; i < len(matrix); i++)
    {
        double eval = 0.0;
        for (int j = 0; j < len(matrix[0]); j++)
        {
            double c = matrix[i][j] / 2 + 0.5;
            eval += c * weight[j];
        }
        res.push_back(eval / weight_sum);
    }
    return {res, 0.0};
}

pair<vector<double>, double> solve_nash2(vector<vector<double>> matrix)
{
    vector<double> res;
    for (int i = 0; i < len(matrix); i++)
    {
        double eval = 0.0;
        for (int j = 0; j < len(matrix[0]); j++)
        {
            double c = matrix[i][j] / 2 + 0.5;
            eval += sqrt(c);
        }
        res.push_back(eval / len(matrix[0]));
    }
    return {res, 0.0};
}

struct GameState
{
    int round_num;
    bool r_killed, b_killed;

    array<bitset<20>, 15> has_bullet;
    vector<Bullet> bullets;
    pii r_pos, b_pos;

    GameState()
    {
        for (auto &h : has_bullet)
            h = 0;
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
        return prepro_has_bullet[round_num - start_round][pos.x][pos.y] || has_bullet[pos.x][pos.y];
    }

    void move_bullet(int i)
    {
        Bullet &bullet = bullets[i];
        if (boardf(bullet.pos + walks[bullet.dir]) == '#')
            bullet.dir ^= 1;
        else
            bullet.pos = bullet.pos + walks[bullet.dir];
        has_bullet[bullet.pos.x][bullet.pos.y] = true;
    }
    void add_bullet(Bullet bullet)
    {
        bullets.push_back(bullet);
        has_bullet[bullet.pos.x][bullet.pos.y] = true;
    }
    void move_bullets()
    {
        round_num++;
        for (auto &r : has_bullet)
            r = 0;
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

    pair<Moves, Moves> nonlethal()
    {
        move_bullets();

        bool can_shoot_a = !pos_has_bullet(r_pos),
             can_shoot_b = !pos_has_bullet(b_pos);
        Moves a, b;
        a[8] = can_shoot_a;
        b[8] = can_shoot_b;

        for (int i = 0; i < 4; i++)
        {
            if (boardf(r_pos + walks[i]) != '#')
            {
                a[i] = !pos_has_bullet(r_pos + walks[i]);
                a[i + 4] = can_shoot_a;
            }
            if (boardf(b_pos + walks[i]) != '#')
            {
                b[i] = !pos_has_bullet(b_pos + walks[i]);
                b[i + 4] = can_shoot_b;
            }
        }

        return {a, b};
    }
    pair<int, int> random_nonlethal_move()
    {
        auto [a, b] = nonlethal();
        return {a.sample(), b.sample()};
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
                auto [m1, m2] = monte.random_nonlethal_move();
                monte.move_players(m1, m2);

                if (monte.r_killed)
                    res -= 1;
                if (monte.b_killed)
                    res += 1;

                if (monte.r_killed || monte.b_killed)
                    break;
            }
        }

        return res / r;
    }

    double rec_eval(int r, int d, int depth) const
    {
        if (depth == 0)
        {
            return kmonte_carlo_eval(r, d);
        }
        GameState check = *this;
        auto [nl_r, nl_b] = check.nonlethal();
        vector<int> ok;
        vector<int> ok2;
        for (int i = 0; i < moves_num; i++)
        {
            if (nl_r[i] > 0.9)
                ok.push_back(i);
            if (nl_b[i] > 0.9)
                ok2.push_back(i);
        }
        Moves a, b;
        vector<vector<double>> eval(9, vector<double>(9, -1.0));
        for (int i : ok)
        {
            for (int j : ok2)
            {
                GameState monte = check;
                monte.move_players(i, j);
                double score = monte.rec_eval(r, d, depth - 1);
                eval[i][j] = score;
            }
        }

        auto [e, p] = solve_nash(eval);
        for (int i = 0; i < 9; i++)
            a.evaluations[i] = e[i];

        return a[a.maxSample()];
    }

    pair<Moves, Moves> eval_moves(int r, int d, int depth = 2) const
    {
        GameState check = *this;
        auto [nl_r, nl_b] = check.nonlethal();
        vector<int> ok;
        vector<int> ok2;
        for (int i = 0; i < moves_num; i++)
        {
            if (nl_r[i] > 0.9)
                ok.push_back(i);
            if (nl_b[i] > 0.9)
                ok2.push_back(i);
        }
        Moves a, b;
        vector<vector<double>> eval(9, vector<double>(9, -1));
        for (int i : ok)
        {
            for (int j : ok2)
            {
                GameState monte = check;
                monte.move_players(i, j);
                double score = monte.rec_eval(r, d, depth - 1);
                eval[i][j] = score;
            }
        }

        auto [e, p] = solve_nash(eval);
        for (int i = 0; i < 9; i++)
            a.evaluations[i] = e[i];

        return {a, b};
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
        if (P == 'B')
        {
            swap(b_pos, r_pos);
        }
        else if (round_num > 20 and ran() % 30 == 0)
        {
            ofstream file("current_game/round_" + to_string(round_num) + ".in");
            file << o.str();
        }
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
    start_bullets_n = len(game.bullets);
}

string input_line;