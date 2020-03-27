#include "meta_config.h"
#include "combined_graph.h"
#include "config.h"
#include "essential.h"
#include "graph_revise.h"
#include <sstream>
#include <algorithm>

combined_graph::combined_graph()
{
	num_combined = 0;
	strand = '?';
}

int combined_graph::build(splice_graph &gr, hyper_set &hs, vector<PRC> &ub)
{
	chrm = gr.chrm;
	strand = gr.strand;
	num_combined = 1;

	build_regions(gr);
	build_start_bounds(gr);
	build_end_bounds(gr);
	build_splices_junctions(gr);
	build_phase(gr, hs);
	build_reads(gr, ub);
	return 0;
}
	
int combined_graph::build_regions(splice_graph &gr)
{
	regions.clear();
	int n = gr.num_vertices() - 1;
	for(int i = 1; i < n; i++)
	{
		double weight = gr.get_vertex_weight(i);
		vertex_info vi = gr.get_vertex_info(i);
		PI32 p(vi.lpos, vi.rpos);
		DI d(weight, 1);
		regions.push_back(PPDI(p, d));
	}
	return 0;
}

int combined_graph::build_start_bounds(splice_graph &gr)
{
	sbounds.clear();
	PEEI pei = gr.out_edges(0);
	int n = gr.num_vertices() - 1;
	for(edge_iterator it = pei.first; it != pei.second; it++)
	{
		int s = (*it)->source(); 
		int t = (*it)->target();
		assert(s == 0 && t > s);
		if(t == n) continue;
		double w = gr.get_edge_weight(*it);
		int32_t p = gr.get_vertex_info(t).lpos;
		int c = 1;

		PIDI pi(p, DI(w, c));
		sbounds.push_back(pi);
	}
	return 0;
}

int combined_graph::build_end_bounds(splice_graph &gr)
{
	tbounds.clear();
	int n = gr.num_vertices() - 1;
	PEEI pei = gr.in_edges(n);
	for(edge_iterator it = pei.first; it != pei.second; it++)
	{
		int s = (*it)->source(); 
		int t = (*it)->target();
		assert(t == n);
		assert(s < t);
		if(s == 0) continue;
		double w = gr.get_edge_weight(*it);
		int32_t p = gr.get_vertex_info(s).rpos;
		int c = 1;

		PIDI pi(p, DI(w, c));
		tbounds.push_back(pi);
	}
	return 0;
}

int combined_graph::build_splices_junctions(splice_graph &gr)
{
	junctions.clear();
	splices.clear();
	PEEI pei = gr.edges();
	set<int32_t> sp;
	int n = gr.num_vertices() - 1;
	for(edge_iterator it = pei.first; it != pei.second; it++)
	{
		int s = (*it)->source(); 
		int t = (*it)->target();
		assert(s < t);
		if(s == 0) continue;
		if(t == n) continue;
		double w = gr.get_edge_weight(*it);
		int32_t p1 = gr.get_vertex_info(s).rpos;
		int32_t p2 = gr.get_vertex_info(t).lpos;
		int c = 1;
		if(p1 >= p2) continue;

		PI32 p(p1, p2);
		DI d(w, c);
		junctions.push_back(PPDI(p, d));
		sp.insert(p1);
		sp.insert(p2);
	}
	splices.assign(sp.begin(), sp.end());
	sort(splices.begin(), splices.end());
	return 0;
}

int combined_graph::build_phase(splice_graph &gr, hyper_set &hs)
{
	phase.clear();
	map<vector<int32_t>, int> mm;
	for(MVII::const_iterator it = hs.nodes.begin(); it != hs.nodes.end(); it++)
	{
		const vector<int> &v = it->first;
		int w = it->second;
		int c = 1;

		if(v.size() <= 0) continue;
		vector<int32_t> vv;
		build_exon_coordinates_from_path(gr, v, vv);

		if(vv.size() <= 1) continue;
		vector<int32_t> zz(vv.begin() + 1, vv.end() - 1);
		map<vector<int32_t>, int>::iterator tp = mm.find(zz);
		if(tp == mm.end()) 
		{
			rcluster r;
			r.vv = zz;
			r.vl.push_back(vv.front());
			r.vr.push_back(vv.back());
			r.cc.push_back(w);
			mm.insert(pair<vector<int32_t>, int>(zz, phase.size()));
			phase.push_back(r);
		}
		else 
		{
			int k = tp->second;
			assert(zz == phase[k].vv);
			phase[k].vl.push_back(vv.front());
			phase[k].vr.push_back(vv.back());
			phase[k].cc.push_back(w);
		}
	}
	return 0;
}

