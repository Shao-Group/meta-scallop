#include "essential.h"
#include "bridge_solver.h"
#include "util.h"

#include <algorithm>

int entry::print()
{
	printf("entry: length = %d, trace = (%d, %d), stack = (", length, trace1, trace2);
	printv(stack);
	printf(")\n");
	return 0;
}

bool entry_compare(const entry &x, const entry &y)
{
	for(int i = 0; i < x.stack.size() && i < y.stack.size(); i++)
	{
		if(x.stack[i] > y.stack[i]) return true;
		if(x.stack[i] < y.stack[i]) return false;
	}
	if(x.length < y.length) return true;
	else return false;
}

bridge_solver::bridge_solver(splice_graph &g, vector<pereads_cluster> &v, const parameters &c, int32_t low, int32_t high)
	: gr(g), vc(v), cfg(c)
{
	length_low = low;
	length_high = high;
	build_bridging_vertices();
	build_piers();
	nominate();
	vote();
}

int bridge_solver::build_bridging_vertices()
{
	vpairs.clear();
	int n = gr.num_vertices() - 1;
	for(int i = 0; i < vc.size(); i++)
	{
		const pereads_cluster &pc = vc[i];
		int v1 = gr.locate_vertex(pc.bounds[1] - 1);
		int v2 = gr.locate_vertex(pc.bounds[2] - 0);

		/*
		if(check_left_relaxing(pc, v1)) v1--;
		if(check_right_relaxing(pc, v2)) v2++;
		*/

		vpairs.push_back(PI(v1, v2));
	}
	return 0;
}

bool bridge_solver::check_left_relaxing(const pereads_cluster &pc, int v)
{
	int n = gr.num_vertices() - 1;
	if(v <= 0 || v >= n) return false;
	int32_t p = gr.get_vertex_info(v).lpos;
	assert(p <= pc.bounds[1]);
	if(v <= 1) return false;
	if(check_continuous_vertices(gr, v - 1, v) == false) return false;
	if(pc.bounds[1] - p > 10) return false;
	if(pc.chain1.size() >= 1 && pc.chain1.back() >= p) return false;
	return true;
}

bool bridge_solver::check_right_relaxing(const pereads_cluster &pc, int v)
{
	int n = gr.num_vertices() - 1;
	if(v <= 0 || v >= n) return false;
	int32_t p = gr.get_vertex_info(v).rpos;
	assert(p >= pc.bounds[2]);
	if(v >= n - 1) return false;
	if(check_continuous_vertices(gr, v, v + 1) == false) return false;
	if(p - pc.bounds[2] > 10) return false;
	if(pc.chain2.size() >= 1 && pc.chain2.front() <= p) return false;
	return true;
}

int bridge_solver::build_piers()
{
	piers.clear();
	set<PI> ss;
	assert(vc.size() == vpairs.size());
	for(int k = 0; k < vc.size(); k++)
	{
		PI &p = vpairs[k];
		if(p.first < 0) continue;
		if(p.second < 0) continue;
		if(p.first >= p.second) continue;
		if(ss.find(p) != ss.end()) continue;
		ss.insert(p);
		pier pr(p.first, p.second);
		piers.push_back(pr);
	}
	return 0;
}

int bridge_solver::build_piers_index()
{
	pindex.clear();
	for(int k = 0; k < piers.size(); k++)
	{
		PI p(piers[k].bs, piers[k].bt);
		pindex.insert(pair<PI, int>(p, k));
	}
	return 0;
}

int bridge_solver::nominate()
{
	if(piers.size() <= 0) return 0;

	sort(piers.begin(), piers.end());

	vector<int> bounds;
	bounds.push_back(0);
	for(int i = 1; i < piers.size(); i++)
	{
		if(piers[i].bs != piers[i - 1].bs)
		{
			bounds.push_back(i - 1);
			bounds.push_back(i - 0);
		}
	}
	bounds.push_back(piers.size() - 1);

	vector< vector<entry> > table;
	table.resize(gr.num_vertices());
	for(int k = 0; k < bounds.size() / 2; k++)
	{
		int b1 = bounds[k * 2 + 0];
		int b2 = bounds[k * 2 + 1];
		assert(piers[b1].bs == piers[b2].bs);
		int k1 = piers[b2].bs;
		int k2 = piers[b2].bt;

		dynamic_programming(k1, k2, table);

		for(int b = b1; b <= b2; b++)
		{

			int bt = piers[b].bt;
			vector< vector<int> > pb = trace_back(bt, table);

			for(int j = 0; j < pb.size(); j++)
			{
				bridge_path p;
				p.score = table[bt][j].stack.front();
				p.stack = table[bt][j].stack;
				p.v = pb[j];
				build_intron_coordinates_from_path(gr, p.v, p.chain);
				piers[b].bridges.push_back(p);
			}

			sort(piers[b].bridges.begin(), piers[b].bridges.end(), compare_bridge_path_stack);

			/*
			printf("bridges for pier %d: (%d, %d)\n", b, piers[b].bs, piers[b].bt);
			for(int i = 0; i < piers[b].bridges.size(); i++)
			{
				piers[b].bridges[i].print(i);
			}
			*/
		}
	}
	return 0;
}

