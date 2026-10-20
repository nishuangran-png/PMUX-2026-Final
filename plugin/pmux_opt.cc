/*
 * pmux_opt —— PMUX 结构重构 pass（本选题的核心算法）
 *
 * 做什么：
 *   在 Yosys 网表里找出「常量解码型 $pmux」——也就是选择信号由一组
 *   `$eq(bus, 常量)` 比较器解出来的 $pmux，把比较器和 $pmux 整体换掉，
 *   改成直接用 bus 的位驱动的平衡 mux 树。
 *
 * 为什么省：
 *   默认流程里 `$pmux` 会被 techmap 展开成「每片 AND 上自己的选择信号、
 *   再全部 OR 起来」的与或网络，比较器也要单独实现。实测 pmux_basic：
 *   默认 132 个门 → 改写后 56 个 $_MUX_，降 57.6%。
 *   上游现成的 `pmuxtree` 只能到 103，剩下这一段正是本 pass 的增量。
 *
 * 正确性：
 *   原语义是「bus == 码 k 时输出第 k 片；其余情况输出 A」。
 *   只要把所有码的叶子按 bus 取值摆好，缺的那一个码放 A，
 *   位选 mux 树就与该语义逐位相同。等价性由 EQY 验证（scripts/regress.sh）。
 *
 * 用法：
 *   yosys -m build/pmux_opt.so -p "read_verilog x.v; proc; opt; pmux_opt; synth -top x"
 *   选项：-max_leaves N   叶子上限，默认 64（即 bus 最宽 6 位）
 *         -no_cost_model 关掉代价模型（强制改写所有识别到的 $pmux）
 *         -v             打印每个被改写的 $pmux
 */

#include "kernel/yosys.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"
#include "kernel/register.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace {

// 一个选择位的解码信息
struct DecodeBit {
	SigSpec bus;         // 被比较的公共信号
	int code = -1;       // 该位对应的选择码
	pool<IdString> helper; // 解这一位用到的辅助单元
};

bool analyze_select_bit(const SigSpec &bit, SigMap &sigmap,
                        const dict<SigBit, Cell *> &bit2cell, RTLIL::Module *module,
                        DecodeBit &out)
{
	SigBit b = sigmap(bit);
	auto it = bit2cell.find(b);
	if (it == bit2cell.end())
		return false;
	Cell *drv = it->second;
	if (drv == nullptr || !module->cells_.count(drv->name))
		return false;

	if (drv->type.in(ID($eq), ID($eqx))) {
		SigSpec pa = sigmap(drv->getPort(ID::A));
		SigSpec pb = sigmap(drv->getPort(ID::B));
		if (GetSize(pa) != GetSize(pb))
			return false;
		if (pa.is_fully_const() && !pb.is_fully_const()) {
			out.bus = pb;
			out.code = pa.as_int();
		} else if (pb.is_fully_const() && !pa.is_fully_const()) {
			out.bus = pa;
			out.code = pb.as_int();
		} else {
			return false;
		}
		out.helper.insert(drv->name);
		return true;
	}

	if (drv->type == ID($logic_not)) {
		SigSpec in = sigmap(drv->getPort(ID::A));

		// $logic_not 的语义是「整值是否为 0」（对多位输入也一样，
		// 不是按位取反）。所以：
		//   $logic_not(sel)      → 该位 = (sel == 0)，bus 就是 sel，码 0
		//   $logic_not(|sel|)    → 同上，bus 是 $reduce_or 的输入
		if (GetSize(in) > 1) {
			out.bus = in;
			out.code = 0;
			out.helper.insert(drv->name);
			return true;
		}

		auto jt = bit2cell.find(in[0]);
		if (jt != bit2cell.end() && jt->second != nullptr &&
		    module->cells_.count(jt->second->name) &&
		    jt->second->type.in(ID($reduce_or), ID($reduce_bool))) {
			Cell *red = jt->second;
			out.bus = sigmap(red->getPort(ID::A));
			out.code = 0;
			out.helper.insert(drv->name);
			out.helper.insert(red->name);
			return true;
		}
		if (jt == bit2cell.end()) {
			// logic_not(x) 且 x 就是一位输入 —— 表示 x == 0
			out.bus = in;
			out.code = 0;
			out.helper.insert(drv->name);
			return true;
		}
		return false;
	}

	return false;
}