int combined_graph::build_reads(splice_graph &gr, vector<PRC> &ub)
{
	reads.clear();
	int n = gr.num_vertices() - 1;
	for(int i = 0; i < ub.size(); i++)
	{
		PRC &prc = ub[i];
		if(prc.first.vv.size() <= 0) continue;
		if(prc.second.vv.size() <= 0) continue;
		assert(prc.first.vv.front() != 0);
		assert(prc.second.vv.front() != 0);
		assert(prc.first.vv.back() != n);
		assert(prc.second.vv.back() != n);

		PRC rr = prc;
		build_exon_coordinates_from_path(gr, prc.first.vv, rr.first.vv);
		build_exon_coordinates_from_path(gr, prc.second.vv, rr.second.vv);
		reads.push_back(rr);
	}
	return 0;
}

int combined_graph::combine(const combined_graph &gt)
{
	if(children.size() == 0) children.push_back(*this);

	if(gt.children.size() == 0) children.push_back(gt);
	else children.insert(children.end(), gt.children.begin(), gt.children.end());

	if(chrm == "") chrm = gt.chrm;
	if(strand == '?') strand = gt.strand;
	assert(gt.chrm == chrm);
	assert(gt.strand == strand);

	num_combined += gt.num_combined;

	// combine splices
	vector<int32_t> vv(gt.splices.size() + splices.size(), 0);
	vector<int32_t>::iterator it = set_union(gt.splices.begin(), gt.splices.end(), splices.begin(), splices.end(), vv.begin());
	vv.resize(it - vv.begin());
	splices = vv;

	return 0;
}

int combined_graph::get_overlapped_splice_positions(const vector<int32_t> &v) const
{
	vector<int32_t> vv(v.size(), 0);
	vector<int32_t>::iterator it = set_intersection(v.begin(), v.end(), splices.begin(), splices.end(), vv.begin());
	return it - vv.begin();
}

int combined_graph::combine_children()
{
	if(children.size() == 0) return 0;

	split_interval_map imap;
	map<PI32, DI> mj;
	map<int32_t, DI> ms;
	map<int32_t, DI> mt;
	phase.clear();
	reads.clear();

	int num = 0;
	for(int i = 0; i < children.size(); i++)
	{
		combined_graph &gt = children[i];
		combine_regions(imap, gt);
		combine_junctions(mj, gt);
		combine_start_bounds(ms, gt);
		combine_end_bounds(mt, gt);
		//phase.insert(phase.end(), gt.phase.begin(), gt.phase.end());
		//reads.insert(reads.end(), gt.reads.begin(), gt.reads.end());
		num += gt.num_combined;
	}
	assert(num == num_combined);

	regions.clear();
	for(SIMI it = imap.begin(); it != imap.end(); it++)
	{
		int32_t l = lower(it->first);
		int32_t r = upper(it->first);
		regions.push_back(PPDI(PI32(l, r), DI(it->second, 1)));
	}

	junctions.assign(mj.begin(), mj.end());
	sbounds.assign(ms.begin(), ms.end());
	tbounds.assign(mt.begin(), mt.end());

	return 0;
}

int combined_graph::combine_regions(split_interval_map &imap, const combined_graph &gt)
{
	for(int i = 0; i < gt.regions.size(); i++)
	{
		PI32 p = gt.regions[i].first;
		int w = (int)(gt.regions[i].second.first);
		imap += make_pair(ROI(p.first, p.second), w);
	}
	return 0;
}

int combined_graph::combine_junctions(map<PI32, DI> &m, const combined_graph &gt)
{
	for(int i = 0; i < gt.junctions.size(); i++)
	{
		PI32 p = gt.junctions[i].first;
		DI d = gt.junctions[i].second;

		map<PI32, DI>::iterator x = m.find(p);

		if(x == m.end())
		{
			m.insert(pair<PI32, DI>(p, d));
		}
		else 
		{
			x->second.first += d.first;
			x->second.second += d.second;
		}
	}
	return 0;
}