int bridge_solver::vote()
{
	build_piers_index();
	opt.resize(vc.size());
	for(int i = 0; i < vc.size(); i++)
	{
		vote(i, opt[i]);
	}
	return 0;
}

int bridge_solver::vote(int r, bridge_path &bbp)
{
	bbp.type = -1;
	int ss = vpairs[r].first;
	int tt = vpairs[r].second;
	if(ss < 0 || tt < 0) return 0;

	const pereads_cluster &pc = vc[r];

	// construct candidate bridging paths
	int type = 0;
	vector< vector<int32_t> > chains;
	vector< vector<int32_t> > wholes;
	vector<int> scores;
	if(ss >= tt)
	{
		vector<int32_t> c;
		vector<int32_t> w;
		bool b = merge_intron_chains(pc.chain1, pc.chain2, w);
		assert(b == true);
		type = 1;
		chains.push_back(c);
		wholes.push_back(w);
		scores.push_back(10);
	}
	else if(pindex.find(PI(ss, tt)) != pindex.end())
	{
		type = 2;
		if(pc.chain1.size() >= 1 && pc.chain2.size() >= 1) assert(pc.chain1.back() < pc.chain2.front());
		int k = pindex[PI(ss, tt)];
		vector<bridge_path> &pb = piers[k].bridges;
		for(int e = 0; e < pb.size(); e++)
		{
			vector<int32_t> w = pc.chain1;
			w.insert(w.end(), pb[e].chain.begin(), pb[e].chain.end());
			w.insert(w.end(), pc.chain2.begin(), pc.chain2.end());
			wholes.push_back(w);
			chains.push_back(pb[e].chain);
			scores.push_back(pb[e].score);
		}
	}

	assert(wholes.size() == chains.size());
	if(chains.size() == 0) return 0;

	int be = -1;
	for(int e = 0; e < chains.size(); e++)
	{
		assert(check_increasing_sequence<int32_t>(wholes[e]));
		assert(check_increasing_sequence<int32_t>(chains[e]));
		if(wholes[e].size() >= 1) assert(wholes[e].front() > pc.bounds[0]);
		if(wholes[e].size() >= 1) assert(wholes[e].back() < pc.bounds[3]);
		if(pc.chain1.size() > 0 && chains[e].size() > 0) assert(pc.chain1.back() < chains[e].front());
		if(pc.chain2.size() > 0 && chains[e].size() > 0) assert(pc.chain2.front() > chains[e].back());

		int32_t intron = get_total_length_of_introns(wholes[e]);
		int32_t length = pc.bounds[3] - pc.bounds[0] - intron;
		if(length < length_low) continue;
		if(length > length_high) continue;
		be = e;
		break;
	}

	if(be < 0) return 0;

	bbp.type = type;
	bbp.score = scores[be];
	bbp.chain = chains[be];
	bbp.whole = wholes[be];

	return 0;
}

int bridge_solver::collect_unbridged_clusters(vector<pereads_cluster> &v)
{
	v.clear();
	for(int i = 0; i < opt.size(); i++)
	{
		if(opt[i].type >= 0) continue;
		v.push_back(std::move(vc[i]));
	}
	return 0;
}

int bridge_solver::build_phase_set(phase_set &ps)
{
	assert(opt.size() == vc.size());
	for(int i = 0; i < vc.size(); i++)
	{
		if(opt[i].type >= 0) add_phases_from_bridged_pereads_cluster(vc[i], opt[i], ps);
		else add_phases_from_unbridged_pereads_cluster(vc[i], ps);
	}
	return 0;
}

int add_phases_from_bridged_pereads_cluster(const pereads_cluster &pc, const bridge_path &bbp, phase_set &ps)
{
	assert(bbp.type >= 0);
	int32_t p0 = pc.extend[0];
	int32_t p3 = pc.extend[3];
	vector<int32_t> v;
	v.push_back(p0);
	v.insert(v.end(), bbp.whole.begin(), bbp.whole.end());
	v.push_back(p3);
	assert(check_increasing_sequence<int32_t>(v));
	ps.add(v, pc.count);
	return 0;
}

		
int add_phases_from_unbridged_pereads_cluster(const pereads_cluster &pc, phase_set &ps)
{
	int32_t p0 = pc.extend[0];
	int32_t p1 = pc.extend[1];
	int32_t p2 = pc.extend[2];
	int32_t p3 = pc.extend[3];

	vector<int32_t> v1;
	v1.push_back(p0);
	v1.insert(v1.end(), pc.chain1.begin(), pc.chain1.end());
	v1.push_back(p1);
	assert(check_increasing_sequence<int32_t>(v1));
	ps.add(v1, pc.count);

	vector<int32_t> v2;
	v2.push_back(p2);
	v2.insert(v2.end(), pc.chain2.begin(), pc.chain2.end());
	v2.push_back(p3);
	assert(check_increasing_sequence<int32_t>(v2));
	ps.add(v2, pc.count);
	return 0;
}

