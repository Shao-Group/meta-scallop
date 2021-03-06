/*
Part of aletsch
(c) 2020 by Mingfu Shao, The Pennsylvania State University
Part of Scallop Transcript Assembler
(c) 2017 by  Mingfu Shao, Carl Kingsford, and Carnegie Mellon University.
See LICENSE for licensing.
*/

#include "util.h"
#include <algorithm>

vector<int> get_random_permutation(int n)
{
	vector<int> v;
	for(int i = 0; i < n; i++) v.push_back(i);
	for(int i = 0; i < n; i++)
	{
		int k = rand() % (n - i);
		int x = v[k];
		v[k] = v[n - i - 1];
		v[n - i - 1] = x;
	}
	return v;
}


size_t string_hash(const std::string& str)
{
	size_t hash = 1315423911;
	for(std::size_t i = 0; i < str.length(); i++)
	{
		hash ^= ((hash << 5) + str[i] + (hash >> 2));
	}

	return (hash & 0x7FFFFFFF);
}

size_t vector_hash(const vector<int32_t> & vec) 
{
	size_t seed = vec.size();
	for(int i = 0; i < vec.size(); i++)
	{
		seed ^= (size_t)(vec[i]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}
	return (seed & 0x7FFFFFFF);
}

vector<string> split_string(const string& str, const string& delim)
{
    vector<string> tokens;
    size_t prev = 0, pos = 0;
    do
    {
        pos = str.find(delim, prev);
        if (pos == string::npos) pos = str.length();
        string token = str.substr(prev, pos-prev);
        if (!token.empty()) tokens.push_back(token);
        prev = pos + delim.length();
    }
    while (pos < str.length() && prev < str.length());
    return tokens;
}

int transform_vertex_set_map(const set<int> &s, map<int, int> &m)
{
	m.clear();
	if(s.size() <= 0) return 0;

	vector<int> vv(s.begin(), s.end());
	sort(vv.begin(), vv.end());
	for(int i = 0; i < vv.size(); i++)
	{
		m.insert(pair<int, int>(vv[i], i + 1));
	}
	return 0;
}

vector<int> project_vector(const vector<int> &v, const map<int, int> &a2b)
{
	vector<int> vv;
	for(int k = 0; k < v.size(); k++)
	{
		map<int, int>::const_iterator it = a2b.find(v[k]);
		if(it == a2b.end()) break;
		vv.push_back(it->second);
	}
	return vv;
}

bool check_identical(const vector<int> &x, int x1, int x2, const vector<int> &y, int y1, int y2)
{
	assert(x1 >= 0 && x1 < x.size());
	assert(x2 >= 0 && x2 < x.size());
	assert(y1 >= 0 && y1 < y.size());
	assert(y2 >= 0 && y2 < y.size());

	if(x[x1] != y[y1]) return false;
	if(x[x2] != y[y2]) return false;
	if(x2 - x1 != y2 - y1) return false;

	for(int kx = x1, ky = y1; kx <= x2 && ky <= y2; kx++, ky++)
	{
		if(x[kx] != y[ky]) return false;
	}

	return true;
}