int combined_graph::combine_start_bounds(map<int32_t, DI> &m, const combined_graph &gt)
{
	for(int i = 0; i < gt.sbounds.size(); i++)
	{
		int32_t p = gt.sbounds[i].first;
		DI d = gt.sbounds[i].second;

		map<int32_t, DI>::iterator x = m.find(p);

		if(x == m.end())
		{
			m.insert(pair<int32_t, DI>(p, d));
		}
		else 
		{
			x->second.first += d.first;
			x->second.second += d.second;
		}
	}
	return 0;
}

int combined_graph::combine_end_bounds(map<int32_t, DI> &m, const combined_graph &gt)
{
	for(int i = 0; i < gt.tbounds.size(); i++)
	{
		int32_t p = gt.tbounds[i].first;
		DI d = gt.tbounds[i].second;

		map<int32_t, DI>::iterator x = m.find(p);

		if(x == m.end())
		{
			m.insert(pair<int32_t, DI>(p, d));
		}
		else 
		{
			x->second.first += d.first;
			x->second.second += d.second;
		}
	}
	return 0;
}

int combined_graph::resolve(splice_graph &gr, hyper_set &hs, vector<PRC> &ub)
{
	group_junctions();
	build_splice_graph(gr);
	group_start_boundaries(gr);
	group_end_boundaries(gr);
	build_phasing_paths(gr, hs);
	return 0;
}

int combined_graph::group_start_boundaries(splice_graph &gr)
{
	smap.clear();
	vector<int> v;
	edge_iterator it;
	PEEI pei = gr.out_edges(0);
	for(edge_iterator it = pei.first; it != pei.second; it++)
	{
		edge_descriptor e = *it;
		assert(e->source() == 0);
		v.push_back(e->target());
	}

	if(v.size() <= 1) return 0;

	sort(v.begin(), v.end());

	int32_t p1 = gr.get_vertex_info(v[0]).lpos;
	int32_t p2 = p1;
	int k1 = v[0];
	int k2 = k1;
	PEB pa = gr.edge(0, v[0]);
	assert(pa.second == true);
	double wa = gr.get_edge_weight(pa.first);
	edge_info ea = gr.get_edge_info(pa.first);

	for(int i = 1; i < v.size(); i++)
	{
		int32_t p = gr.get_vertex_info(v[i]).lpos;
		PEB pb = gr.edge(0, v[i]);
		assert(pb.second == true);
		double wb = gr.get_edge_weight(pb.first);
		edge_info eb = gr.get_edge_info(pb.first);

		bool b = check_continuous_vertices(gr, k2, v[i]);

		assert(p >= p2);
		if(p - p2 > max_group_boundary_distance) b = false;

		if(b == false)
		{
			p1 = p;
			p2 = p;
			k1 = v[i];
			k2 = v[i];
			pa = pb;
			wa = wb;
			ea = eb;
		}
		else
		{
			smap.insert(pair<int32_t, int32_t>(p, p1));
			for(int j = k1; j < v[i]; j++)
			{
				PEB pc = gr.edge(j, j + 1);
				assert(pc.second == true);
				double vc = gr.get_vertex_weight(j);
				double wc = gr.get_edge_weight(pc.first);
				gr.set_vertex_weight(j, vc + wb);
				edge_info ec = gr.get_edge_info(pc.first);
				ec.count += eb.count;
				ec.weight += eb.weight;
				gr.set_edge_weight(pc.first, wc + wb);
				gr.set_edge_info(pc.first, ec);
			}
			wa += wb;
			ea.count += eb.count;
			ea.weight += eb.weight;
			gr.set_edge_weight(pa.first, wa);
			gr.set_edge_info(pa.first, ea);
			gr.remove_edge(pb.first);

			k2 = v[i];
			p2 = p;

			if(meta_verbose >= 2) printf("map start boundary %d:%d (weight = %.2lf) to %d:%d (weight = %.2lf)\n", v[i], p, wb, k1, p1, wa);
		}
	}
	return 0;
}

