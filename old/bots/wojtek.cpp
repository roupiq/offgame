#include <bits/stdc++.h>
using namespace std;

#define len(x) ((int) x.size())
#define pii pair<int, int>
#define pid pair<int, double>

struct Bullet
{
    pii pos;
    int dir;
};

pii operator+(pii a, pii b)
{
    return pii{a.first + b.first, a.second + b.second};
}

string input_line;
int n, m;
int round_num;
array<array<char, 20>, 15> board;
vector<Bullet> bullets;
int max_depth = 3;
int depth = 0;
mt19937 ran;

bool pos_has_bullet(pii pos)
{
    for (int i = 0; i < len(bullets); i++)
    {
        if (bullets[i].pos == pos) return true;
    }

    return false;
}

bool p_killed;
bool e_killed;
Bullet p_bullet, e_bullet;

array<pii, 9> walks = {pii{-1, 0}, pii{1, 0}, pii{0, -1}, pii{0, 1}, 
pii{0, 0}, pii{0, 0}, pii{0, 0}, pii{0, 0}, pii{0, 0}};

array<array<bool, 20>, 15> visited;

char boardf(pii pos)
{
    return board[pos.first][pos.second];
}

bool visitedf(pii pos)
{
    return visited[pos.first][pos.second];
}

double dist(pii p_pos, pii e_pos)
{
    for (int i = 0; i < 15; i++)
    {
        for (int j = 0; j < 20; j++)
        {
            visited[i][j] = false;
        }
    }

    queue<Bullet> line;
    line.push(Bullet{p_pos, 0});

    while (!line.empty())
    {
        Bullet path = line.front();
        line.pop();

        if (path.pos == e_pos) return path.dir;

        for (int i = 0; i < 4; i++)
        {
            if (visitedf(path.pos + walks[i]) || 
            boardf(path.pos + walks[i]) == '#') continue;

            visited[path.pos.first + walks[i].first]
            [path.pos.second + walks[i].second] = true;

            line.push(Bullet{path.pos + walks[i], path.dir + 1});
        }
    }

    return 0;
}

int manhat(pii posa, pii posb)
{
    return abs(posa.first - posb.first) + abs(posa.second - posb.second);
}

double get_score(pii p_pos, pii e_pos)
{
    double answer = 0;
    if (depth == 2)
    {
        answer -= dist(p_pos, e_pos);
    }
    answer += (double) (ran() % 11 - 5) / 1000000;

    /*for (int i = 0; i < len(bullets); i++)
    {
        if (manhat(bullets[i].pos, p_pos) < 
        manhat(bullets[i].pos + walks[bullets[i].dir], p_pos))
        {
            answer -= 1/(manhat(bullets[i].pos, p_pos));
        }
    }

    for (int i = 0; i < len(bullets); i++)
    {
        if (manhat(bullets[i].pos, e_pos) < 
        manhat(bullets[i].pos + walks[bullets[i].dir], e_pos))
        {
            answer += 1/(manhat(bullets[i].pos, e_pos));
        }
    }*/

    return answer;
}