// 按 bus 的位建平衡 mux 树；leaves[c] 是 bus == c 时的输出
SigSpec build_mux_tree(Module *module, const SigSpec &bus, const std::vector<SigSpec> &leaves)
{
	if (GetSize(bus) == 0)
		return leaves[0];
	int n = GetSize(bus);
	int half = GetSize(leaves) / 2;
	std::vector<SigSpec> lo(leaves.begin(), leaves.begin() + half);
	std::vector<SigSpec> hi(leaves.begin() + half, leaves.end());
	SigSpec l = build_mux_tree(module, bus.extract(0, n - 1), lo);
	SigSpec h = build_mux_tree(module, bus.extract(0, n - 1), hi);
	return module->Mux(NEW_ID, l, h, bus[n - 1]);
}

// 代价模型：先算出「建完树、再被常量折叠之后，真正会剩下多少个 mux 节点」。
// 一个内部节点整段同值（比如整段都是 default 的 A）时会被 opt 折叠掉，不计入。
// 返回 {节点数, 是否整段同值}；同值时 out_sig 给出那个共同的值。
std::pair<int, SigSpec> tree_cost(const std::vector<SigSpec> &leaves, int lo, int hi)
{
	if (hi - lo == 1)
		return {0, leaves[lo]};
	int mid = (lo + hi) / 2;
	auto l = tree_cost(leaves, lo, mid);
	auto r = tree_cost(leaves, mid, hi);
	if (l.first == 0 && r.first == 0 && l.second == r.second)
		return {0, l.second};
	return {1 + l.first + r.first, SigSpec()};
}

bool bit_is_used(Module *module, const SigSpec &sig, SigMap &sigmap)
{
	pool<SigBit> want;
	for (auto bit : sigmap(sig))
		want.insert(bit);

	for (auto wire : module->wires())
		if (wire->port_output)
			for (auto bit : sigmap(wire))
				if (want.count(bit))
					return true;

	for (auto cell : module->cells())
		for (auto &conn : cell->connections())
			if (cell->input(conn.first))
				for (auto bit : sigmap(conn.second))
					if (want.count(bit))
						return true;
	return false;
}

} // namespace

struct PmuxOptPass : public Pass {
	PmuxOptPass() : Pass("pmux_opt", "把常量解码型 $pmux 重构为位选 mux 树") {}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		int max_leaves = 64;
		bool verbose = false;
		bool cost_model = true;

		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			std::string a = args[argidx];
			if (a == "-max_leaves" && argidx + 1 < args.size()) {
				max_leaves = atoi(args[++argidx].c_str());
				continue;
			}
			if (a == "-v") {
				verbose = true;
				continue;
			}
			if (a == "-no_cost_model") {
				cost_model = false;
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);

		log_header(design, "Executing PMUX_OPT pass.\n");

		CellTypes ct;
		ct.setup_internals();
		ct.setup_design(design);

		int n_ok = 0, n_skip = 0;