int combined_graph::group_end_boundaries(splice_graph &gr)
{
	tmap.clear();
	vector<int> v;
	edge_iterator it;
	PEEI pei = gr.in_edges(gr.num_vertices() - 1);
	for(edge_iterator it = pei.first; it != pei.second; it++)
	{
		edge_descriptor e = *it;
		assert(e->target() == gr.num_vertices() - 1);
		v.push_back(e->source());
	}

	if(v.size() <= 1) return 0;

	sort(v.begin(), v.end(), greater<int>());

	int32_t p1 = gr.get_vertex_info(v[0]).rpos;
	int32_t p2 = p1;
	int k1 = v[0];
	int k2 = k1;
	PEB pa = gr.edge(v[0], gr.num_vertices() - 1);
	assert(pa.second == true);
	double wa = gr.get_edge_weight(pa.first);

	for(int i = 1; i < v.size(); i++)
	{
		int32_t p = gr.get_vertex_info(v[i]).rpos;
		PEB pb = gr.edge(v[i], gr.num_vertices() - 1);
		assert(pb.second == true);
		double wb = gr.get_edge_weight(pb.first);

		bool b = check_continuous_vertices(gr, v[i], k2);

		assert(p <= p2);
		if(p2 - p > max_group_boundary_distance) b = false;

		if(b == false)
		{
			p1 = p;
			p2 = p;
			k1 = v[i];
			k2 = v[i];
			pa = pb;
			wa = wb;
		}
		else
		{
			tmap.insert(pair<int32_t, int32_t>(p, p1));
			for(int j = v[i]; j < k1; j++)
			{
				PEB pc = gr.edge(j, j + 1);
				assert(pc.second == true);
				double wc = gr.get_edge_weight(pc.first);
				double vc = gr.get_vertex_weight(j + 1);
				gr.set_edge_weight(pc.first, wc + wb);
				gr.set_vertex_weight(j + 1, wc + wb);
			}
			wa += wb;
			gr.set_edge_weight(pa.first, wa);
			gr.remove_edge(pb.first);

			k2 = v[i];
			p2 = p;

			if(meta_verbose >= 2) printf("map end boundary %d:%d (weight = %.2lf) to %d:%d (weight = %.2lf)\n", v[i], p, wb, k1, p1, wa);
		}
	}
	return 0;
}

int combined_graph::group_junctions()
{
	set<int> fb;
	for(int i = 0; i < junctions.size(); i++)
	{
		if(fb.find(i) != fb.end()) continue;
		PPDI x = junctions[i];
		for(int j = i + 1; j < junctions.size(); j++)
		{
			if(fb.find(j) != fb.end()) continue;
			PPDI y = junctions[j];
			double d1 = fabs(x.first.first - y.first.first);
			double d2 = fabs(x.first.second - y.first.second);
			if(d1 + d2 >= max_group_junction_distance) continue;
			if(10 * x.second.first < y.second.first && x.second.second < y.second.second && x.second.second <= 2 && y.second.first <= 100)
			{
				fb.insert(i);
				if(meta_verbose >= 2) printf("filter junction: (%d, %d), w = %.1lf, c = %d -> (%d, %d), w = %.1lf, c = %d\n", 
						x.first.first, x.first.second, x.second.first, x.second.second,
						y.first.first, y.first.second, y.second.first, y.second.second);
			}
			if(x.second.first > 10 * y.second.first && x.second.second > y.second.second && y.second.second <= 2 && y.second.first <= 100)
			{
				if(meta_verbose >= 2) printf("filter junction: (%d, %d), w = %.1lf, c = %d -> (%d, %d), w = %.1lf, c = %d\n",
						y.first.first, y.first.second, y.second.first, y.second.second,
						x.first.first, x.first.second, x.second.first, x.second.second);
				fb.insert(j);
			}
		}
	}

	vector<PPDI> v;
	for(int k = 0; k < junctions.size(); k++)
	{
		PPDI p = junctions[k];
		if(fb.find(k) == fb.end()) v.push_back(p);
	}

	junctions = v;
	return 0;
}