int add_phases_from_bridge_path(const pereads_cluster &pc, const bridge_path &bbp, phase_set &ps)
{
	int32_t p0 = pc.extend[0];
	int32_t p3 = pc.extend[3];

	if(bbp.type >= 0)
	{
		vector<int32_t> v;
		v.push_back(p0);
		v.insert(v.end(), pc.chain1.begin(), pc.chain1.end());
		v.insert(v.end(), bbp.chain.begin(), bbp.chain.end());
		v.insert(v.end(), pc.chain2.begin(), pc.chain2.end());
		v.push_back(p3);
		ps.add(v, pc.count);
	}
	else
	{
		int32_t p1 = pc.extend[1];
		int32_t p2 = pc.extend[2];

		vector<int32_t> v1;
		v1.push_back(p0);
		v1.insert(v1.end(), pc.chain1.begin(), pc.chain1.end());
		v1.push_back(p1);
		ps.add(v1, pc.count);

		vector<int32_t> v2;
		v2.push_back(p2);
		v2.insert(v2.end(), pc.chain2.begin(), pc.chain2.end());
		v2.push_back(p3);

		ps.add(v2, pc.count);
	}
	return 0;
}

int bridge_solver::dynamic_programming(int k1, int k2, vector< vector<entry> > &table)
{
	int n = gr.num_vertices();
	assert(k1 >= 0 && k1 < n);
	assert(k2 >= 0 && k2 < n);

	table.clear();
	table.resize(n);

	table[k1].resize(1);
	table[k1][0].stack.assign(cfg.bridge_dp_stack_size, 999999);
	table[k1][0].length = gr.get_vertex_info(k1).rpos - gr.get_vertex_info(k1).lpos;
	table[k1][0].trace1 = -1;
	table[k1][0].trace2 = -1;

	for(int k = k1 + 1; k <= k2; k++)
	{
		vector<entry> v;
		int32_t len = gr.get_vertex_info(k).rpos - gr.get_vertex_info(k).lpos;
		PEEI pi = gr.in_edges(k);
		for(edge_iterator it = pi.first; it != pi.second; it++)
		{
			edge_descriptor e = (*it);
			int j = e->source();
			int w = (int)(gr.get_edge_weight(e));
			if(j < k1) continue;
			if(table[j].size() == 0) continue;

			for(int i = 0; i < table[j].size(); i++)
			{
				entry e;
				e.stack = update_stack(table[j][i].stack, w);
				e.length = table[j][i].length + len;
				e.trace1 = j;
				e.trace2 = i;
				v.push_back(e);
			}
		}

		sort(v.begin(), v.end(), entry_compare);
		if(v.size() > cfg.bridge_dp_solution_size) v.resize(cfg.bridge_dp_solution_size);
		table[k] = v;
	}
	return 0;
}

vector<int> bridge_solver::update_stack(const vector<int> &v, int s)
{
	vector<int> stack(v.size(), 0);
	for(int i = 0, j = 0; i < v.size() && j < v.size(); i++, j++)
	{
		if(i == j && v[i] > s)
		{
			stack[j] = s;
			j++;
		}
		stack[j] = v[i];
	}
	return stack;
}

vector< vector<int> > bridge_solver::trace_back(int k, const vector< vector<entry> > &table)
{
	vector< vector<int> > vv;
	for(int i = 0; i < table[k].size(); i++)
	{
		vector<int> v;
		int p = k;
		int q = i;
		while(true)
		{
			v.push_back(p);
			const entry &e = table[p][q];
			p = e.trace1;
			q = e.trace2;
			if(p < 0) break;
		}
		reverse(v);
		vv.push_back(v);
	}
	return vv;
}

int bridge_solver::print()
{
	assert(vc.size() == opt.size());
	int total_reads = 0;
	int bridged_reads = 0;
	int bridged_clusters = 0;
	for(int i = 0; i < vc.size(); i++)
	{
		total_reads += vc[i].count;
		if(opt[i].type < 0) continue;
		bridged_reads += vc[i].count;
		bridged_clusters++;
	}

	printf("bridge_solver: clusters %d / %lu, reads %d / %d, low = %d, high = %d\n", bridged_clusters, vc.size(), bridged_reads, total_reads, length_low, length_high);
	return 0;
}
