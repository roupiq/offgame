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

struct Segment
{
    int length;
    vector<int> r;
    Segment(int _length = 0) : length(_length), r(_length * 2) {}

    void add_bullet(int offset, bool left, int t, int v = 1)
    {
        int x = offset, y = len(r) - offset - 1;
        x = (x + t) % len(r);
        y = (y + t) % len(r);
        if (left)
            r[y] += v;
        else
            r[x] += v;
    }

    bool has_bullet(int offset, int time) const
    {
        int x = offset, y = len(r) - offset - 1;
        x = (x + time) % len(r);
        y = (y + time) % len(r);
        return r[x] || r[y];
    }

    void print(int time) const
    {
        for (int i = 0; i < length; i++)
            cout << has_bullet(i, time) << " ";
        cout << "\n";
    }
};

struct Board
{
    static inline array<array<int, 20>, 15> row_offset;
    static inline array<array<int, 20>, 15> col_offset;
    static inline array<array<Segment *, 20>, 15> row_segment;
    static inline array<array<Segment *, 20>, 15> col_segment;
    static inline vector<Segment> segments;
    static inline int round = 0;
    vector<pair<int, pair<pii, int>>> changes;

    bool has_bullet(pii pos, int time = round) const
    {
        if (row_offset[pos.x][pos.y] == -1)
            return 0;
        return (*row_segment[pos.x][pos.y]).has_bullet(row_offset[pos.x][pos.y], time) ||
               (*col_segment[pos.x][pos.y]).has_bullet(col_offset[pos.x][pos.y], time);
    }
    void add_bullet(pii pos, int dir, int d = 1, int t = round)
    {
        // cout << d << " " << t << "\n";

        if (d == 1)
        {
            // cout << pos << " " << row_offset[pos.x][pos.y] << " '" << board[pos.x][pos.y] << "'\n";
            changes.push_back({t, {pos, dir}});
        }
        if (dir >= 2)
            (*row_segment[pos.x][pos.y]).add_bullet(row_offset[pos.x][pos.y], dir % 2, t, d);
        else
            (*col_segment[pos.x][pos.y]).add_bullet(col_offset[pos.x][pos.y], dir % 2, t, d);
    }
    void next()
    {
        round++;
    }

    int in_round = 0;
    Board() { in_round = round; }
    ~Board()
    {
        for (auto [t, b] : changes)
            add_bullet(b.x, b.y, -1, t);
        round = in_round;
    }
    Board(const array<array<char, 20>, 15> &_board)
    {
        cout << n << " " << m << "\n";
        int t;
        for (int i = 0; i < n; i++)
        {
            row_offset[i][0] = -1;
            for (int j = 1; j < m; j++)
            {
                if (_board[i][j] != '#')
                {
                    row_offset[i][j] = row_offset[i][j - 1] + 1;
                    t += !row_offset[i][j];
                }
                else
                    row_offset[i][j] = -1;
            }
        }
        for (int j = 0; j < m; j++)
        {
            col_offset[0][j] = -1;
            for (int i = 1; i < n; i++)
            {
                if (_board[i][j] != '#')
                {
                    col_offset[i][j] = col_offset[i - 1][j] + 1;
                    t += !col_offset[i][j];
                }
                else
                    col_offset[i][j] = -1;
            }
        }
        segments.resize(t);
        for (int i = 1; i < n; i++)
        {
            for (int j = 1; j < m; j++)
            {
                if (_board[i][j] == '#')
                {
                    if (_board[i][j - 1] != '#')
                    {
                        // cerr << row_offset[i][j - 1] + 1 << "\n";
                        segments[--t] = Segment(row_offset[i][j - 1] + 1);
                        for (int k = 0; k < row_offset[i][j - 1] + 1; k++)
                            row_segment[i][j - 1 - k] = &segments[t];
                    }
                    if (_board[i - 1][j] != '#')
                    {
                        // cerr << col_offset[i - 1][j] + 1 << "\n";
                        segments[--t] = Segment(col_offset[i - 1][j] + 1);
                        for (int k = 0; k < col_offset[i - 1][j] + 1; k++)
                            col_segment[i - 1 - k][j] = &segments[t];
                    }
                }
            }
        }
    }