int combined_graph::build_splice_graph(splice_graph &gr)
{
	gr.clear();

	gr.gid = gid;
	gr.chrm = chrm;
	gr.strand = strand;

	// add vertices
	gr.add_vertex();	// 0
	PIDI sb = get_leftmost_bound();
	vertex_info v0;
	v0.lpos = sb.first;
	v0.rpos = sb.first;
	gr.set_vertex_weight(0, 0);
	gr.set_vertex_info(0, v0);

	for(int i = 0; i < regions.size(); i++) 
	{
		gr.add_vertex();
		vertex_info vi;
		vi.lpos = regions[i].first.first;
		vi.rpos = regions[i].first.second;
		vi.count = regions[i].second.second;
		double w = regions[i].second.first;
		vi.length = vi.rpos - vi.lpos;
		gr.set_vertex_weight(i + 1, w);
		gr.set_vertex_info(i + 1, vi);
	}

	gr.add_vertex();	// n
	PIDI tb = get_rightmost_bound();
	vertex_info vn;
	vn.lpos = tb.first;
	vn.rpos = tb.first;
	gr.set_vertex_info(regions.size() + 1, vn);
	gr.set_vertex_weight(regions.size() + 1, 0);

	// build vertex index
	gr.build_vertex_index();

	// add sbounds
	for(int i = 0; i < sbounds.size(); i++)
	{
		int32_t p = sbounds[i].first;
		double w = sbounds[i].second.first;
		int c = sbounds[i].second.second;

		assert(gr.lindex.find(p) != gr.lindex.end());
		int k = gr.lindex[p];
		edge_descriptor e = gr.add_edge(0, k);
		edge_info ei;
		ei.weight = w;
		ei.count = c;
		gr.set_edge_info(e, ei);
		gr.set_edge_weight(e, w);
	}

	// add tbounds
	for(int i = 0; i < tbounds.size(); i++)
	{
		int32_t p = tbounds[i].first;
		double w = tbounds[i].second.first;
		int c = tbounds[i].second.second;

		assert(gr.rindex.find(p) != gr.rindex.end());
		int k = gr.rindex[p];
		edge_descriptor e = gr.add_edge(k, gr.num_vertices() - 1);
		edge_info ei;
		ei.weight = w;
		ei.count = c;
		gr.set_edge_info(e, ei);
		gr.set_edge_weight(e, w);
	}

	// add junctions
	for(int i = 0; i < junctions.size(); i++)
	{
		PI32 p = junctions[i].first;
		double w = junctions[i].second.first;
		int c = junctions[i].second.second;

		// filtering later on

		assert(gr.rindex.find(p.first) != gr.rindex.end());
		assert(gr.lindex.find(p.second) != gr.lindex.end());
		int s = gr.rindex[p.first];
		int t = gr.lindex[p.second];
		edge_descriptor e = gr.add_edge(s, t);
		edge_info ei;
		ei.weight = w;
		ei.count = c;
		gr.set_edge_info(e, ei);
		gr.set_edge_weight(e, w);
	}

	// connect adjacent regions
	for(int i = 1; i < regions.size(); i++)
	{
		int32_t p1 = regions[i - 1].first.second;
		int32_t p2 = regions[i - 0].first.first;

		assert(p1 <= p2);
		if(p1 < p2) continue;

		PPDI ss = regions[i - 1];
		PPDI tt = regions[i - 0];
		if(ss.first.second != tt.first.first) continue;

		// TODO
		/*
		double w1 = gr.get_out_weights(i + 0);
		double w2 = gr.get_in_weights(i + 1);
		double ws = ss.second.first - w1;
		double wt = tt.second.first - w2;
		double w = (ws + wt) * 0.5;
		*/

		int xd = gr.out_degree(i + 0);
		int yd = gr.in_degree(i + 1);
		double w = (xd < yd) ? ss.second.first : tt.second.first;
		int c = ss.second.second;
		if(ss.second.second > tt.second.second) c = tt.second.second;

		if(w < 1) w = 1;
		edge_descriptor e = gr.add_edge(i + 0, i + 1);
		edge_info ei;
		ei.weight = w;
		ei.count = c;
		gr.set_edge_info(e, ei);
		gr.set_edge_weight(e, w);
	}
	return 0;
}

int combined_graph::build_phasing_paths(splice_graph &gr, hyper_set &hs, rcluster &rc)
{
	vector<int> uu;
	bool b = build_path_from_intron_coordinates(gr, rc.vv, uu);
	assert(b == true);

	for(int j = 0; j < rc.vl.size(); j++)
	{
		int32_t p1 = rc.vl[j];
		int32_t p2 = rc.vr[j];
		int w = rc.cc[j];

		assert(p1 >= 0 && p2 >= 0);

		// NOTE: group phasing here
		if(smap.find(p1) != smap.end()) p1 = smap[p1];
		if(tmap.find(p2) != tmap.end()) p2 = tmap[p2];

		assert(gr.lindex.find(p1) != gr.lindex.end());
		assert(gr.rindex.find(p2) != gr.rindex.end());

		int a = gr.lindex[p1];
		int b = gr.rindex[p2];

		vector<int> vv;
		if(uu.size() == 0)
		{
			for(int k = a; k <= b; k++) vv.push_back(k);
		}
		else
		{
			for(int k = a; k < uu.front(); k++) vv.push_back(k);
			vv.insert(vv.end(), uu.begin(), uu.end());
			for(int k = uu.back() + 1; k <= b; k++) vv.push_back(k);
		}

		for(int k = 0; k < vv.size(); k++) vv[k]--;
		hs.add_node_list(vv, w);
	}
	return 0;
}

