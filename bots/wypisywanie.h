#pragma once
#include <bits/stdc++.h>
using namespace std;

/*
Wypisywanie różnych typów
*/

template <typename T1, typename T2>
ostream &operator<<(ostream &, const pair<T1, T2> &);
template <typename T1, typename T2>
ostream &operator<<(ostream &, const map<T1, T2> &);
template <typename T1, typename T2>
ostream &operator<<(ostream &, const unordered_map<T1, T2> &);
template <typename T>
ostream &operator<<(ostream &, const vector<T> &);
template <typename T>
ostream &operator<<(ostream &, const set<T> &);
template <typename T>
ostream &operator<<(ostream &, const unordered_set<T> &);
template <typename T>
ostream &operator<<(ostream &, const deque<T> &);

template <typename T>
ostream &operator<<(ostream &, const priority_queue<T> &);
template <typename T>
ostream &operator<<(ostream &, const queue<T> &);
template <typename T>
ostream &operator<<(ostream &, const __int128 &);

template <typename T>
ostream &print_it(ostream &o, const T &p, string klamerki)
{
    o << klamerki[0];
    for (int i{}; auto el : p)
        o << (i++ ? "," : "") << el;
    return o << klamerki[1];
}

template <typename T1, typename T2>
ostream &operator<<(ostream &o, const map<T1, T2> &p) { return print_it(o, p, "{}"); }
template <typename T1, typename T2>
ostream &operator<<(ostream &o, const unordered_map<T1, T2> &p) { return print_it(o, p, "{}"); };
template <typename T>
ostream &operator<<(ostream &o, const vector<T> &p) { return print_it(o, p, "[]"); }
template <typename T>
ostream &operator<<(ostream &o, const deque<T> &p) { return print_it(o, p, "[]"); }
template <typename T>
ostream &operator<<(ostream &o, const set<T> &p) { return print_it(o, p, "{}"); }
template <typename T>
ostream &operator<<(ostream &o, const unordered_set<T> &p) { return print_it(o, p, "{}"); }

template <typename T1, typename T2>
ostream &operator<<(ostream &o, const pair<T1, T2> &p)
{
    return o << "(" << p.first << ", " << p.second << ")";
}

template <typename T>
ostream &operator<<(ostream &o, const priority_queue<T> &p)
{
    auto p_copy = p;
    o << '{';
    for (int i{}; p_copy.size(); p_copy.pop())
        o << (i++ ? "," : "") << p_copy.top();
    return o << '}';
}
template <typename T>
ostream &operator<<(ostream &o, const queue<T> &p)
{
    auto p_copy = p;
    o << '{';
    for (int i{}; p_copy.size(); p_copy.pop())
        o << (i++ ? "," : "") << p_copy.front();
    return o << '}';
}

static void out128(ostream &out, __uint128_t val, int neg)
{
    // Note if the number is zero. (No hex or octal prefix in this case.)
    auto zero = val == 0;

    // Note if upper-case letters requested.
    auto state = out.flags();
    auto upper = (state & ios_base::uppercase) != 0;

    // Set base for digits.
    unsigned base = state & ios_base::hex ? 16 : state & ios_base::oct ? 8
                                                                       : 10;

    // Space for digits and prefix. Generate digits starting at the end of the
    // string, going backwards. num will be the digit string. Terminate it.
    char str[47];
    auto end = str + sizeof(str), num = end;
    *--num = 0;

    // Compute and place digits in base base.
    do
    {
        char dig = char(val % base);
        val /= base;
        dig = char(dig + (dig < 10 ? '0' : (upper ? 'A' : 'a') - 10));
        *--num = dig;
    } while (val);

    // Prepend octal number with a zero if requested.
    if (state & ios_base::showbase && base == 8 && !zero)
        *--num = '0';

    // pre will be the prefix string. Terminate it.
    auto pre = num;
    *--pre = 0;

    // Put a plus or minus sign in the prefix as appropriate.
    if (base == 10)
    {
        if (neg)
            *--pre = '-';
        else if (state & ios_base::showpos)
            *--pre = '+';
    }

    // Prefix a hexadecimal number if requested.
    else if (state & ios_base::showbase && base == 16 && !zero)
    {
        *--pre = upper ? 'X' : 'x';
        *--pre = '0';
    }

    // Compute the number of pad characters and get the fill character.
    auto len = (num - pre) + (end - num) - 2;
    auto pad = out.width();
    out.width(0);
    pad = pad > len ? pad - len : 0;
    char fill = out.fill();

    // Put the padding before prefix if neither left nor internal requested.
    if (!(state & (ios_base::internal | ios_base::left)))
        while (pad)
        {
            out << fill;
            pad--;
        }

    // Write prefix.
    out << pre;

    // Put the padding between the prefix and the digits if requested.
    if (state & ios_base::internal)
        while (pad)
        {
            out << fill;
            pad--;
        }

    // Write digits.
    out << num;

    // Put number to the left of padding, if requested.
    if (state & ios_base::left)
        while (pad)
        {
            out << fill;
            pad--;
        }
}

// Overload << for an unsigned 128-bit integer.
ostream &operator<<(ostream &out, __uint128_t val)
{
    out128(out, val, 0);
    return out;
}

// Overload << for a signed 128-bit integer. Negation of the most negative
// signed value gives the correct unsigned absolute value.
ostream &operator<<(ostream &out, __int128_t val)
{
    auto state = out.flags();
    if (val < 0 && !(state & (ios_base::hex | ios_base::oct)))
        out128(out, -(__uint128_t)val, 1);
    else
        out128(out, (__uint128_t)val, 0);
    return out;
}