    void print(int t = round) const
    {
        for (int i = 0; i < n; i++)
        {
            for (int j = 0; j < m; j++)
            {
                if (row_offset[i][j] == -1)
                    cerr << "[]";
                else
                    cerr << (has_bullet({i, j}, t) ? "<>" : "  ");
            }
            cerr << "\n";
        }
        cerr << "\n";
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
    for (int i = 0; i < len(matrix); i++)
    {
        double eval = 0.0;
        for (int j = 0; j < len(matrix[0]); j++)
        {
            eval += matrix[i][j] / 2 + 0.5;
        }
        res.push_back(eval / len(matrix[0]));
    }
    return {res, 0.0};
}

struct GameState
{
    int round_num;
    bool r_killed = false, b_killed = false;

    Board fastBoard;
    array<bitset<20>, 15> has_bullet;
    vector<Bullet> bullets;
    pii r_pos, b_pos;
    // bool pos_has_bullet(pii pos) const;

    GameState(const GameState &state)
    {
        has_bullet = state.has_bullet;
        bullets = state.bullets;
        r_pos = state.r_pos;
        b_pos = state.b_pos;
        r_killed = state.r_killed;
        b_killed = state.b_killed;
        round_num = state.round_num;
    }

    GameState()
    {
        for (auto &h : has_bullet)
            h = 0;
    }

    bool pos_has_bullet2(pii pos) const
    {
        return has_bullet[pos.x][pos.y];
    }

    bool pos_has_bullet(pii pos) const
    {
        if (fastBoard.has_bullet(pos) != (pos_has_bullet2(pos)))
        {
            cout << pos << " " << fastBoard.has_bullet(pos) << " " << (pos_has_bullet2(pos)) << "\n";

            for (int x = 0; x < n; x++)
            {
                for (int y = 0; y < m; y++)
                    cerr << (fastBoard.has_bullet({x, y}) ^ pos_has_bullet2({x, y})) << " ";
                cerr << "\n";
            }
            cerr << "\n";

            fastBoard.print();

            for (int x = 0; x < n; x++)
            {
                for (int y = 0; y < m; y++)
                    cerr << (pos_has_bullet2({x, y})) << " ";
                cerr << "\n";
            }
            cerr << "\n";

            exit(1);
        }
        return pos_has_bullet2(pos);
    }

    void print_has_bullet()
    {
        for (int x = 0; x < n; x++)
        {
            for (int y = 0; y < m; y++)
                cerr << pos_has_bullet({x, y}) << " ";
            cerr << "\n";
        }
        cerr << "\n";
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
        
        fastBoard.add_bullet(bullet.pos, bullet.dir);
    }

    void move_bullets()
    {
        round_num++;
        fastBoard.next();
        for (auto &r : has_bullet)
            r = 0;
        for (int i = 0; i < len(bullets); i++)
            move_bullet(i);

        for (int i = 0; i < n; i++)
        {
            for (int j = 0; j < m; j++)
            {
                pos_has_bullet({i, j});
            }
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
        GameState check = *this;
        double keval = kmonte_carlo_eval(r, d);
        if (abs(keval) > 0.5)
            return keval;
        if (depth == 0)
        {
            return keval;
        }

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
                // cout << pair{i, j} << " i j\n";
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
        vector<Bullet> initial_bullets;
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
                    initial_bullets.push_back(Bullet{pii{i, j / 4}, 0});
                else if (c == 'v')
                    initial_bullets.push_back(Bullet{pii{i, j / 4}, 1});
                else if (c == '<')
                    initial_bullets.push_back(Bullet{pii{i, j / 4}, 2});
                else if (c == '>')
                    initial_bullets.push_back(Bullet{pii{i, j / 4}, 3});
            }
            ignore = scanf("\n");
            o << "\n";
        }
        fastBoard = Board(board);
        for (auto b : initial_bullets)
            add_bullet(b);

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

string input_line;

/*
15 20
#   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   
#              >                       >   >       >                        #   
#    v >       >#           ^    v              ^                           #   
#                        v   v  ^v                                          #   
#   ^     <   <         ^     < ^v        <                 R       #       #   
#      >         v >       > v< ^v<         ^   ^   ^                       #   
#           #      >     v   v >^ <        >        ^ <   <>B    v          #   
#     <             #    v<  v      #   #           ^   #                   #   
#     <      v   v   v   v    <           <   <>  <> v         >#           #   
#    v        <  v     >     v         >       >                       >   >#   
#       #        v                     >               > v   v     >   >  < #   
#     <           <      v    <      v                 > v        <   <     #   
#       ^                v                 >    ^    v      #               #   
#                   ^v   v   v >                          <                 #   
#   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   #   
200


*/