int combined_graph::build_phasing_paths(splice_graph &gr, hyper_set &hs)
{
	hs.clear();
	for(int i = 0; i < phase.size(); i++)
	{
		build_phasing_paths(gr, hs, phase[i]);
	}

	for(int k = 0; k < children.size(); k++)
	{
		combined_graph &gt = children[k];
		for(int i = 0; i < gt.phase.size(); i++)
		{
			build_phasing_paths(gr, hs, gt.phase[i]);
		}
	}
	return 0;
}



set<int32_t> combined_graph::get_reliable_adjacencies(int samples, double weight)
{
	set<int32_t> s;
	if(regions.size() <= 1) return s;
	for(int i = 0; i < regions.size() - 1; i++)
	{
		int32_t p1 = regions[i + 0].first.second;
		int32_t p2 = regions[i + 1].first.first;
		if(p1 != p2) continue;

		double w1 = regions[i + 0].second.first;
		double w2 = regions[i + 1].second.first;
		int c1 = regions[i + 0].second.second;
		int c2 = regions[i + 1].second.second;

		bool b = false;
		if(w1 >= weight && w2 >= weight) b = true;
		if(c1 >= samples && c2 >= samples) b = true;

		if(b == false) continue;
		s.insert(p1);
	}
	return s;
}

set<int32_t> combined_graph::get_reliable_splices(int samples, double weight)
{
	map<int32_t, DI> m;
	for(int i = 0; i < junctions.size(); i++)
	{
		PI32 p = junctions[i].first;
		int32_t p1 = p.first;
		int32_t p2 = p.second;
		double w = junctions[i].second.first;
		int c = junctions[i].second.second;

		if(m.find(p1) == m.end())
		{
			DI di(w, c);
			m.insert(pair<int32_t, DI>(p1, di));
		}
		else
		{
			m[p1].first += w;
			m[p1].second += c;
		}

		if(m.find(p2) == m.end())
		{
			DI di(w, c);
			m.insert(pair<int32_t, DI>(p2, di));
		}
		else
		{
			m[p2].first += w;
			m[p2].second += c;
		}
	}

	set<int32_t> s;
	for(map<int32_t, DI>::iterator it = m.begin(); it != m.end(); it++)
	{
		int32_t p = it->first;
		double w = it->second.first;
		int c = it->second.second;
		if(w < weight && c < samples) continue;
		s.insert(p);
	}
	return s;
}

set<PI32> combined_graph::get_reliable_junctions(int samples, double weight)
{
	set<PI32> s;
	for(int i = 0; i < junctions.size(); i++)
	{
		PI32 p = junctions[i].first;
		double w = junctions[i].second.first;
		int c = junctions[i].second.second;

		if(c < samples && w < weight) continue;
		s.insert(p);
	}
	return s;
}

set<int32_t> combined_graph::get_reliable_start_boundaries(int samples, double weight)
{
	map<int32_t, DI> m;
	for(int i = 0; i < sbounds.size(); i++)
	{
		int32_t p = sbounds[i].first;
		int32_t q = p;
		if(smap.find(p) != smap.end()) q = smap[p];
		double w = sbounds[i].second.first;
		int c = sbounds[i].second.second;

		if(m.find(q) == m.end())
		{
			DI di(w, c);
			m.insert(pair<int32_t, DI>(q, di));
		}
		else
		{
			m[q].first += w;
			m[q].second += c;
		}
	}

	set<int32_t> s;
	for(map<int32_t, DI>::iterator it = m.begin(); it != m.end(); it++)
	{
		int32_t p = it->first;
		double w = it->second.first;
		int c = it->second.second;
		if(w < weight && c < samples) continue;
		s.insert(p);
	}

	set<int32_t> ss;
	for(int i = 0; i < sbounds.size(); i++)
	{
		int32_t p = sbounds[i].first;
		int32_t q = p;
		if(smap.find(p) != smap.end()) q = smap[p];
		if(s.find(q) == s.end()) continue;
		ss.insert(p);
	}

	return ss;
}