pid dfs(pii p_pos, pii e_pos)
{
    array<array<double, 9>, 9> scores;
    array<double, 9> p_scores;
    double score = 0;
    int best_move_p;
    depth++;

    p_killed = pos_has_bullet(p_pos);
    e_killed = pos_has_bullet(e_pos);

    /*if (depth == 2)
    {
        cout << "p_pos: " << p_pos.first << " " << p_pos.second << "\n";

        for (int i = 0; i < len(bullets); i++)
        {
            cout << bullets[i].pos.first << " " << bullets[i].pos.second << "\n";
        }
    }*/

    if (p_killed || e_killed)
    {
        if (p_killed) score -= 2000'000'000;
        if (e_killed) score += 1000'000'000;
        depth--;
        return pid{8, score};
    }

    if (depth > max_depth)
    {
        depth--;
        return pid{8, score};
    }

    for (int move_p = 0; move_p < 9; move_p++)
    {
        pii np_pos = p_pos + walks[move_p];
        p_scores[move_p] = -4000'000'000;
        if (boardf(np_pos) == '#') continue;

        if (4 <= move_p && move_p < 8)
        {
            if (boardf(p_pos + walks[move_p - 4]) == '#') continue;
            p_bullet.pos = p_pos + walks[move_p - 4];
            p_bullet.dir = move_p - 4;
        }
        /*if (depth == 1)
        {
            cout << "move_p: " << move_p << "\n";
        }*/

        for (int move_e = 0; move_e < 9; move_e++)
        {
            np_pos = p_pos + walks[move_p];
            scores[move_p][move_e] = 4000'000'000;
            pii ne_pos = e_pos + walks[move_e];
            if (boardf(ne_pos) == '#') continue;

            if (4 <= move_e && move_e < 8)
            {
                if (boardf(e_pos + walks[move_e - 4]) == '#') continue;
                e_bullet.pos = e_pos + walks[move_e - 4];
                e_bullet.dir = move_e - 4;
            }

            if (np_pos == ne_pos)
            {
                np_pos = p_pos;
                ne_pos = e_pos;
            }

            for (int i = 0; i < len(bullets); i++)
            {
                if (boardf(bullets[i].pos + walks[bullets[i].dir]) == '#')
                {
                    bullets[i].dir ^= 1;
                }
                else
                {
                    bullets[i].pos = bullets[i].pos + walks[bullets[i].dir];
                }
            }

            if (4 <= move_p && move_p < 8)
            {
                bullets.push_back(p_bullet);
            }
            if (4 <= move_e && move_e < 8)
            {
                bullets.push_back(e_bullet);
            }

            /*if (depth == 1)
            {
                cout << "np_pos: " << np_pos.first << " " << np_pos.second << "\n";
            }*/
            scores[move_p][move_e] = dfs(np_pos, ne_pos).second;

            if (4 <= move_p && move_p < 8)
            {
                bullets.pop_back();
            }
            if (4 <= move_e && move_e < 8)
            {
                bullets.pop_back();
            }

            for (int i = 0; i < len(bullets); i++)
            {
                if (boardf(bullets[i].pos + walks[bullets[i].dir ^ 1]) == '#')
                {
                    bullets[i].dir ^= 1;
                }
                else
                {
                    bullets[i].pos = bullets[i].pos + walks[bullets[i].dir ^ 1];
                }
            }
        }

        p_scores[move_p] = 4000'000'000;

        for (int move_e = 0; move_e < 9; move_e++)
        {
            /*if (depth == 1)
            {
                cout<<"move_e, score for e: "<<move_e<<" "<<scores[move_p][move_e]<<"\n";
            }*/
            p_scores[move_p] = min(p_scores[move_p], scores[move_p][move_e]);
        }
    }

    double max_p_score = -4000'000'000;

    for (int move_p = 0; move_p < 9; move_p++)
    {
        /*if (depth == 1)
        {
            cout << "move_p, score for p: " << move_p << " " << p_scores[move_p] << "\n";
        }*/
        if (max_p_score < p_scores[move_p])
        {
            max_p_score = p_scores[move_p];
            best_move_p = move_p;
        }
    }

    score = max_p_score * 1.5 + get_score(p_pos, e_pos);
    depth--;
    return pid{best_move_p, score};
}

int main()
{
    uint64_t microseconds_since_epoch = 
    std::chrono::duration_cast<std::chrono::microseconds>
    (std::chrono::system_clock::now().time_since_epoch()).count();
    mt19937 ran1(microseconds_since_epoch);
    ran = ran1;
    cin >> n >> m;
    char c;
    char P;
    pii r_pos;
    pii b_pos;

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

    /*for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            cout << board[i][j];
        }
        cout << "\n";
    }*/

    cin >> round_num;
    cin >> P;

    if (P == 'B')
    {
        swap(r_pos, b_pos);
    }

    cout << dfs(r_pos, b_pos).first << "\n";
}

/*
6 5
#  #  #  #  #
#           #
#  R  #     #
#     #     #
#    <   B  #
#  #  #  #  #
2
R

*/