		for (auto module : design->selected_modules()) {
			SigMap sigmap(module);

			dict<SigBit, Cell *> bit2cell;
			for (auto cell : module->cells())
				for (auto &conn : cell->connections())
					if (ct.cell_output(cell->type, conn.first))
						for (auto bit : sigmap(conn.second))
							bit2cell[bit] = cell;

			std::vector<Cell *> pmuxes;
			for (auto cell : module->cells())
				if (cell->type == ID($pmux))
					pmuxes.push_back(cell);

			pool<IdString> dead;

			for (auto cell : pmuxes) {
				if (!module->cells_.count(cell->name))
					continue;

				SigSpec s = sigmap(cell->getPort(ID::S));
				SigSpec bport = sigmap(cell->getPort(ID::B));
				SigSpec aport = sigmap(cell->getPort(ID::A));
				SigSpec y = cell->getPort(ID::Y);

				int n_sel = GetSize(s);
				int width = GetSize(y);
				if (n_sel == 0 || width == 0 || GetSize(bport) != n_sel * width) {
					n_skip++;
					continue;
				}

				std::vector<DecodeBit> dec(n_sel);
				bool ok = true;
				for (int i = 0; i < n_sel; i++) {
					if (!analyze_select_bit(s[i], sigmap, bit2cell, module, dec[i])) {
						ok = false;
						break;
					}
					if (dec[i].bus != dec[0].bus) {
						ok = false;
						break;
					}
				}
				if (!ok || (int)dec[0].bus.size() > 12) {
					n_skip++;
					continue;
				}

				int bus_width = GetSize(dec[0].bus);
				int n_leaves = 1 << bus_width;
				if (n_leaves > max_leaves) {
					n_skip++;
					continue;
				}

				std::vector<int> slot(n_leaves, -1);
				bool dup = false;
				for (int i = 0; i < n_sel; i++) {
					int c = dec[i].code;
					if (c < 0 || c >= n_leaves || slot[c] != -1) {
						dup = true;
						break;
					}
					slot[c] = i;
				}
				if (dup) {
					n_skip++;
					continue;
				}

				// 没被 case 列到的码，输出都该是 A —— 这些叶子各填一份 A 即可，
				// 多缺几个码也能处理（原来只支持缺 1 个，太保守）。
				int n_missing = 0;
				for (int c = 0; c < n_leaves; c++)
					if (slot[c] == -1)
						n_missing++;

				std::vector<SigSpec> leaves(n_leaves);
				for (int c = 0; c < n_leaves; c++)
					leaves[c] = (slot[c] >= 0) ? bport.extract(slot[c] * width, width) : aport;

				// 代价模型：树 vs 默认流程那套「比较器 + 与或网络」。
				//   树：折叠后剩下的 mux 节点数 × 数据位宽（这一侧估得比较准）
				//   默认：与或网络约 (k−1)×W，再加 k 个比较器的译码开销 2×k×|bus|
				// 预估不划算就跳过（模糊测试里 fuzz_033 就是这种：4 位总线只列了
				// 3 个码、数据 16 位，建树反而比与或网络贵 30%）。
				if (cost_model) {
					int tree_nodes = tree_cost(leaves, 0, n_leaves).first;
					long long est_tree = (long long)tree_nodes * width;
					long long est_orig = (long long)(n_sel - 1) * width
					                   + (long long)2 * n_sel * bus_width;
					// 阈值 1.5：估算本身很粗（ABC 的行为还会再变一层），
					// 只在「明显不划算」时才跳过，避免误伤真实收益。
					if (est_tree * 2 > est_orig * 3) {
						n_skip++;
						if (verbose)
							log("  %s: 代价模型预估不划算（树 %lld > 原 %lld），跳过\n",
							    log_id(cell->name), est_tree, est_orig);
						continue;
					}
				}

				SigSpec tree = build_mux_tree(module, dec[0].bus, leaves);
				module->connect(y, tree);
				module->remove(cell);

				for (int i = 0; i < n_sel; i++)
					for (auto h : dec[i].helper)
						dead.insert(h);

				n_ok++;
				if (verbose)
					log("  %s: %d 路选择（码宽 %d，缺码 %d 个）→ %d 叶 mux 树\n",
					    log_id(cell->name), n_sel, bus_width, n_missing, n_leaves);
			}

			// 保守清理：确认没有任何用户，才删辅助单元
			bool changed = true;
			while (changed) {
				changed = false;
				for (auto name : dead) {
					if (!module->cells_.count(name))
						continue;
					Cell *c = module->cell(name);
					if (!bit_is_used(module, c->getPort(ID::Y), sigmap)) {
						module->remove(c);
						changed = true;
					}
				}
			}
		}

		log("pmux_opt: 改写 %d 个 $pmux，跳过 %d 个\n", n_ok, n_skip);
	}
} PmuxOptPass;

PRIVATE_NAMESPACE_END