set<int32_t> combined_graph::get_reliable_end_boundaries(int samples, double weight)
{
	map<int32_t, DI> m;
	for(int i = 0; i < tbounds.size(); i++)
	{
		int32_t p = tbounds[i].first;
		int32_t q = p;
		if(tmap.find(p) != tmap.end()) q = tmap[p];
		double w = tbounds[i].second.first;
		int c = tbounds[i].second.second;

		if(m.find(q) == m.end())
		{
			DI di(w, c);
			m.insert(pair<int32_t, DI>(q, di));
		}
		else
		{
			m[q].first += w;
			m[q].second += c;
		}
	}

	set<int32_t> s;
	for(map<int32_t, DI>::iterator it = m.begin(); it != m.end(); it++)
	{
		int32_t p = it->first;
		double w = it->second.first;
		int c = it->second.second;
		if(w < weight && c < samples) continue;
		s.insert(p);
	}

	set<int32_t> ss;
	for(int i = 0; i < tbounds.size(); i++)
	{
		int32_t p = tbounds[i].first;
		int32_t q = p;
		if(tmap.find(p) != tmap.end()) q = tmap[p];
		if(s.find(q) == s.end()) continue;
		ss.insert(p);
	}

	return ss;
}

int combined_graph::clear()
{
	num_combined = 0;
	gid = "";
	chrm = "";
	strand = '.';
	splices.clear();
	regions.clear();
	junctions.clear();
	sbounds.clear();
	tbounds.clear();
	phase.clear();
	reads.clear();
	smap.clear();
	tmap.clear();
	return 0;
}

int combined_graph::print(int index)
{
	printf("combined-graph %d: #combined = %d, chrm = %s, strand = %c, #regions = %lu, #sbounds = %lu, #tbounds = %lu, #junctions = %lu, #phasing-phase = %lu\n", 
			index, num_combined, chrm.c_str(), strand, regions.size(), sbounds.size(), tbounds.size(), junctions.size(), phase.size());

	//printf("combined-graph %d: #combined = %d, chrm = %s, strand = %c, #regions = %lu, #sbounds = %lu, #tbounds = %lu, #junctions = %lu, #phase = %lu, #reads = %lu\n", 
	//		index, num_combined, chrm.c_str(), strand, regions.size(), sbounds.size(), tbounds.size(), junctions.size(), phase.size(), reads.size());


	for(int i = 0; i < regions.size(); i++)
	{
		PI32 p = regions[i].first;
		DI d = regions[i].second;
		printf("region %d: [%d, %d), w = %.2lf, c = %d\n", i, p.first, p.second, d.first, d.second);
	}
	for(int i = 0; i < junctions.size(); i++)
	{
		PI32 p = junctions[i].first;
		DI d = junctions[i].second;
		printf("junction %d: [%d, %d), w = %.2lf, c = %d\n", i, p.first, p.second, d.first, d.second);
	}
	for(int i = 0; i < sbounds.size(); i++)
	{
		int32_t p = sbounds[i].first;
		DI d = sbounds[i].second;
		printf("sbound %d: %d, w = %.2lf, c = %d\n", i, p, d.first, d.second);
	}
	for(int i = 0; i < tbounds.size(); i++)
	{
		int32_t p = tbounds[i].first;
		DI d = tbounds[i].second;
		printf("tbound %d: %d, w = %.2lf, c = %d\n", i, p, d.first, d.second);
	}
	for(int i = 0; i < phase.size(); i++)
	{
		rcluster &r = phase[i];
		printf("path %d: vv = ( ", i);
		printv(r.vv);
		printf(")\n");
		for(int k = 0; k < r.vl.size(); k++)
		{
			printf(" bounds = (%d, %d), w = %d, c = 1\n", r.vl[k], r.vr[k], r.cc[k]);
		}
	}
	return 0;
}

PIDI combined_graph::get_leftmost_bound()
{
	PIDI x;
	x.first = -1;
	for(int i = 0; i < sbounds.size(); i++)
	{
		int32_t p = sbounds[i].first;
		if(x.first == -1 || p < x.first)
		{
			x = sbounds[i];
		}
	}
	return x;
}

PIDI combined_graph::get_rightmost_bound()
{
	PIDI x;
	x.first = -1;
	for(int i = 0; i < tbounds.size(); i++)
	{
		int32_t p = tbounds[i].first;
		if(x.first == -1 || p > x.first)
		{
			x = tbounds[i];
		}
	}
	return x;
}
