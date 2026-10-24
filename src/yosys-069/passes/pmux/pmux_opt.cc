/*
 * pmux_opt — PMUX restructuring for the 2026 FPGA competition (topic 2).
 *
 * v2: PMUX restructuring for the official Cyclone IV flow.
 *
 * Implemented structural rewrites:
 *   1. dual-$pmux sharing;
 *   2. priority $mux-chain folding into adaptive-width nested mux trees;
 *   3. sparse partial-update PMUX collapse;
 *   4. gated equality-decoder narrowing;
 *   5. adjacent equality-code factorization and shared enable factoring.
 *   6. small combinational constant-ROM lowering to LUTs, folding a
 *      following constant-vs-ROM mux (for example a clock-enable gate).
 *
 * Pattern: two $pmux cells that
 *   - share the exact same select vector S, which decodes a bus into one-hot
 *     comparisons (bus == code, covering all 2^bw codes), and
 *   - have data vectors that are pairwise swapped by the LSB of the bus:
 *       B1[2k]   == B2[2k+1]
 *       B1[2k+1] == B2[2k]      for k = 1 .. n/2-1
 * are merged into one shared two-level selection network:
 *
 *   isel  = bus[1:] ? B1[2k]     (k >= 1),  bus[1:] == 0 ? B2[1]
 *   qsel  = bus[1:] ? B1[2k+1]   (k >= 1),  bus[1:] == 0 ? B1[1]
 *   Y1    = (bus == 0) ? B1[0] : (bus[0] ? qsel : isel)
 *   Y2    = (bus == 0) ? B2[0] : (bus[0] ? isel : qsel)
 *
 * This halves the select-network work for dual-output register updates
 * (the official test1 is the first user of this pattern).
 */

#include "kernel/yosys.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"
#include "kernel/mem.h"
#include "kernel/register.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

namespace {

// proc_rom turns complete constant switches into $mem_v2 before this pass
// runs.  A small, write-free ROM with fully-defined contents is a truth table,
// so exposing it as $lut lets ABC optimize the table directly instead of first
// materializing memory_map's mux tree.  Keep the rule deliberately narrow:
// synchronous ports, partial address spaces and X-valued entries stay in the
// normal memory flow.
struct InputLoad {
	Cell *cell;
	IdString port;
	int offset;
};

struct LutNode {
	bool is_const = false;
	bool inverted = false;
	State value = State::S0;
	SigSpec sig;
	Cell *cell = nullptr;
	int width = 0;
};

typedef std::pair<uint32_t, std::vector<uint8_t>> LutFunction;

static int popcount32(uint32_t value)
{
	return __builtin_popcount(value);
}

static uint32_t function_support(const std::vector<uint8_t> &table, int input_count)
{
	uint32_t support = 0;
	for (int input = 0; input < input_count; input++) {
		uint32_t bit = 1u << input;
		bool differs = false;
		for (int index = 0; index < GetSize(table); index++)
			if (!(index & bit) && table[index] != table[index | bit]) {
				differs = true;
				break;
			}
		if (differs)
			support |= bit;
	}
	return support;
}

// memory_map followed by ABC is already very effective for sparse truth
// tables and low-degree XOR/AND networks.  In those cases a fixed Shannon
// decomposition can add one or two LUTs.  Only replace a ROM when at least
// one output has a genuinely dense Boolean function.
static bool function_is_cheap(const std::vector<uint8_t> &table)
{
	int size = GetSize(table);
	int ones = 0;
	for (uint8_t value : table)
		ones += value != 0;
	if (std::min(ones, size - ones) <= std::max(1, size / 4))
		return true;

	std::vector<uint8_t> anf = table;
	int input_count = ceil_log2(size);
	for (int input = 0; input < input_count; input++)
		for (int index = 0; index < size; index++)
			if (index & (1 << input))
				anf[index] ^= anf[index ^ (1 << input)];

	int terms = 0;
	for (uint8_t value : anf)
		terms += value != 0;
	return terms <= 16;
}

static std::vector<uint8_t> compress_function(const std::vector<uint8_t> &table, uint32_t support)
{
	std::vector<int> positions;
	for (int input = 0; input < 32; input++)
		if (support & (1u << input))
			positions.push_back(input);

	std::vector<uint8_t> compressed(1u << GetSize(positions));
	for (int index = 0; index < GetSize(compressed); index++) {
		int full_index = 0;
		for (int bit = 0; bit < GetSize(positions); bit++)
			if (index & (1 << bit))
				full_index |= 1 << positions[bit];
		compressed[index] = table[full_index];
	}
	return compressed;
}

static LutFunction canonical_function(const std::vector<uint8_t> &table, int input_count)
{
	uint32_t support = function_support(table, input_count);
	return {support, compress_function(table, support)};
}

// Two LUTs that differ only by inversion can share one physical LUT when
// every consumer can absorb the inversion into its own truth table.  This is
// common for the cofactors of a multi-output ROM: the data LUT is shared and
// each output mux chooses whether to invert its input bit.
static std::pair<LutFunction, bool> canonical_function_polarity(
		const std::vector<uint8_t> &table, int input_count)
{
	LutFunction key = canonical_function(table, input_count);
	if (!key.first)
		return {key, false};

	LutFunction inverted = key;
	for (auto &value : inverted.second)
		value ^= 1;
	if (inverted.second < key.second)
		return {inverted, true};
	return {key, false};
}

static std::vector<uint8_t> expand_function(const LutFunction &fn, int input_count)
{
	std::vector<int> positions;
	for (int input = 0; input < input_count; input++)
		if (fn.first & (1u << input))
			positions.push_back(input);

	std::vector<uint8_t> table(1u << input_count);
	for (int index = 0; index < GetSize(table); index++) {
		int compressed_index = 0;
		for (int bit = 0; bit < GetSize(positions); bit++)
			if (index & (1 << positions[bit]))
				compressed_index |= 1 << bit;
		table[index] = fn.second[compressed_index];
	}
	return table;
}

static std::vector<uint8_t> cofactor(const std::vector<uint8_t> &table, int input, bool value)
{
	std::vector<uint8_t> result(table.size());
	uint32_t bit = 1u << input;
	for (int index = 0; index < GetSize(table); index++)
		if (!(index & bit))
			result[index] = result[index | bit] = table[index | (value ? bit : 0)];
	return result;
}

static std::vector<int> choose_lut_order(const std::vector<std::vector<uint8_t>> &functions,
                                         int input_count)
{
	std::vector<int> order;
	std::vector<LutFunction> current;
	for (auto &table : functions) {
		LutFunction fn = canonical_function(table, input_count);
		if (!fn.first)
			continue;
		current.push_back(std::move(fn));
	}
	std::sort(current.begin(), current.end());
	current.erase(std::unique(current.begin(), current.end()), current.end());

	std::vector<bool> used(input_count, false);
	while (!current.empty()) {
		int best_input = -1, best_nonconst = INT_MAX, best_unique = INT_MAX, best_users = -1;
		for (int input = 0; input < input_count; input++) {
			if (used[input])
				continue;
			std::vector<LutFunction> next;
			int users = 0;
			for (auto &fn : current) {
				uint32_t bit = 1u << input;
				if (!(fn.first & bit)) {
					next.push_back(fn);
					continue;
				}
				users++;
				std::vector<uint8_t> full = expand_function(fn, input_count);
				for (bool value : {false, true}) {
					next.push_back(canonical_function(cofactor(full, input, value), input_count));
				}
			}
			std::sort(next.begin(), next.end());
			next.erase(std::unique(next.begin(), next.end()), next.end());
			int unique_count = GetSize(next);
			int nonconst_count = 0;
			for (auto &fn : next)
				if (fn.first)
					nonconst_count++;
			if (nonconst_count < best_nonconst ||
			    (nonconst_count == best_nonconst && unique_count < best_unique) ||
			    (nonconst_count == best_nonconst && unique_count == best_unique && users > best_users)) {
				best_input = input;
				best_nonconst = nonconst_count;
				best_unique = unique_count;
				best_users = users;
			}
		}
		if (best_input < 0)
			break;
		used[best_input] = true;
		order.push_back(best_input);

		std::vector<LutFunction> next;
		for (auto &fn : current) {
			uint32_t bit = 1u << best_input;
			if (!(fn.first & bit)) {
				next.push_back(fn);
				continue;
			}
			std::vector<uint8_t> full = expand_function(fn, input_count);
			for (bool value : {false, true}) {
				next.push_back(canonical_function(cofactor(full, best_input, value), input_count));
			}
		}
		std::sort(next.begin(), next.end());
		next.erase(std::unique(next.begin(), next.end()), next.end());
		current = std::move(next);
	}
	for (int input = 0; input < input_count; input++)
		if (!used[input])
			order.push_back(input);
	return order;
}

static int estimate_shannon_luts(const std::vector<uint8_t> &table, int input_count,
                                 const std::vector<int> &order,
                                 std::map<LutFunction, int> &memo)
{
	uint32_t support = function_support(table, input_count);
	if (!support)
		return 0;

	LutFunction key = canonical_function(table, input_count);
	if (memo.count(key))
		return 0;
	memo[key] = 1;
	if (popcount32(support) <= 4)
		return 1;

	int split = -1;
	for (int input : order)
		if (support & (1u << input)) {
			split = input;
			break;
		}
	if (split < 0)
		return 1;
	return 1 + estimate_shannon_luts(cofactor(table, split, false), input_count, order, memo)
	         + estimate_shannon_luts(cofactor(table, split, true), input_count, order, memo);
}

static int estimate_rom_lut_cost(const std::vector<std::vector<uint8_t>> &functions,
                                 int input_count, const std::vector<int> &order)
{
	std::map<LutFunction, int> memo;
	int cost = 0;
	for (auto &table : functions)
		cost += estimate_shannon_luts(table, input_count, order, memo);
	return cost;
}

// For small ROMs, choosing a separate split input for every output is a tiny
// multi-output covering problem: each output contributes its two Shannon
// cofactors, and cofactors that are equal (or complements) share one LUT.
// Exhaustive search is useful here because a globally fixed split order can
// miss a two-LUT saving.  Larger ROMs keep the linear fallback order.
static std::map<LutFunction, int> choose_split_plan(
		const std::vector<std::vector<uint8_t>> &functions,
		int input_count, const std::vector<int> &order, bool verbose)
{
	std::map<LutFunction, int> plan;
	if (input_count > 5)
		return plan;

	struct SplitItem {
		LutFunction key;
		std::vector<std::pair<int, std::vector<int>>> options;
	};

	std::map<LutFunction, int> node_ids;
	std::map<LutFunction, SplitItem> items;
	for (auto &table : functions) {
		auto canonical = canonical_function_polarity(table, input_count);
		if (!canonical.first.first || popcount32(canonical.first.first) <= 4)
			continue;
		if (items.count(canonical.first))
			continue;

		SplitItem item;
		item.key = canonical.first;
		std::vector<uint8_t> expanded = expand_function(item.key, input_count);
		for (int split : order) {
			if (!(item.key.first & (1u << split)))
				continue;
			std::vector<int> ids;
			for (bool value : {false, true}) {
				LutFunction child = canonical_function_polarity(
						cofactor(expanded, split, value), input_count).first;
				if (!child.first)
					continue;
				auto inserted = node_ids.emplace(child, GetSize(node_ids));
				ids.push_back(inserted.first->second);
			}
			std::sort(ids.begin(), ids.end());
			ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
			if (!ids.empty())
				item.options.push_back({split, std::move(ids)});
		}
		if (!item.options.empty())
			items.emplace(item.key, std::move(item));
	}

	if (items.empty())
		return plan;

	std::vector<SplitItem> roots;
	for (auto &entry : items)
		roots.push_back(entry.second);
	std::sort(roots.begin(), roots.end(), [](const SplitItem &a, const SplitItem &b) {
		if (a.options.size() != b.options.size())
			return a.options.size() < b.options.size();
		return a.key < b.key;
	});

	bool exact_search = true;
	long long combinations = 1;
	for (auto &root : roots) {
		if (GetSize(root.options) > 1000000 ||
		    combinations > 100000 / GetSize(root.options)) {
			exact_search = false;
			break;
		}
		combinations *= GetSize(root.options);
	}

	// The exhaustive search is exact but repeated allocations in the recursive
	// visitor make a 10-root, five-choice ROM needlessly expensive.  Keep the
	// exact path for small plans and use a deterministic local search for the
	// large cases.  The local objective is the same: choose one split per root
	// while minimizing the number of distinct cofactor nodes.
	if (!exact_search) {
		constexpr int max_coordinate_passes = 8;
		constexpr int max_pair_passes = 3;
		constexpr long long pair_evaluation_budget = 120000;

		// Reuse one generation-stamped table instead of sorting a fresh vector
		// for every candidate.  A hidden ROM may expose hundreds of outputs.
		std::vector<int> seen(GetSize(node_ids), 0);
		int seen_generation = 0;
		auto plan_cost = [&](const std::vector<int> &choices) {
			if (++seen_generation == INT_MAX) {
				std::fill(seen.begin(), seen.end(), 0);
				seen_generation = 1;
			}
			int cost = 0;
			for (int root = 0; root < GetSize(roots); root++)
				for (int id : roots[root].options[choices[root]].second)
					if (seen[id] != seen_generation) {
						seen[id] = seen_generation;
						cost++;
					}
			return cost;
		};
		auto coordinate_descent = [&](std::vector<int> choices) {
			for (int pass = 0; pass < max_coordinate_passes; pass++) {
				bool changed = false;
				for (int root = 0; root < GetSize(roots); root++) {
					int original_option = choices[root];
					int current_cost = plan_cost(choices);
					int best_option = choices[root];
					for (int option = 0; option < GetSize(roots[root].options); option++) {
						choices[root] = option;
						int option_cost = plan_cost(choices);
						if (option_cost < current_cost) {
							current_cost = option_cost;
							best_option = option;
						}
					}
					choices[root] = best_option;
					changed |= best_option != original_option;
				}
				if (!changed)
					break;
			}
			return choices;
		};
		auto pair_descent = [&](std::vector<int> choices) {
			long long evaluations_left = pair_evaluation_budget;
			for (int pass = 0; pass < max_pair_passes && evaluations_left > 0; pass++) {
				bool changed = false;
				int current_cost = plan_cost(choices);
				std::vector<int> best_choices = choices;
				for (int a = 0; a < GetSize(roots) && evaluations_left > 0; a++)
					for (int b = a + 1; b < GetSize(roots) && evaluations_left > 0; b++)
						for (int option_a = 0; option_a < GetSize(roots[a].options) && evaluations_left > 0; option_a++)
							for (int option_b = 0; option_b < GetSize(roots[b].options) && evaluations_left > 0; option_b++) {
								std::vector<int> trial = choices;
								trial[a] = option_a;
								trial[b] = option_b;
								int trial_cost = plan_cost(trial);
								evaluations_left--;
								if (trial_cost < current_cost) {
									current_cost = trial_cost;
									best_choices = std::move(trial);
								}
							}
				if (current_cost < plan_cost(choices)) {
					choices = std::move(best_choices);
					changed = true;
				}
				if (!changed)
					break;
			}
			return choices;
		};

		std::vector<int> best_choices = coordinate_descent(std::vector<int>(roots.size(), 0));
		long long pair_evaluations = 0;
		for (int a = 0; a < GetSize(roots); a++)
			for (int b = a + 1; b < GetSize(roots); b++)
				pair_evaluations += (long long)GetSize(roots[a].options) * GetSize(roots[b].options);
		bool use_pair_descent = pair_evaluations <= pair_evaluation_budget;
		if (use_pair_descent)
			best_choices = pair_descent(std::move(best_choices));
		int best_cost = plan_cost(best_choices);

		log("pmux_opt: split plan local=%d roots=%d pair=%s pair_candidates=%lld\n",
		    best_cost, GetSize(roots), use_pair_descent ? "on" : "off", pair_evaluations);
		for (int index = 0; index < GetSize(roots); index++) {
			plan[roots[index].key] = roots[index].options[best_choices[index]].first;
			if (verbose)
				log("  split plan: support=%d -> address bit %d (%d nodes, %d choices)\n",
				    popcount32(roots[index].key.first), plan[roots[index].key],
				    best_cost, GetSize(roots[index].options));
		}
		return plan;
	}

	int words = (GetSize(node_ids) + 63) / 64;
	std::vector<uint64_t> used(words, 0), best_used(words, 0);
	std::vector<int> choice(roots.size(), 0), best_choice(roots.size(), 0);
	int best_count = INT_MAX;
	for (auto &root : roots) {
		log("  split candidate support=%d:", popcount32(root.key.first));
		for (auto &option : root.options) {
			log(" [bit%d:", option.first);
			for (int id : option.second)
				log(" %d", id);
			log("]");
		}
		log("\n");
	}

	auto visit = [&](auto &&self, int pos, int count) -> void {
		if (count >= best_count)
			return;
		if (pos == GetSize(roots)) {
			best_count = count;
			best_choice = choice;
			best_used = used;
			return;
		}
		for (int option = 0; option < GetSize(roots[pos].options); option++) {
			choice[pos] = option;
			int next_count = count;
			std::vector<int> added;
			for (int id : roots[pos].options[option].second) {
				int word = id / 64;
				uint64_t mask = uint64_t(1) << (id % 64);
				if (!(used[word] & mask)) {
					used[word] |= mask;
					added.push_back(id);
					next_count++;
				}
			}
			self(self, pos + 1, next_count);
			for (int id : added) {
				int word = id / 64;
				uint64_t mask = uint64_t(1) << (id % 64);
				used[word] &= ~mask;
			}
		}
	};
	visit(visit, 0, 0);

	if (best_count == INT_MAX)
		return plan;
	log("pmux_opt: split plan exact=%d roots=%d\n", best_count, GetSize(roots));
	for (int index = 0; index < GetSize(roots); index++) {
		plan[roots[index].key] = roots[index].options[best_choice[index]].first;
		if (verbose)
			log("  split plan: support=%d -> address bit %d (%d nodes, %d choices)\n",
			    popcount32(roots[index].key.first), plan[roots[index].key],
			    best_count, GetSize(roots[index].options));
	}
	return plan;
}

class ConstantRomSynthesizer
{
	Module *module;
	SigSpec address;
	int input_count;
	std::vector<int> order;
	std::map<LutFunction, int> split_plan;
	std::map<LutFunction, LutNode> memo;

	int choose_split(const LutFunction &key) const
	{
		auto planned = split_plan.find(key);
		if (planned != split_plan.end())
			return planned->second;
		for (int input : order)
			if (key.first & (1u << input))
				return input;
		return -1;
	}

	LutNode make_constant(State value) const
	{
		LutNode node;
		node.is_const = true;
		node.value = value;
		return node;
	}

	LutNode add_lut(const SigSpec &inputs, Const table)
	{
		LutNode node;

		// Yosys 0.69's Intel cells_map.v maps every width-1 $lut to
		// ~A[0], regardless of the LUT parameter.  A width-1 identity LUT
		// would therefore be miscompiled after abc9.  Lower the two
		// possible one-input functions directly here instead of emitting
		// the problematic cell.
		if (GetSize(inputs) == 1 && GetSize(table) == 2) {
			if (table[0] == State::S0 && table[1] == State::S1) {
				node.sig = inputs[0];
				node.width = 1;
				return node;
			}
			if (table[0] == State::S1 && table[1] == State::S0) {
				node.sig = inputs[0];
				node.width = 1;
				node.inverted = true;
				return node;
			}
		}

		node.sig = module->addWire(NEW_ID);
		node.cell = module->addLut(module->uniquify("$pmux_deferred_lut"),
		                           inputs, node.sig, std::move(table));
		node.width = GetSize(inputs);
		return node;
	}

	LutNode merge_children(const LutNode &low, const LutNode &high, int split);

public:
	ConstantRomSynthesizer(Module *module, SigSpec address, int input_count,
	                       std::vector<int> order, std::map<LutFunction, int> split_plan)
	    : module(module), address(std::move(address)), input_count(input_count),
	      order(std::move(order)), split_plan(std::move(split_plan))
	{
	}

	LutNode synthesize(const std::vector<uint8_t> &table);
	LutNode synthesize_gated(const std::vector<uint8_t> &table, SigBit gate, State base);
	LutNode synthesize_gated_wide(const std::vector<uint8_t> &table, SigBit gate, State base);
};

LutNode ConstantRomSynthesizer::merge_children(const LutNode &low, const LutNode &high, int split)
{
	SigSpec inputs;
	std::vector<int> roles;
	if (!low.is_const) {
		inputs.append(low.sig);
		roles.push_back(0);
	}
	if (!high.is_const) {
		inputs.append(high.sig);
		roles.push_back(1);
	}
	inputs.append(address[split]);
	roles.push_back(2);

	Const lut(State::S0, 1 << GetSize(inputs));
	for (int index = 0; index < GetSize(lut); index++) {
		bool low_value = low.is_const ? low.value == State::S1 : false;
		bool high_value = high.is_const ? high.value == State::S1 : false;
		bool select = false;
		for (int pos = 0; pos < GetSize(roles); pos++) {
			bool bit = index & (1 << pos);
			if (roles[pos] == 0)
				low_value = bit;
			else if (roles[pos] == 1)
				high_value = bit;
			else
				select = bit;
		}
		if (low.inverted)
			low_value = !low_value;
		if (high.inverted)
			high_value = !high_value;
		lut.set(index, (select ? high_value : low_value) ? State::S1 : State::S0);
	}
	return add_lut(inputs, std::move(lut));
}

LutNode ConstantRomSynthesizer::synthesize(const std::vector<uint8_t> &table)
{
	uint32_t support = function_support(table, input_count);
	if (!support)
		return make_constant(table[0] ? State::S1 : State::S0);

	std::pair<LutFunction, bool> canonical = canonical_function_polarity(table, input_count);
	LutFunction key = canonical.first;
	auto found = memo.find(key);
	if (found != memo.end()) {
		LutNode node = found->second;
		node.inverted = node.inverted != canonical.second;
		return node;
	}

	LutNode node;
	if (popcount32(support) <= 4) {
		SigSpec inputs;
		for (int input = 0; input < input_count; input++)
			if (support & (1u << input))
				inputs.append(address[input]);
		Const lut(State::S0, 1 << GetSize(inputs));
		for (int index = 0; index < GetSize(lut); index++)
			lut.set(index, key.second[index] ? State::S1 : State::S0);
		node = add_lut(inputs, std::move(lut));
	} else {
		int split = choose_split(key);
		std::vector<uint8_t> canonical_table = expand_function(key, input_count);
		LutNode low = synthesize(cofactor(canonical_table, split, false));
		LutNode high = synthesize(cofactor(canonical_table, split, true));
		node = merge_children(low, high, split);
	}
	// Cache only the physical, non-inverted LUT.  Polarity is a property of
	// this particular use, not of the shared node stored in the memo table.
	node.inverted = false;
	memo[key] = node;
	node.inverted = canonical.second;
	return node;
}

LutNode ConstantRomSynthesizer::synthesize_gated(const std::vector<uint8_t> &table,
                                                 SigBit gate, State base)
{
	uint32_t support = function_support(table, input_count);
	LutNode data;
	if (!support) {
		data = make_constant(table[0] ? State::S1 : State::S0);
	} else if (popcount32(support) > 4) {
		auto canonical = canonical_function_polarity(table, input_count);
		int split = choose_split(canonical.first);
		LutNode low = synthesize(cofactor(table, split, false));
		LutNode high = synthesize(cofactor(table, split, true));

		SigSpec inputs;
		std::vector<int> roles;
		if (!low.is_const) {
			inputs.append(low.sig);
			roles.push_back(0);
		}
		if (!high.is_const) {
			inputs.append(high.sig);
			roles.push_back(1);
		}
		inputs.append(address[split]);
		roles.push_back(2);
		inputs.append(gate);
		roles.push_back(3);

		Const lut(State::S0, 1 << GetSize(inputs));
		for (int index = 0; index < GetSize(lut); index++) {
			bool low_value = low.is_const ? low.value == State::S1 : false;
			bool high_value = high.is_const ? high.value == State::S1 : false;
			bool select = false, enable = false;
			for (int pos = 0; pos < GetSize(roles); pos++) {
				bool bit = index & (1 << pos);
				if (roles[pos] == 0)
					low_value = bit;
				else if (roles[pos] == 1)
					high_value = bit;
				else if (roles[pos] == 2)
					select = bit;
				else
					enable = bit;
			}
			if (low.inverted)
				low_value = !low_value;
			if (high.inverted)
				high_value = !high_value;
			lut.set(index, (enable ? (select ? high_value : low_value) : base == State::S1)
			                   ? State::S1 : State::S0);
		}
		return add_lut(inputs, std::move(lut));
	} else {
		data = synthesize(table);
	}

	if (data.is_const) {
		State enabled = data.value;
		if (data.inverted)
			enabled = enabled == State::S1 ? State::S0 : State::S1;
		if (enabled == base)
			return make_constant(base);
		Const lut(State::S0, 2);
		lut.set(0, base);
		lut.set(1, enabled);
		SigSpec inputs;
		inputs.append(gate);
		return add_lut(inputs, std::move(lut));
	}

	Const lut(State::S0, 4);
	for (int index = 0; index < 4; index++) {
		bool data_value = index & 1;
		if (data.inverted)
			data_value = !data_value;
		bool enable = index & 2;
		lut.set(index, (enable ? data_value : base == State::S1) ? State::S1 : State::S0);
	}
	SigSpec inputs;
	inputs.append(data.sig);
	inputs.append(gate);
	return add_lut(inputs, std::move(lut));
}

LutNode ConstantRomSynthesizer::synthesize_gated_wide(const std::vector<uint8_t> &table,
                                                       SigBit gate, State base)
{
	uint32_t support = function_support(table, input_count);
	std::vector<int> used_inputs;
	for (int input = 0; input < input_count; input++)
		if (support & (1u << input))
			used_inputs.push_back(input);

	if (used_inputs.empty()) {
		State enabled = table[0] ? State::S1 : State::S0;
		if (enabled == base)
			return make_constant(base);
		Const lut(State::S0, 2);
		lut.set(0, base);
		lut.set(1, enabled);
		return add_lut(SigSpec(gate), std::move(lut));
	}

	SigSpec inputs;
	for (int input : used_inputs)
		inputs.append(address[input]);
	inputs.append(gate);

	Const lut(State::S0, 1 << GetSize(inputs));
	for (int index = 0; index < GetSize(lut); index++) {
		int address_index = 0;
		for (int pos = 0; pos < GetSize(used_inputs); pos++)
			if (index & (1 << pos))
				address_index |= 1 << used_inputs[pos];
		bool enabled = (index >> GetSize(used_inputs)) & 1;
		bool value = enabled ? table[address_index] != 0 : base == State::S1;
		lut.set(index, value ? State::S1 : State::S0);
	}
	return add_lut(inputs, std::move(lut));
}

int lower_constant_roms_to_luts(Module *module, SigMap &sigmap, CellTypes &ct, bool verbose)
{
	int lowered = 0;

	for (auto &mem : Mem::get_selected_memories(module)) {
		if (!mem.wr_ports.empty() || mem.rd_ports.empty())
			continue;
		if (mem.attributes.count(ID::keep))
			continue;
		if (mem.width <= 0 || mem.size <= 0 || mem.start_offset != 0)
			continue;
		if (mem.size > (1 << 8))
			continue;
		if ((mem.size & (mem.size - 1)) != 0)
			continue;

		int addr_bits = ceil_log2(mem.size);
		if (addr_bits > 8 || mem.width > 256)
			continue;
		if ((1 << addr_bits) * mem.width > 16384)
			continue;

		Const init_data = mem.get_init_data();
		if (!init_data.is_fully_def())
			continue;

		bool convertible = true;
		for (auto &port : mem.rd_ports) {
			if (port.removed || port.clk_enable || port.wide_log2 != 0 ||
			    !port.en.is_fully_ones() || !port.arst.is_fully_zero() ||
			    !port.srst.is_fully_zero() || GetSize(port.addr) != addr_bits ||
			    GetSize(port.data) != mem.width) {
				convertible = false;
				break;
			}
		}
		if (!convertible)
			continue;

		bool synthesizable = true;
		for (auto &port : mem.rd_ports) {
			SigSpec addr = sigmap(port.addr);
			pool<SigBit> seen;
			for (auto bit : addr)
				if (bit.wire == nullptr || seen.count(bit)) {
					synthesizable = false;
					break;
				}
			if (!synthesizable)
				break;
		}
		if (!synthesizable) {
			if (verbose)
				log("  constant ROM %s has a constant or aliased address bit; leaving it to memory_map\n",
				    log_id(mem.memid));
			continue;
		}

		std::vector<std::vector<uint8_t>> output_functions(mem.width);
		for (int bit = 0; bit < mem.width; bit++) {
			output_functions[bit].resize(1 << addr_bits);
			for (int addr = 0; addr < mem.size; addr++)
				output_functions[bit][addr] = init_data[addr * mem.width + bit] == State::S1;
		}

		bool needs_decomposition = false, all_cheap = true;
		for (auto &table : output_functions) {
			if (popcount32(function_support(table, addr_bits)) > 4) {
				needs_decomposition = true;
			}
			if (!function_is_cheap(table))
				all_cheap = false;
		}
		if (!needs_decomposition || all_cheap) {
			if (verbose)
				log("  constant ROM %s is already cheap for memory_map/ABC; leaving it unchanged\n",
				    log_id(mem.memid));
			continue;
		}
		// Above 128 outputs the fixed Shannon fallback is too coarse: without
		// the <=5-bit split-plan path it can freeze poor cuts and lose to ABC's
		// global mapping.  Keep those wide memories in the normal flow.
		if (mem.width > 128 && addr_bits > 5) {
			if (verbose)
				log("  constant ROM %s has %d outputs without a split plan; leaving it to ABC\n",
				    log_id(mem.memid), mem.width);
			continue;
		}
		int64_t timing_start = PerformanceTimer::query();
		std::vector<int> lut_order = choose_lut_order(output_functions, addr_bits);
		int64_t timing_order = PerformanceTimer::query();
		std::map<LutFunction, int> split_plan = choose_split_plan(output_functions, addr_bits, lut_order, verbose);
		int64_t timing_split = PerformanceTimer::query();
		int estimated_luts = estimate_rom_lut_cost(output_functions, addr_bits, lut_order);
		int64_t timing_estimate = PerformanceTimer::query();
		if (verbose)
			log("  constant ROM timing: order=%.3f ms split=%.3f ms estimate=%.3f ms\n",
			    (timing_order - timing_start) / 1e6,
			    (timing_split - timing_order) / 1e6,
			    (timing_estimate - timing_split) / 1e6);
		if (estimated_luts <= 2 * mem.width) {
			if (verbose)
				log("  constant ROM %s estimated at %d LUTs, not enough margin over %d outputs; leaving it unchanged\n",
				    log_id(mem.memid), estimated_luts, mem.width);
			continue;
		}

		dict<SigBit, std::vector<InputLoad>> bit2loads;
		for (auto cell : module->cells())
			for (auto &conn : cell->connections()) {
				if (ct.cell_output(cell->type, conn.first))
					continue;
				if (cell->type.in(ID($memrd), ID($memrd_v2)) && conn.first == ID::DATA)
					continue;
				if (cell->type == ID($mem_v2) && conn.first == ID::RD_DATA)
					continue;
				int offset = 0;
				for (auto bit : sigmap(conn.second))
					bit2loads[bit].push_back({cell, conn.first, offset++});
			}

		for (auto &port : mem.rd_ports) {
			if (addr_bits == 0) {
				module->connect(port.data, init_data.extract(0, mem.width));
				continue;
			}

			SigSpec port_addr = sigmap(port.addr);
			ConstantRomSynthesizer synthesizer(module, port_addr, addr_bits, lut_order, split_plan);

			dict<SigBit, int> rom_bit_index;
			for (int bit = 0; bit < mem.width; bit++)
				rom_bit_index[sigmap(port.data[bit])] = bit;
			if (verbose)
				log("  constant ROM %s port has %d mapped data bit(s), %d loads\n",
				    log_id(mem.memid), GetSize(rom_bit_index), GetSize(bit2loads));

			pool<int> folded_bits;
			std::vector<Cell *> folded_muxes;
			std::vector<Cell *> candidates;
			for (auto cell : module->cells())
				if (cell->type == ID($mux))
					candidates.push_back(cell);
			if (verbose)
				log("  considered %d mux candidate(s)\n", GetSize(candidates));
			for (auto cell : candidates) {
				if (cell->type != ID($mux))
					continue;
				int width = GetSize(cell->getPort(ID::Y));
				if (width < 1 || GetSize(cell->getPort(ID::A)) != width ||
				    GetSize(cell->getPort(ID::B)) != width ||
				    GetSize(cell->getPort(ID::S)) != 1)
					continue;
				SigSpec a_data = sigmap(cell->getPort(ID::A));
				if (!a_data.is_fully_const())
					continue;

				std::vector<int> indices;
				pool<int> unique;
				bool convertible = true;
				SigSpec b_data = sigmap(cell->getPort(ID::B));
				bool touches_rom = false;
				for (auto bit : b_data)
					if (rom_bit_index.count(bit))
						touches_rom = true;
				for (int bit = 0; bit < width && convertible; bit++) {
					auto it = rom_bit_index.find(b_data[bit]);
					if (it == rom_bit_index.end() || folded_bits.count(it->second) ||
					    unique.count(it->second)) {
						if (verbose && touches_rom)
							log("  cannot fold mux %s bit %d: rom index unavailable/duplicate\n",
							    log_id(cell->name), bit);
						convertible = false;
						break;
					}
					unique.insert(it->second);
					auto load_it = bit2loads.find(b_data[bit]);
					if (load_it == bit2loads.end() || load_it->second.size() != 1 ||
					    load_it->second[0].cell != cell || load_it->second[0].port != ID::B ||
					    load_it->second[0].offset != bit) {
						if (verbose && touches_rom)
							log("  cannot fold mux %s bit %d: loads=%d first=%s/%s/%d\n",
							    log_id(cell->name), bit,
							    load_it == bit2loads.end() ? -1 : GetSize(load_it->second),
							    load_it == bit2loads.end() || load_it->second.empty() ? "" : log_id(load_it->second[0].cell->name),
							    load_it == bit2loads.end() || load_it->second.empty() ? "" : log_id(load_it->second[0].port),
							    load_it == bit2loads.end() || load_it->second.empty() ? -1 : load_it->second[0].offset);
						if (verbose && load_it != bit2loads.end())
							for (auto &load : load_it->second)
								log("    load from %s/%s/%d\n", log_id(load.cell->name),
								    log_id(load.port), load.offset);
						convertible = false;
						break;
					}
					indices.push_back(it->second);
				}
				if (!convertible)
					continue;

				bool fold_into_dff_clear = a_data.is_fully_zero();
				if (fold_into_dff_clear) {
					for (int bit = 0; bit < width; bit++) {
						auto load_it = bit2loads.find(sigmap(cell->getPort(ID::Y)[bit]));
						if (load_it == bit2loads.end() || GetSize(load_it->second) != 1 ||
						    load_it->second[0].port != ID::D ||
						    !load_it->second[0].cell->type.in(ID($dff), ID($adff))) {
							fold_into_dff_clear = false;
							break;
						}
					}
				}

				SigSpec lut_output = module->addWire(NEW_ID, width);
				SigBit select = sigmap(cell->getPort(ID::S))[0];
				for (int bit = 0; bit < width; bit++) {
					LutNode node = fold_into_dff_clear
					                   ? synthesizer.synthesize_gated_wide(
					                         output_functions[indices[bit]], select, a_data[bit].data)
					                   : synthesizer.synthesize_gated(
					                         output_functions[indices[bit]], select, a_data[bit].data);
					if (node.is_const)
						module->connect(lut_output[bit], Const(node.value, 1));
					else
						module->connect(lut_output[bit], node.sig);
					folded_bits.insert(indices[bit]);
				}
				module->connect(cell->getPort(ID::Y), lut_output);
				folded_muxes.push_back(cell);

				if (verbose)
					log("  folded enable mux %s into constant ROM %s\n",
					    log_id(cell->name), log_id(mem.memid));
			}
			for (auto cell : folded_muxes)
				module->remove(cell);

			for (int bit = 0; bit < mem.width; bit++) {
				if (folded_bits.count(bit))
					continue;
					LutNode node = synthesizer.synthesize(output_functions[bit]);
					if (node.is_const)
						module->connect(port.data[bit], Const(node.value, 1));
					else {
						SigBit value = node.sig;
						if (node.inverted) {
							SigBit inverted = module->addWire(NEW_ID);
							module->addNot(NEW_ID, value, inverted);
							value = inverted;
						}
						module->connect(port.data[bit], value);
					}
				}
		}

		if (verbose)
			log("  lowered constant ROM %s: %d x %d -> %d LUT input(s)\n",
			    log_id(mem.memid), mem.size, mem.width, addr_bits);

		mem.remove();
		lowered++;
	}

	return lowered;
}

// dfflegalize implements a high-reset FF by keeping the complemented state
// and inserting inverters on both sides of the register.  The input inverter
// can be absorbed for free when it is the only load of a LUT: complementing
// the LUT truth table changes no other user and leaves the register's reset
// semantics untouched.
pool<SigBit> collect_output_bits(Module *module, SigMap &sigmap);
SigBit make_not(Module *module, SigBit bit);
SigBit make_or(Module *module, const std::vector<SigBit> &inputs);

bool combinational_depends_on(SigBit target, SigBit start, SigMap &sigmap, CellTypes &ct,
                              const dict<SigBit, Cell *> &bit_driver)
{
	pool<SigBit> visited;
	std::vector<SigBit> pending = {start};
	while (!pending.empty()) {
		SigBit bit = sigmap(pending.back());
		pending.pop_back();
		if (!visited.insert(bit).second)
			continue;
		if (bit == target)
			return true;

		auto it = bit_driver.find(bit);
		if (it == bit_driver.end())
			continue;
		Cell *driver = it->second;
		if (driver->is_builtin_ff())
			continue;
		for (auto &conn : driver->connections())
			if (ct.cell_input(driver->type, conn.first))
				for (auto input : sigmap(conn.second))
					pending.push_back(input);
	}
	return false;
}

int fold_not_into_luts(Module *module, CellTypes &ct, bool verbose)
{
	int folded = 0;

	while (true) {
		SigMap sigmap(module);
		pool<SigBit> output_bits = collect_output_bits(module, sigmap);
		dict<SigBit, int> loads;
		dict<SigBit, Cell *> lut_driver;

		for (auto cell : module->cells()) {
			for (auto &conn : cell->connections()) {
				if (ct.cell_input(cell->type, conn.first))
					for (auto bit : sigmap(conn.second))
						loads[bit]++;
				if (cell->type == ID($lut) && ct.cell_output(cell->type, conn.first))
					for (auto bit : sigmap(conn.second))
						lut_driver[bit] = cell;
			}
		}

		Cell *candidate = nullptr;
		Cell *driver = nullptr;
		for (auto cell : module->cells()) {
			if (!cell->type.in(ID($not), ID($_NOT_)))
				continue;
			if (GetSize(cell->getPort(ID::A)) != 1 || GetSize(cell->getPort(ID::Y)) != 1)
				continue;

			SigBit input = sigmap(cell->getPort(ID::A)[0]);
			if (output_bits.count(input) || loads[input] != 1)
				continue;

			auto it = lut_driver.find(input);
			if (it == lut_driver.end())
				continue;

			Cell *lut = it->second;
			if (GetSize(lut->getPort(ID::Y)) != 1)
				continue;

			Const table = lut->getParam(ID::LUT);
			int width = lut->getParam(ID::WIDTH).as_int();
			if (!table.is_fully_def() || GetSize(table) != (1 << width) ||
			    GetSize(lut->getPort(ID::A)) != width)
				continue;

			candidate = cell;
			driver = lut;
			break;
		}

		if (candidate == nullptr)
			break;

		Const table = driver->getParam(ID::LUT);
		std::vector<State> table_bits = table.to_bits();
		for (auto &bit : table_bits)
			bit = bit == State::S1 ? State::S0 : State::S1;
		driver->setParam(ID::LUT, Const(std::move(table_bits)));

		if (verbose)
			log("  folded %s into LUT %s\n", log_id(candidate->name), log_id(driver->name));

		module->connect(candidate->getPort(ID::Y), driver->getPort(ID::Y));
		module->remove(candidate);
		folded++;
	}

	return folded;
}

// A technology flip-flop can absorb an inverted clock enable without spending
// a LUT: hold the register by synchronously loading its own Q, and enable that
// load with the un-inverted signal.  This is only profitable when the inverter
// exclusively drives DFF enables, so a shared inverter disappears completely.
int fold_inverted_enable_into_dff(Module *module, CellTypes &ct, bool verbose)
{
	int folded = 0;
	const IdString port_q = RTLIL::escape_id("q");
	const IdString port_ena = RTLIL::escape_id("ena");
	const IdString port_asdata = RTLIL::escape_id("asdata");
	const IdString port_sload = RTLIL::escape_id("sload");
	const IdString port_sclr = RTLIL::escape_id("sclr");
	const IdString port_aload = RTLIL::escape_id("aload");

	while (true) {
		SigMap sigmap(module);
		pool<SigBit> output_bits = collect_output_bits(module, sigmap);
		dict<SigBit, int> loads;
		dict<SigBit, int> ena_loads;

		for (auto cell : module->cells())
			for (auto &conn : cell->connections()) {
				bool is_input = cell->type == ID(dffeas) ? conn.first != port_q
				                                         : ct.cell_input(cell->type, conn.first);
				if (!is_input)
					continue;
				for (auto bit : sigmap(conn.second)) {
					loads[bit]++;
					if (cell->type == ID(dffeas) && conn.first == port_ena)
						ena_loads[bit]++;
				}
			}

		Cell *candidate = nullptr;
		std::vector<Cell *> targets;
		SigBit inverter_input;

		for (auto cell : module->cells()) {
			if (!cell->type.in(ID($not), ID($_NOT_)))
				continue;
			if (GetSize(cell->getPort(ID::A)) != 1 || GetSize(cell->getPort(ID::Y)) != 1)
				continue;

			SigBit output = sigmap(cell->getPort(ID::Y)[0]);
			if (output_bits.count(output) || loads[output] == 0 ||
			    loads[output] != ena_loads[output])
				continue;

			std::vector<Cell *> local_targets;
			bool ok = true;
			for (auto dff : module->cells()) {
				if (dff->type != ID(dffeas) || !dff->hasPort(port_ena) ||
				    GetSize(dff->getPort(port_ena)) != 1 ||
				    sigmap(dff->getPort(port_ena)[0]) != output)
					continue;
				if (dff->get_bool_attribute(ID(pmux_rom_enable)))
					continue;
				if (!dff->hasPort(port_q) || GetSize(dff->getPort(port_q)) != 1 ||
				    !dff->hasPort(port_asdata) || !dff->hasPort(port_sload) ||
				    !dff->hasPort(port_sclr) || !dff->hasPort(port_aload) ||
				    GetSize(dff->getPort(port_sload)) != 1 ||
				    GetSize(dff->getPort(port_sclr)) != 1 ||
				    GetSize(dff->getPort(port_aload)) != 1 ||
				    sigmap(dff->getPort(port_sload)[0]) != State::S0 ||
				    sigmap(dff->getPort(port_sclr)[0]) != State::S0 ||
				    sigmap(dff->getPort(port_aload)[0]) != State::S0) {
					ok = false;
					break;
				}
				local_targets.push_back(dff);
			}

			if (!ok || local_targets.empty())
				continue;
			candidate = cell;
			targets = std::move(local_targets);
			inverter_input = sigmap(cell->getPort(ID::A)[0]);
			break;
		}

		if (candidate == nullptr)
			break;

		for (auto dff : targets) {
			dff->setPort(port_ena, State::S1);
			dff->setPort(port_sload, inverter_input);
			dff->setPort(port_asdata, dff->getPort(port_q));
			if (verbose)
				log("  moved inverted enable %s into DFF %s\n",
				    log_id(candidate->name), log_id(dff->name));
		}
		module->remove(candidate);
		folded += GetSize(targets);
	}

	return folded;
}

// A DFF next-state cone can contain a shared zero-gate, for example
// next = enable ? rom_data : 0.  Move that shared gate to the DFF synchronous
// clear and leave the ROM cone unconstrained for ABC.  This is structurally
// better than embedding the enable in every output LUT: it avoids forcing an
// early LUT decomposition of the ROM and lets ABC choose the cuts globally.
int fold_zero_gate_into_dff_clear(Module *module, CellTypes &ct, bool verbose)
{
	struct Option {
		Cell *dff;
		Cell *gate;
		SigBit control;
		SigBit data;
	};

	const IdString port_d = RTLIL::escape_id("d");
	const IdString port_ena = RTLIL::escape_id("ena");
	const IdString port_sclr = RTLIL::escape_id("sclr");
	const IdString port_sload = RTLIL::escape_id("sload");
	const IdString port_aload = RTLIL::escape_id("aload");

	SigMap sigmap(module);
	dict<SigBit, Cell *> bit_driver;
	for (auto cell : module->cells())
		for (auto &conn : cell->connections())
			if (ct.cell_output(cell->type, conn.first))
				for (auto bit : sigmap(conn.second))
					bit_driver[bit] = cell;

	dict<SigBit, std::vector<Option>> options_by_control;
	for (auto dff : module->cells()) {
		if (dff->type != ID(dffeas) || !dff->hasPort(port_d) ||
		    !dff->hasPort(port_ena) || !dff->hasPort(port_sclr) ||
		    !dff->hasPort(port_sload) || !dff->hasPort(port_aload) ||
		    GetSize(dff->getPort(port_d)) != 1 || GetSize(dff->getPort(port_ena)) != 1 ||
		    GetSize(dff->getPort(port_sclr)) != 1 || GetSize(dff->getPort(port_sload)) != 1 ||
		    GetSize(dff->getPort(port_aload)) != 1 ||
		    sigmap(dff->getPort(port_ena)[0]) != State::S1 ||
		    sigmap(dff->getPort(port_sclr)[0]) != State::S0 ||
		    sigmap(dff->getPort(port_sload)[0]) != State::S0 ||
		    sigmap(dff->getPort(port_aload)[0]) != State::S0)
			continue;
		if (dff->get_bool_attribute(ID(pmux_rom_enable)))
			continue;

		SigBit d = sigmap(dff->getPort(port_d)[0]);
		auto driver_it = bit_driver.find(d);
		if (driver_it == bit_driver.end())
			continue;
		Cell *gate = driver_it->second;
		if (!gate->type.in(ID($and), ID($_AND_)) ||
		    GetSize(gate->getPort(ID::A)) != 1 || GetSize(gate->getPort(ID::B)) != 1 ||
		    GetSize(gate->getPort(ID::Y)) != 1)
			continue;

		SigBit a = sigmap(gate->getPort(ID::A)[0]);
		SigBit b = sigmap(gate->getPort(ID::B)[0]);
		options_by_control[a].push_back({dff, gate, a, b});
		options_by_control[b].push_back({dff, gate, b, a});
	}

	std::vector<std::pair<SigBit, std::vector<Option>>> groups;
	for (auto &entry : options_by_control)
		groups.push_back(entry);
	std::sort(groups.begin(), groups.end(),
	          [](const std::pair<SigBit, std::vector<Option>> &a,
	             const std::pair<SigBit, std::vector<Option>> &b) {
		          if (a.second.size() != b.second.size())
			          return a.second.size() > b.second.size();
		          return a.first < b.first;
	          });

	pool<Cell *> assigned;
	int folded = 0;
	for (auto &entry : groups) {
		std::vector<Option> selected;
		pool<Cell *> seen_dffs;
		for (auto &option : entry.second) {
			if (assigned.count(option.dff) || !seen_dffs.insert(option.dff).second)
				continue;
			selected.push_back(option);
		}
		if (GetSize(selected) < 2)
			continue;

		SigBit synchronous_clear = make_not(module, entry.first);
		for (auto &option : selected) {
			option.dff->setPort(port_d, option.data);
			option.dff->setPort(port_sclr, synchronous_clear);
			assigned.insert(option.dff);
			folded++;
			if (verbose)
				log("  moved shared zero-gate %s into DFF %s synchronous clear\n",
				    log_signal(entry.first), log_id(option.dff->name));
		}
	}

	return folded;
}

// proc represents a synchronous reset in a dedicated register cell before
// technology mapping.  The Cyclone IV dffeas primitive already has both an
// enable and a synchronous clear, so lowering an enabled RTL cell directly
// avoids spending LUT inputs on the combined enable/reset condition.  Plain
// $sdff cells are deliberately left to dfflegalize: they have no independent
// enable to preserve, and splitting a wide reset group into one target FF per
// bit destroys sharing that ABC otherwise exploits (observed on PicoRV32).
// CEX over SRST is deliberately left for the normal dfflegalize path.  Only
// zero resets and registers without an init value are accepted so the rewrite
// cannot silently change state.
int lower_sync_reset_dffs_to_target(Module *module, bool verbose)
{
	bool has_target = module->design != nullptr && module->design->module(ID(dffeas)) != nullptr;
	if (verbose)
		log("  synchronous-reset scan: target=%d\n", int(has_target));
	if (!has_target)
		return 0;

	const IdString port_d = RTLIL::escape_id("d");
	const IdString port_q = RTLIL::escape_id("q");
	const IdString port_clk = RTLIL::escape_id("clk");
	const IdString port_clrn = RTLIL::escape_id("clrn");
	const IdString port_prn = RTLIL::escape_id("prn");
	const IdString port_ena = RTLIL::escape_id("ena");
	const IdString port_asdata = RTLIL::escape_id("asdata");
	const IdString port_aload = RTLIL::escape_id("aload");
	const IdString port_sclr = RTLIL::escape_id("sclr");
	const IdString port_sload = RTLIL::escape_id("sload");

	SigMap sigmap(module);
	std::vector<Cell *> candidates;
	for (auto cell : module->cells()) {
		if (cell->type != ID($sdffce) ||
		    !cell->hasParam(ID::WIDTH) || !cell->hasParam(ID::CLK_POLARITY) ||
		    !cell->hasParam(ID::SRST_POLARITY) || !cell->hasParam(ID::SRST_VALUE) ||
		    !cell->hasPort(ID::D) || !cell->hasPort(ID::Q) ||
		    !cell->hasPort(ID::CLK) || !cell->hasPort(ID::SRST))
			continue;

		int width = cell->getParam(ID::WIDTH).as_int();
		if (width <= 0 || GetSize(cell->getPort(ID::D)) != width ||
		    GetSize(cell->getPort(ID::Q)) != width ||
		    GetSize(cell->getPort(ID::CLK)) != 1 ||
		    GetSize(cell->getPort(ID::SRST)) != 1 ||
		    GetSize(cell->getParam(ID::SRST_VALUE)) != width)
			continue;

		if (cell->type == ID($sdffce) &&
		    (!cell->hasParam(ID::EN_POLARITY) || !cell->hasPort(ID::EN) ||
		     GetSize(cell->getPort(ID::EN)) != 1))
			continue;

		Const reset_values = cell->getParam(ID::SRST_VALUE);
		bool zero_reset = true;
		for (auto value : reset_values)
			if (value != State::S0) {
				zero_reset = false;
				break;
			}
		if (!zero_reset)
			continue;

		bool has_init = false;
		for (int bit = 0; bit < width && !has_init; bit++)
			if (cell->getPort(ID::Q)[bit].wire != nullptr &&
			    cell->getPort(ID::Q)[bit].wire->attributes.count(ID::init))
				has_init = true;
		if (!has_init)
			candidates.push_back(cell);
	}

	int lowered = 0;
	for (auto cell : candidates) {
		if (!module->cells_.count(cell->name))
			continue;

		int width = cell->getParam(ID::WIDTH).as_int();
		SigBit clock = sigmap(cell->getPort(ID::CLK))[0];
		if (!cell->getParam(ID::CLK_POLARITY).as_bool())
			clock = make_not(module, clock);

		SigBit synchronous_clear = sigmap(cell->getPort(ID::SRST))[0];
		if (!cell->getParam(ID::SRST_POLARITY).as_bool())
			synchronous_clear = make_not(module, synchronous_clear);

		SigBit enable = State::S1;
		if (cell->type == ID($sdffce)) {
			enable = sigmap(cell->getPort(ID::EN))[0];
			if (!cell->getParam(ID::EN_POLARITY).as_bool())
				enable = make_not(module, enable);
		}

		SigSpec data = cell->getPort(ID::D);
		SigSpec output = cell->getPort(ID::Q);
		for (int bit = 0; bit < width; bit++) {
			Cell *ff = module->addCell(NEW_ID, ID(dffeas));
			ff->setParam(ID(is_wysiwyg), Const("TRUE"));
			ff->setPort(port_q, sigmap(output)[bit]);
			ff->setPort(port_d, data[bit]);
			ff->setPort(port_clk, clock);
			ff->setPort(port_clrn, State::S1);
			ff->setPort(port_prn, State::S1);
			ff->setPort(port_ena, enable);
			ff->setPort(port_asdata, State::S0);
			ff->setPort(port_aload, State::S0);
			ff->setPort(port_sclr, synchronous_clear);
			ff->setPort(port_sload, State::S0);
			lowered++;
		}
		if (verbose)
			log("  lowered synchronous-reset DFF %s (%d bit(s)) to target clear\n",
			    log_id(cell->name), width);
		module->remove(cell);
	}

	return lowered;
}

// Before memory_map, an enabled register update is still represented as a
// zero-data mux feeding a plain/async-reset DFF.  On Cyclone IV the target FF
// has both asynchronous clear/set and a synchronous clear.  Move the shared
// zero condition to SCLR and connect the DFF directly to the ROM read data.
// The ROM then reaches memory_map untouched, leaving one complete mux tree for
// ABC instead of a fixed, prematurely decomposed LUT network.
int fold_zero_mux_into_target_dff_clear(Module *module, bool verbose)
{
	bool has_target = module->design != nullptr && module->design->module(ID(dffeas)) != nullptr;
	if (verbose)
		log("  target-clear scan: target=%d\n", int(has_target));
	if (!has_target)
		return 0;

	const IdString port_d = RTLIL::escape_id("d");
	const IdString port_q = RTLIL::escape_id("q");
	const IdString port_clk = RTLIL::escape_id("clk");
	const IdString port_clrn = RTLIL::escape_id("clrn");
	const IdString port_prn = RTLIL::escape_id("prn");
	const IdString port_ena = RTLIL::escape_id("ena");
	const IdString port_asdata = RTLIL::escape_id("asdata");
	const IdString port_aload = RTLIL::escape_id("aload");
	const IdString port_sclr = RTLIL::escape_id("sclr");
	const IdString port_sload = RTLIL::escape_id("sload");

	struct BitBinding {
		Cell *dff;
		int index;
		SigBit data;
	};

	SigMap sigmap(module);
	dict<SigBit, std::vector<InputLoad>> loads;
	for (auto cell : module->cells())
		for (auto &conn : cell->connections())
			if (cell->input(conn.first)) {
				int offset = 0;
				for (auto bit : sigmap(conn.second))
					loads[bit].push_back({cell, conn.first, offset++});
			}

	std::vector<Cell *> muxes;
	for (auto cell : module->cells())
		if (cell->type == ID($mux))
			muxes.push_back(cell);
	if (verbose)
		log("  target-clear scan: %d mux cell(s)\n", GetSize(muxes));

	// A large memory read is represented as one shared mux tree.  Folding its
	// output enable into DFF clear too early prevents memory_map/ABC from
	// choosing cuts over the complete ROM cone.  Small tables do not suffer
	// from that global-cutting effect and still benefit from target clear.
	// Keep this decision structural: 128 or more entries means ABITS >= 7.
	constexpr int large_rom_addr_bits = 7;
	dict<SigBit, int> memory_addr_bits;
	for (auto cell : module->cells()) {
		IdString data_port;
		if (cell->type.in(ID($memrd), ID($memrd_v2)))
			data_port = ID::DATA;
		else if (cell->type == ID($mem_v2))
			data_port = ID::RD_DATA;
		else
			continue;
		if (!cell->hasPort(data_port))
			continue;
		if (!cell->hasParam(ID::ABITS))
			continue;
		int addr_bits = cell->getParam(ID::ABITS).as_int();
		for (auto bit : sigmap(cell->getPort(data_port)))
			memory_addr_bits[bit] = std::max(memory_addr_bits.count(bit) ? memory_addr_bits[bit] : 0,
			                                 addr_bits);
	}

	pool<Cell *> consumed_dffs;
	dict<SigBit, SigBit> inverted_resets;
	int folded = 0;
	for (auto mux : muxes) {
		if (!module->cells_.count(mux->name) || !mux->hasPort(ID::A) ||
		    !mux->hasPort(ID::B) || !mux->hasPort(ID::S) || !mux->hasPort(ID::Y))
			continue;
		int width = GetSize(mux->getPort(ID::Y));
		if (verbose)
			log("    mux %s width=%d A=%s B=%s S=%s Y=%s\n", log_id(mux->name), width,
			    log_signal(sigmap(mux->getPort(ID::A))), log_signal(sigmap(mux->getPort(ID::B))),
			    log_signal(sigmap(mux->getPort(ID::S))), log_signal(sigmap(mux->getPort(ID::Y))));
		if (width < 2 || GetSize(mux->getPort(ID::A)) != width ||
		    GetSize(mux->getPort(ID::B)) != width ||
		    GetSize(mux->getPort(ID::S)) != 1)
			continue;

		bool zero_on_a = sigmap(mux->getPort(ID::A)).is_fully_zero();
		bool zero_on_b = sigmap(mux->getPort(ID::B)).is_fully_zero();
		if (!zero_on_a && !zero_on_b)
			continue;
		SigSpec data = zero_on_a ? mux->getPort(ID::B) : mux->getPort(ID::A);
		int memory_addr_width = 0;
		for (auto bit : sigmap(data))
			if (memory_addr_bits.count(bit))
				memory_addr_width = std::max(memory_addr_width, memory_addr_bits[bit]);
		if (memory_addr_width >= large_rom_addr_bits) {
			if (verbose)
				log("  zero mux %s drives a %d-bit-address memory read; leaving ROM cone to "
				    "memory_map/ABC\n",
				    log_id(mux->name), memory_addr_width);
			pool<Cell *> rom_dffs;
			for (int bit = 0; bit < width; bit++) {
				auto load_it = loads.find(sigmap(mux->getPort(ID::Y)[bit]));
				if (load_it == loads.end() || GetSize(load_it->second) != 1 ||
				    load_it->second[0].port != ID::D)
					continue;
				Cell *dff = load_it->second[0].cell;
				if (dff->type.in(ID($dff), ID($adff), ID($dffe), ID($adffe)))
					rom_dffs.insert(dff);
			}
			for (auto dff : rom_dffs)
				dff->set_bool_attribute(ID(pmux_rom_enable));
			continue;
		}

		dict<Cell *, std::vector<BitBinding>> groups;
		dict<Cell *, pool<int>> used_indices;
		pool<SigBit> used_output_bits;
		bool supported = true;
		for (int bit = 0; bit < width && supported; bit++) {
			SigBit y = sigmap(mux->getPort(ID::Y)[bit]);
			if (!used_output_bits.insert(y).second) {
				supported = false;
				break;
			}
			auto load_it = loads.find(y);
			if (verbose && (load_it == loads.end() || GetSize(load_it->second) != 1))
				log("      bit %d load_count=%d\n", bit,
				    load_it == loads.end() ? 0 : GetSize(load_it->second));
			if (load_it == loads.end() || GetSize(load_it->second) != 1 ||
			    load_it->second[0].port != ID::D) {
				supported = false;
				break;
			}
			Cell *dff = load_it->second[0].cell;
			if (!dff->type.in(ID($dff), ID($adff), ID($dffe), ID($adffe)) ||
			    consumed_dffs.count(dff) ||
			    !dff->hasPort(ID::D) || !dff->hasPort(ID::Q) ||
			    !dff->hasPort(ID::CLK) || !dff->hasParam(ID::WIDTH) ||
			    !dff->hasParam(ID::CLK_POLARITY)) {
				supported = false;
				break;
			}
			int dff_width = dff->getParam(ID::WIDTH).as_int();
			if (dff_width <= 0 || GetSize(dff->getPort(ID::D)) != dff_width ||
			    GetSize(dff->getPort(ID::Q)) != dff_width ||
			    GetSize(dff->getPort(ID::CLK)) != 1) {
				supported = false;
				break;
			}
			if (dff->type.in(ID($adff), ID($adffe)) &&
			    (!dff->hasPort(ID::ARST) || !dff->hasParam(ID::ARST_POLARITY) ||
			     !dff->hasParam(ID::ARST_VALUE) ||
			     (GetSize(dff->getPort(ID::ARST)) != 1 &&
			      GetSize(dff->getPort(ID::ARST)) != dff_width) ||
			     GetSize(dff->getParam(ID::ARST_VALUE)) != dff_width)) {
				supported = false;
				break;
			}
			if (dff->type.in(ID($adff), ID($adffe))) {
				Const values = dff->getParam(ID::ARST_VALUE);
				for (auto value : values)
					if (value != State::S0 && value != State::S1) {
						supported = false;
						break;
					}
				if (!supported)
					break;
			}
			if (dff->type.in(ID($dffe), ID($adffe)) &&
			    (!dff->hasPort(ID::EN) || !dff->hasParam(ID::EN_POLARITY) ||
			     GetSize(dff->getPort(ID::EN)) != 1)) {
				supported = false;
				break;
			}

			int index = -1;
			for (int j = 0; j < dff_width; j++)
				if (sigmap(dff->getPort(ID::D)[j]) == y) {
					index = j;
					break;
				}
			if (index < 0 || !used_indices[dff].insert(index).second) {
				supported = false;
				break;
			}
			groups[dff].push_back({dff, index, data[bit]});
		}
		if (!supported)
			continue;

		for (auto &entry : groups) {
			Cell *dff = entry.first;
			int dff_width = dff->getParam(ID::WIDTH).as_int();
			if (GetSize(entry.second) != dff_width ||
			    GetSize(used_indices[dff]) != dff_width) {
				supported = false;
				break;
			}
			for (int bit = 0; bit < dff_width; bit++)
				if (dff->getPort(ID::Q)[bit].wire != nullptr &&
				    dff->getPort(ID::Q)[bit].wire->attributes.count(ID::init)) {
					supported = false;
					break;
				}
			if (!supported)
				break;
		}
		if (!supported)
			continue;

		SigBit zero_select = sigmap(mux->getPort(ID::S))[0];
		SigBit synchronous_clear = zero_on_a ? make_not(module, zero_select) : zero_select;
		for (auto &entry : groups) {
			Cell *dff = entry.first;
			bool clk_positive = dff->getParam(ID::CLK_POLARITY).as_bool();
			SigBit clock = sigmap(dff->getPort(ID::CLK))[0];
			if (!clk_positive)
				clock = make_not(module, clock);

			SigBit load_enable = State::S1;
			if (dff->type.in(ID($dffe), ID($adffe))) {
				SigBit enable = sigmap(dff->getPort(ID::EN))[0];
				if (!dff->getParam(ID::EN_POLARITY).as_bool())
					enable = make_not(module, enable);
				load_enable = enable;
			}

			Const reset_values;
			SigSpec reset_signal;
			bool reset_positive = false;
			if (dff->type.in(ID($adff), ID($adffe))) {
				reset_values = dff->getParam(ID::ARST_VALUE);
				reset_signal = dff->getPort(ID::ARST);
				reset_positive = dff->getParam(ID::ARST_POLARITY).as_bool();
			}

			for (auto &binding : entry.second) {
				int bit = binding.index;
				SigBit clrn = State::S1, prn = State::S1;
				if (dff->type.in(ID($adff), ID($adffe))) {
					SigBit reset_bit = sigmap(reset_signal[GetSize(reset_signal) == 1 ? 0 : bit]);
					if (reset_positive) {
						auto cached = inverted_resets.find(reset_bit);
						if (cached == inverted_resets.end())
							cached = inverted_resets.insert({reset_bit, make_not(module, reset_bit)}).first;
						reset_bit = cached->second;
					}
					if (reset_values[bit] == State::S0)
						clrn = reset_bit;
					else if (reset_values[bit] == State::S1)
						prn = reset_bit;
					else {
						supported = false;
						break;
					}
				}

				Cell *ff = module->addCell(NEW_ID, ID(dffeas));
				ff->setParam(ID(is_wysiwyg), Const("TRUE"));
				ff->setPort(port_q, sigmap(dff->getPort(ID::Q))[bit]);
				ff->setPort(port_d, binding.data);
				ff->setPort(port_clk, clock);
				ff->setPort(port_clrn, clrn);
				ff->setPort(port_prn, prn);
				ff->setPort(port_ena, load_enable);
				ff->setPort(port_asdata, State::S0);
				ff->setPort(port_aload, State::S0);
				ff->setPort(port_sclr, synchronous_clear);
				ff->setPort(port_sload, State::S0);
				if (verbose)
					log("  moved zero mux %s into target DFF clear for %s[%d]\n",
					    log_id(mux->name), log_id(dff->name), bit);
				folded++;
			}
			if (!supported)
				break;
			consumed_dffs.insert(dff);
			module->remove(dff);
		}
		if (!supported)
			continue;
		module->remove(mux);
	}

	return folded;
}

// A DFF whose next-state LUT is forced to zero by one control input can use
// the technology FF's synchronous clear instead of spending LUT inputs on that
// control.  The rewrite is only applied to a shared control bit: one inverter
// then feeds all affected DFFs, while every DFF's LUT loses one input.  The
// original DFF enable and reset ports are left untouched.
int fold_disabled_lut_into_dff(Module *module, bool verbose)
{
	struct Candidate {
		Cell *dff;
		Cell *lut;
		std::vector<int> control_positions;
	};

	const IdString port_d = RTLIL::escape_id("d");
	const IdString port_ena = RTLIL::escape_id("ena");
	const IdString port_sclr = RTLIL::escape_id("sclr");
	const IdString port_sload = RTLIL::escape_id("sload");
	const IdString port_aload = RTLIL::escape_id("aload");

	SigMap sigmap(module);
	pool<SigBit> output_bits = collect_output_bits(module, sigmap);
	dict<SigBit, Cell *> lut_driver;

	for (auto cell : module->cells())
		if (cell->type == ID($lut) && cell->hasPort(ID::Y))
			for (auto bit : sigmap(cell->getPort(ID::Y)))
				lut_driver[bit] = cell;

	std::vector<Candidate> candidates;
	for (auto dff : module->cells()) {
		if (dff->type != ID(dffeas) || !dff->hasPort(port_d) ||
		    !dff->hasPort(port_ena) || !dff->hasPort(port_sclr) ||
		    !dff->hasPort(port_sload) || !dff->hasPort(port_aload) ||
		    GetSize(dff->getPort(port_d)) != 1 || GetSize(dff->getPort(port_ena)) != 1 ||
		    GetSize(dff->getPort(port_sclr)) != 1 || GetSize(dff->getPort(port_sload)) != 1 ||
		    GetSize(dff->getPort(port_aload)) != 1 ||
		    sigmap(dff->getPort(port_ena)[0]) != State::S1 ||
		    sigmap(dff->getPort(port_sclr)[0]) != State::S0 ||
		    sigmap(dff->getPort(port_sload)[0]) != State::S0 ||
		    sigmap(dff->getPort(port_aload)[0]) != State::S0)
			continue;
		if (dff->get_bool_attribute(ID(pmux_rom_enable)))
			continue;

		SigBit d = sigmap(dff->getPort(port_d)[0]);
		auto driver_it = lut_driver.find(d);
		if (driver_it == lut_driver.end())
			continue;

		Cell *lut = driver_it->second;
		if (!lut->hasPort(ID::A) || !lut->hasPort(ID::Y) ||
		    GetSize(lut->getPort(ID::Y)) != 1 || !lut->hasParam(ID::WIDTH) ||
		    !lut->hasParam(ID::LUT))
			continue;

		int width = lut->getParam(ID::WIDTH).as_int();
		Const table = lut->getParam(ID::LUT);
		if (width < 1 || GetSize(lut->getPort(ID::A)) != width ||
		    !table.is_fully_def() || GetSize(table) != (1 << width))
			continue;

		Candidate candidate;
		candidate.dff = dff;
		candidate.lut = lut;
		for (int pos = 0; pos < width; pos++) {
			SigBit control = sigmap(lut->getPort(ID::A)[pos]);
			if (control.wire == nullptr || output_bits.count(control))
				continue;

			bool disabled_is_zero = true, enabled_is_nonzero = false;
			for (int index = 0; index < GetSize(table); index++) {
				if (index & (1 << pos)) {
					enabled_is_nonzero |= table[index] == State::S1;
				} else if (table[index] != State::S0) {
					disabled_is_zero = false;
					break;
				}
			}
			if (disabled_is_zero && enabled_is_nonzero)
				candidate.control_positions.push_back(pos);
		}
		if (!candidate.control_positions.empty())
			candidates.push_back(std::move(candidate));
	}

	dict<SigBit, int> control_users;
	for (auto &candidate : candidates) {
		pool<SigBit> seen;
		for (int pos : candidate.control_positions) {
			SigBit control = sigmap(candidate.lut->getPort(ID::A)[pos]);
			if (seen.insert(control).second)
				control_users[control]++;
		}
	}

	std::vector<std::pair<SigBit, int>> ordered_controls;
	for (auto &entry : control_users)
		ordered_controls.push_back(entry);
	std::sort(ordered_controls.begin(), ordered_controls.end(),
	          [](const std::pair<SigBit, int> &a, const std::pair<SigBit, int> &b) {
		          if (a.second != b.second)
			          return a.second > b.second;
		          return a.first < b.first;
	          });

	pool<int> assigned;
	int folded = 0;
	for (auto &entry : ordered_controls) {
		SigBit control = entry.first;
		std::vector<std::pair<int, int>> group;
		for (int index = 0; index < GetSize(candidates); index++) {
			if (assigned.count(index))
				continue;
			for (int pos : candidates[index].control_positions)
				if (sigmap(candidates[index].lut->getPort(ID::A)[pos]) == control) {
					group.push_back({index, pos});
					break;
				}
		}
		if (GetSize(group) < 2)
			continue;

		SigBit synchronous_clear = make_not(module, control);
		for (auto &item : group) {
			Candidate &candidate = candidates[item.first];
			int control_pos = item.second;
			int width = candidate.lut->getParam(ID::WIDTH).as_int();
			Const old_table = candidate.lut->getParam(ID::LUT);
			Const new_table(State::S0, 1 << (width - 1));
			SigSpec new_inputs;

			for (int old_pos = 0; old_pos < width; old_pos++) {
				if (old_pos == control_pos)
					continue;
				new_inputs.append(candidate.lut->getPort(ID::A)[old_pos]);
			}
			for (int index = 0; index < GetSize(new_table); index++) {
				int old_index = 1 << control_pos;
				for (int src = 0, dst = 0; src < width; src++) {
					if (src == control_pos)
						continue;
					if (index & (1 << dst))
						old_index |= 1 << src;
					dst++;
				}
				new_table.set(index, old_table[old_index]);
			}

			SigBit new_y = module->addWire(NEW_ID);
			module->addLut(module->uniquify("$pmux_dff_clear_lut"), new_inputs,
			               new_y, std::move(new_table));
			candidate.dff->setPort(port_d, new_y);
			candidate.dff->setPort(port_sclr, synchronous_clear);
			assigned.insert(item.first);
			folded++;

			if (verbose)
				log("  moved disabled-state LUT input %s into DFF %s\n",
				    log_signal(control), log_id(candidate.dff->name));
		}
	}

	return folded;
}

// A constant-data mux can be absorbed by the LUT that drives its other data
// input.  This is the same boolean operation as extending the LUT's truth
// table with the select signal; no physical LUT is added as long as the result
// still fits in the technology's four-input LUT.  Requiring the LUT output to
// have exactly one load keeps the rewrite local and preserves every other fanout.
int fold_mux_into_luts(Module *module, CellTypes &ct, bool verbose)
{
	int folded = 0;

	while (true) {
		SigMap sigmap(module);
		pool<SigBit> output_bits = collect_output_bits(module, sigmap);
		dict<SigBit, int> loads;
		dict<SigBit, Cell *> bit_driver;
		dict<SigBit, Cell *> lut_driver;

		for (auto cell : module->cells()) {
			for (auto &conn : cell->connections()) {
				if (ct.cell_input(cell->type, conn.first))
					for (auto bit : sigmap(conn.second))
						loads[bit]++;
				if (ct.cell_output(cell->type, conn.first))
					for (auto bit : sigmap(conn.second)) {
						bit_driver[bit] = cell;
						if (cell->type == ID($lut))
							lut_driver[bit] = cell;
					}
			}
		}

		Cell *candidate = nullptr;
		Cell *driver = nullptr;
		bool lut_on_b = false;
		State constant = State::S0;
		SigBit select;

		for (auto cell : module->cells()) {
			if (!cell->type.in(ID($mux), ID($_MUX_)))
				continue;
			if (GetSize(cell->getPort(ID::A)) != 1 ||
			    GetSize(cell->getPort(ID::B)) != 1 ||
			    GetSize(cell->getPort(ID::Y)) != 1 ||
			    GetSize(cell->getPort(ID::S)) != 1)
				continue;

			SigBit a = sigmap(cell->getPort(ID::A)[0]);
			SigBit b = sigmap(cell->getPort(ID::B)[0]);
			SigBit s = sigmap(cell->getPort(ID::S)[0]);
			if (s.wire == nullptr)
				continue;

			SigBit lut_input;
			if ((a == State::S0 || a == State::S1) && b.wire != nullptr) {
				constant = a.data;
				lut_input = b;
				lut_on_b = true;
			} else if ((b == State::S0 || b == State::S1) && a.wire != nullptr) {
				constant = b.data;
				lut_input = a;
				lut_on_b = false;
			} else {
				continue;
			}

			if (output_bits.count(lut_input) || loads[lut_input] != 1 ||
			    combinational_depends_on(lut_input, s, sigmap, ct, bit_driver))
				continue;

			auto it = lut_driver.find(lut_input);
			if (it == lut_driver.end())
				continue;

			Cell *lut = it->second;
			int width = lut->getParam(ID::WIDTH).as_int();
			Const table = lut->getParam(ID::LUT);
			if (width < 0 || width >= 4 || GetSize(lut->getPort(ID::Y)) != 1 ||
			    GetSize(lut->getPort(ID::A)) != width || !table.is_fully_def() ||
			    GetSize(table) != (1 << width))
				continue;

			candidate = cell;
			driver = lut;
			select = s;
			break;
		}

		if (candidate == nullptr)
			break;

		int width = driver->getParam(ID::WIDTH).as_int();
		Const old_table = driver->getParam(ID::LUT);
		Const new_table(State::S0, 1 << (width + 1));
		for (int index = 0; index < GetSize(old_table); index++) {
			bool original = old_table[index] == State::S1;
			bool constant_value = constant == State::S1;
			new_table.set(index, (lut_on_b ? constant_value : original) ? State::S1 : State::S0);
			new_table.set(index | (1 << width),
			              (lut_on_b ? original : constant_value) ? State::S1 : State::S0);
		}

		SigSpec inputs = driver->getPort(ID::A);
		inputs.append(select);
		driver->setPort(ID::A, inputs);
		driver->setParam(ID::WIDTH, width + 1);
		driver->setParam(ID::LUT, std::move(new_table));

		if (verbose)
			log("  folded mux %s into LUT %s\n", log_id(candidate->name), log_id(driver->name));

		module->connect(candidate->getPort(ID::Y), driver->getPort(ID::Y));
		module->remove(candidate);
		folded++;
	}

	return folded;
}

struct DecodeBit {
	SigSpec bus;   // common decoded bus (sigmap'ed)
	int code = -1; // code value selected by this bit
};

// Recognize a single select bit as "bus == const" (also handles the
// logic_not/reduce_or form that proc emits for the all-zero code).
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
		return true;
	}

	if (drv->type == ID($logic_not)) {
		SigSpec in = sigmap(drv->getPort(ID::A));
		if (GetSize(in) > 1) {
			out.bus = in;
			out.code = 0;
			return true;
		}
		auto jt = bit2cell.find(in[0]);
		if (jt != bit2cell.end() && jt->second != nullptr &&
		    module->cells_.count(jt->second->name) &&
		    jt->second->type.in(ID($reduce_or), ID($reduce_bool))) {
			out.bus = sigmap(jt->second->getPort(ID::A));
			out.code = 0;
			return true;
		}
		if (jt == bit2cell.end()) {
			out.bus = in;
			out.code = 0;
			return true;
		}
	}
	return false;
}

// Balanced mux tree over `bus`; leaves[i] is selected by bus value i.
SigSpec build_tree(Module *module, const SigSpec &bus, const std::vector<SigSpec> &leaves)
{
	if (GetSize(bus) == 0)
		return leaves[0];
	int n = GetSize(bus);
	int half = GetSize(leaves) / 2;
	std::vector<SigSpec> lo(leaves.begin(), leaves.begin() + half);
	std::vector<SigSpec> hi(leaves.begin() + half, leaves.end());
	SigSpec l = build_tree(module, bus.extract(0, n - 1), lo);
	SigSpec h = build_tree(module, bus.extract(0, n - 1), hi);
	return module->Mux(NEW_ID, l, h, bus[n - 1]);
}

bool slice_equal(const SigSpec &a, int aoff, const SigSpec &b, int boff, int width, SigMap &sigmap)
{
	for (int i = 0; i < width; i++)
		if (sigmap(a[aoff + i]) != sigmap(b[boff + i]))
			return false;
	return true;
}

SigBit make_not(Module *module, SigBit bit)
{
	for (auto cell : module->cells())
		if (cell->type == ID($not) && GetSize(cell->getPort(ID::Y)) == 1 &&
		    GetSize(cell->getPort(ID::A)) == 1 && cell->getPort(ID::A)[0] == bit)
			return cell->getPort(ID::Y)[0];
	SigBit y = module->addWire(NEW_ID);
	module->addNot(NEW_ID, bit, y);
	return y;
}

SigBit make_bit_and(Module *module, const std::vector<SigBit> &inputs)
{
	if (inputs.empty())
		return State::S1;
	if (inputs.size() == 1)
		return inputs.front();

	SigBit acc = inputs.front();
	for (size_t i = 1; i < inputs.size(); i++) {
		SigBit rhs = inputs[i];
		for (auto cell : module->cells())
			if (cell->type.in(ID($and), ID($_AND_)) &&
			    GetSize(cell->getPort(ID::Y)) == 1 &&
			    GetSize(cell->getPort(ID::A)) == 1 && GetSize(cell->getPort(ID::B)) == 1) {
				SigBit a = cell->getPort(ID::A)[0];
				SigBit b = cell->getPort(ID::B)[0];
				if ((a == acc && b == rhs) || (a == rhs && b == acc))
					return cell->getPort(ID::Y)[0];
			}
		SigBit y = module->addWire(NEW_ID);
		module->addAndGate(NEW_ID, acc, rhs, y);
		acc = y;
	}
	return acc;
}

bool same_multiset(const SigSpec &a, const SigSpec &b)
{
	if (GetSize(a) != GetSize(b))
		return false;
	std::map<SigBit, int> counts;
	for (auto bit : a)
		counts[bit]++;
	for (auto bit : b) {
		auto it = counts.find(bit);
		if (it == counts.end())
			return false;
		if (--it->second == 0)
			counts.erase(it);
	}
	return counts.empty();
}

SigBit make_or(Module *module, const std::vector<SigBit> &inputs)
{
	if (inputs.empty())
		return State::S0;
	if (inputs.size() == 1)
		return inputs.front();
	SigSpec sig(inputs);
	for (auto cell : module->cells())
		if (cell->type.in(ID($reduce_bool), ID($reduce_or), ID($or), ID($logic_or)) &&
		    GetSize(cell->getPort(ID::Y)) == 1) {
			SigSpec cell_inputs = cell->getPort(ID::A);
			if (cell->hasPort(ID::B))
				cell_inputs.append(cell->getPort(ID::B));
			if (same_multiset(cell_inputs, sig))
				return cell->getPort(ID::Y)[0];
		}

	SigBit y = module->addWire(NEW_ID);
	module->addReduceBool(NEW_ID, sig, y);
	return y;
}

void build_load_maps(Module *module, SigMap &sigmap, CellTypes &ct,
                     dict<SigBit, Cell *> &bit2cell,
                     dict<SigBit, std::vector<InputLoad>> &bit2loads)
{
	for (auto cell : module->cells())
		for (auto &conn : cell->connections()) {
			bool is_output = ct.cell_output(cell->type, conn.first);
			int offset = 0;
			for (auto bit : sigmap(conn.second)) {
				if (is_output)
					bit2cell[bit] = cell;
				else
					bit2loads[bit].push_back({cell, conn.first, offset});
				offset++;
			}
		}
}

pool<SigBit> collect_output_bits(Module *module, SigMap &sigmap)
{
	pool<SigBit> output_bits;
	for (auto port_name : module->ports) {
		Wire *wire = module->wire(port_name);
		if (wire != nullptr && wire->port_output)
			for (auto bit : sigmap(SigSpec(wire)))
				output_bits.insert(bit);
	}
	return output_bits;
}

bool only_consumed_by(const SigSpec &sig, Cell *allowed_cell, SigMap &sigmap,
                      const dict<SigBit, std::vector<InputLoad>> &bit2loads,
                      const pool<SigBit> &output_bits)
{
	for (auto bit : sigmap(sig)) {
		if (output_bits.count(bit))
			return false;
		auto it = bit2loads.find(bit);
		if (it == bit2loads.end())
			continue;
		for (auto &load : it->second)
			if (load.cell != allowed_cell)
				return false;
	}
	return true;
}

// A constant power-of-two shift is a one-hot decoder.  ABC often maps the generic
// $shl datapath less efficiently than the equivalent per-output equality
// comparisons, especially once the decoded bus is wide.  Expose that form
// before the coarse technology-mapping stages.  The rule is structural: it
// checks only the shift operands and widths, not module or signal names.
int lower_constant_power_shift_decoders(Module *module, SigMap &sigmap, bool verbose)
{
	std::vector<Cell *> candidates;
	for (auto cell : module->cells())
		if (cell->type == ID($shl))
			candidates.push_back(cell);

	int lowered = 0;
	for (auto cell : candidates) {
		if (cell->type != ID($shl) || !module->cells_.count(cell->name) ||
		    !cell->hasPort(ID::A) ||
		    !cell->hasPort(ID::B) || !cell->hasPort(ID::Y))
			continue;

		SigSpec a = sigmap(cell->getPort(ID::A));
		SigSpec b = sigmap(cell->getPort(ID::B));
		SigSpec y = sigmap(cell->getPort(ID::Y));
		int b_width = GetSize(b);
		int y_width = GetSize(y);
		// The native shift can be cheaper than an explicit equality decoder for
		// short indices when the decoded vector is itself a top-level output.
		// On the official 0.69 ABC flow a 7-bit index and a 128-bit output
		// regresses by 8 LCELLs, while the same construct feeding internal
		// logic amortizes through downstream sharing.  The 9-bit/512-bit output
		// still benefits, so this is a local cost guard rather than a size cap.
		if (!a.is_fully_const() || b_width < 5 || b_width > 16 ||
		    y_width < 32 || y_width > 65536)
			continue;

		bool decoded_drives_output = false;
		pool<SigBit> output_bits = collect_output_bits(module, sigmap);
		for (auto bit : y)
			if (output_bits.count(bit)) {
				decoded_drives_output = true;
				break;
			}
		if (b_width < 8 && decoded_drives_output)
			continue;

		// The shift amount must be an unsigned index.
		if (cell->getParam(ID::B_SIGNED).as_bool())
			continue;
		Const value = a.as_const();
		int one_bit = -1;
		bool is_power_of_two = true;
		for (int i = 0; i < GetSize(value); i++) {
			if (value[i] != State::S0 && value[i] != State::S1) {
				is_power_of_two = false;
				break;
			}
			if (value[i] == State::S1) {
				if (one_bit >= 0) {
					is_power_of_two = false;
					break;
				}
				one_bit = i;
			}
		}
		if (one_bit < 0 || !is_power_of_two)
			continue;

		// $shl sign-extends a narrow signed A before shifting.  A one-bit
		// signed value, or one whose only set bit is the sign bit, becomes a
		// mask of ones above that bit rather than a one-hot value.  Leave
		// that case to the native shift logic.
		if (cell->getParam(ID::A_SIGNED).as_bool() &&
		    GetSize(value) < y_width && one_bit == GetSize(value) - 1)
			continue;

		// A has one set bit at one_bit.  Output bit i is set exactly when
		// B == i - one_bit; indices that cannot select that bit stay zero.
		int64_t representable = int64_t(1) << b_width;
		int64_t eq_limit = std::min<int64_t>(y_width, one_bit + representable);
		int lowered_bits = int(std::max<int64_t>(0, eq_limit - one_bit));
		for (int i = 0; i < y_width; i++) {
			if (i < one_bit || i >= eq_limit) {
				module->connect(y[i], State::S0);
				continue;
			}
			Cell *eq = module->addEq(NEW_ID, b, Const(i - one_bit, b_width), y[i]);
			eq->attributes = cell->attributes;
		}

		if (verbose)
			log("  lowered constant power-of-two %d-bit shift decoder at %s into %d equality comparisons\n",
			    b_width, log_signal(y), lowered_bits);
		module->remove(cell);
		lowered++;
	}
	return lowered;
}

// Turn a priority chain of scalar-select $mux cells into a lower-cost form.
// Adjacent priorities are folded into small priority groups, and the groups
// are combined from lowest to highest priority.  Two-wide groups win for short
// chains; three-wide groups amortize the shared activity signal better once a
// chain is long enough.  Keeping the select logic local avoids one long mask
// network whose techmap/ABC cost grows with the chain length.
int convert_priority_mux_chains(Module *module, SigMap &sigmap, CellTypes &ct, bool verbose)
{
	std::vector<Cell *> muxes;
	dict<SigBit, Cell *> mux_driver;
	for (auto cell : module->cells())
		if (cell->type == ID($mux)) {
			muxes.push_back(cell);
			for (auto bit : sigmap(cell->getPort(ID::Y)))
				mux_driver[bit] = cell;
		}

	pool<IdString> used_as_inner;
	for (auto mux : muxes) {
		for (auto bit : sigmap(mux->getPort(ID::A))) {
			auto it = mux_driver.find(bit);
			if (it != mux_driver.end())
				used_as_inner.insert(it->second->name);
		}
	}

	int converted = 0;
	pool<IdString> done;
	for (auto outer : muxes) {
		if (done.count(outer->name) || !module->cells_.count(outer->name))
			continue;
		if (used_as_inner.count(outer->name))
			continue;

		int width = GetSize(outer->getPort(ID::Y));
		if (width == 0 || width > 64)
			continue;

		std::vector<Cell *> chain;
		std::vector<SigSpec> selects;
		SigSpec base;
		bool ok = true;
		Cell *cursor = outer;

		while (true) {
			if (chain.size() >= 64 || !module->cells_.count(cursor->name) ||
			    cursor->type != ID($mux) || GetSize(cursor->getPort(ID::Y)) != width) {
				ok = false;
				break;
			}

			SigSpec sel = sigmap(cursor->getPort(ID::S));
			if (GetSize(sel) != 1 || sel[0].wire == nullptr)
				break;
			chain.push_back(cursor);
			selects.push_back(sel);

			SigSpec a = sigmap(cursor->getPort(ID::A));
			if (GetSize(a) != width) {
				ok = false;
				break;
			}

			auto it = mux_driver.find(a[0]);
			if (it == mux_driver.end()) {
				base = a;
				break;
			}

			Cell *next = it->second;
			for (auto bit : a) {
				auto drv = mux_driver.find(bit);
				if (drv == mux_driver.end() || drv->second != next) {
					ok = false;
					break;
				}
			}
			if (!ok)
				break;
			if (std::find(chain.begin(), chain.end(), next) != chain.end()) {
				ok = false;
				break;
			}
			cursor = next;
		}

		if (!ok || chain.size() < 4)
			continue;
		// Pairing adds one activity OR per pair.  For very narrow data paths and
		// long chains that fixed control cost can exceed the saved mux logic.
		if (width <= 2 && chain.size() > size_t(16 * width))
			continue;

		pool<SigBit> inner_bits;
		for (size_t i = 1; i < chain.size(); i++)
			for (auto bit : sigmap(chain[i]->getPort(ID::Y)))
				inner_bits.insert(bit);
		dict<SigBit, std::vector<InputLoad>> chain_loads;
		for (auto cell : module->cells())
			for (auto &conn : cell->connections()) {
				if (ct.cell_output(cell->type, conn.first))
					continue;
				int offset = 0;
				for (auto bit : sigmap(conn.second)) {
					if (inner_bits.count(bit))
						chain_loads[bit].push_back({cell, conn.first, offset});
					offset++;
				}
			}
		pool<SigBit> output_bits = collect_output_bits(module, sigmap);
		for (size_t i = 1; i < chain.size(); i++)
			if (!only_consumed_by(chain[i]->getPort(ID::Y), chain[i - 1], sigmap,
			                      chain_loads, output_bits)) {
				ok = false;
				break;
			}
		if (!ok)
			continue;

		pool<SigBit> seen_selects;
		for (auto &sel : selects)
			if (!seen_selects.insert(sel[0]).second) {
				ok = false;
				break;
			}
		if (!ok)
			continue;

		// The chain's selects are independent scalar predicates, so a group of
		// three can share one OR term without changing priority semantics.
		// Cost sweeps over chain length, data width and group width put the
		// crossover at length 7 for widths above one bit.  Narrow single-bit
		// chains stay pairwise: their control logic dominates the data muxes.
		const int group_size = (width >= 2 && chain.size() >= 7) ? 3 : 2;

		SigSpec out = outer->getPort(ID::Y);
		std::vector<SigBit> group_active;
		std::vector<SigSpec> group_value;
		for (size_t start = 0; start < chain.size(); start += group_size) {
			size_t end = std::min(start + group_size, chain.size());
			SigSpec value = chain[end - 1]->getPort(ID::B);
			for (size_t i = end - 1; i > start; i--)
				value = module->Mux(NEW_ID, value, chain[i - 1]->getPort(ID::B),
				                    selects[i - 1][0]);

			std::vector<SigBit> active;
			for (size_t i = start; i < end; i++)
				active.push_back(selects[i][0]);
			group_value.push_back(value);
			group_active.push_back(make_or(module, active));
		}

		SigSpec result = base;
		for (size_t i = group_value.size(); i > 0; i--)
			result = module->Mux(NEW_ID, result, group_value[i - 1], group_active[i - 1]);
		module->connect(out, result);

		for (auto mux : chain) {
			done.insert(mux->name);
			module->remove(mux);
		}
		converted++;
		if (verbose)
			log("  converted %d-level priority $mux chain at %s into a %d-way nested mux tree\n",
			    GetSize(chain), log_signal(out), group_size);
	}

	if (converted)
		log("pmux_opt: restructured %d priority $mux chain(s).\n", converted);
	return converted;
}

bool implies_bit_value(Module *module, SigMap &sigmap,
                       const dict<SigBit, Cell *> &bit2cell, SigBit expr,
                       SigBit gate, bool gate_value, int depth = 0)
{
	if (depth > 12)
		return false;
	expr = sigmap(expr);
	gate = sigmap(gate);
	if (expr == gate)
		return gate_value;
	if (expr.wire == nullptr)
		return false;

	auto it = bit2cell.find(expr);
	if (it == bit2cell.end())
		return false;
	Cell *cell = it->second;

	if (cell->type == ID($not)) {
		SigSpec a = sigmap(cell->getPort(ID::A));
		return GetSize(a) == 1 && a[0] == gate && !gate_value;
	}

	auto any_imply = [&](const SigSpec &sig) {
		for (auto bit : sig)
			if (implies_bit_value(module, sigmap, bit2cell, bit, gate, gate_value, depth + 1))
				return true;
		return false;
	};
	auto all_imply = [&](const SigSpec &sig) {
		bool any = false;
		for (auto bit : sig) {
			any = true;
			if (!implies_bit_value(module, sigmap, bit2cell, bit, gate, gate_value, depth + 1))
				return false;
		}
		return any;
	};

	if (cell->type.in(ID($reduce_and), ID($and), ID($logic_and)))
		return any_imply(cell->getPort(ID::A)) ||
		       (cell->hasPort(ID::B) && any_imply(cell->getPort(ID::B)));
	if (cell->type.in(ID($reduce_bool), ID($reduce_or), ID($or), ID($logic_or)))
		return all_imply(cell->getPort(ID::A)) &&
		       (!cell->hasPort(ID::B) || all_imply(cell->getPort(ID::B)));
	if (cell->type == ID($mux))
		return implies_bit_value(module, sigmap, bit2cell, cell->getPort(ID::A)[0],
		                         gate, gate_value, depth + 1) &&
		       implies_bit_value(module, sigmap, bit2cell, cell->getPort(ID::B)[0],
		                         gate, gate_value, depth + 1);
	return false;
}

bool all_loads_masked(Module *module, SigMap &sigmap, CellTypes &ct,
                      const dict<SigBit, Cell *> &bit2cell,
                      const dict<SigBit, std::vector<InputLoad>> &bit2loads,
                      const pool<SigBit> &output_bits, Cell *source,
                      SigBit gate, bool gate_value)
{
	pool<SigBit> visited;
	std::function<bool(SigBit)> visit = [&](SigBit bit) {
		bit = sigmap(bit);
		if (!visited.insert(bit).second)
			return true;
		if (output_bits.count(bit))
			return false;

		auto it = bit2loads.find(bit);
		if (it == bit2loads.end())
			return true;
		for (auto &load : it->second) {
			Cell *cell = load.cell;
			if (cell == source)
				return false;

			if (cell->hasPort(ID::CLK)) {
				if (load.port != ID::D && load.port != ID::EN)
					return false;
				if (!cell->hasPort(ID::EN) ||
				    !cell->getParam(ID::EN_POLARITY).as_bool())
					return false;
				if (!implies_bit_value(module, sigmap, bit2cell,
				                       sigmap(cell->getPort(ID::EN))[0],
				                       gate, gate_value))
					return false;
				continue;
			}

			SigSpec outputs;
			bool has_output = false;
			for (auto &conn : cell->connections())
				if (ct.cell_output(cell->type, conn.first)) {
					outputs.append(conn.second);
					has_output = true;
				}
			if (!has_output)
				return false;
			for (auto out_bit : outputs)
				if (!visit(out_bit))
					return false;
		}
		return true;
	};

	for (auto bit : sigmap(source->getPort(ID::Y)))
		if (!visit(bit))
			return false;
	return true;
}

// If a decoder only affects registers whose enable already proves a bus bit,
// that bit is redundant in the comparator.  Remove one such bit at a time and
// leave every unproved comparator untouched.
int narrow_gated_eq_decoders(Module *module, SigMap &sigmap, CellTypes &ct,
                             const pool<SigBit> &allowed_outputs, bool verbose)
{
	dict<SigBit, Cell *> bit2cell;
	dict<SigBit, std::vector<InputLoad>> bit2loads;
	build_load_maps(module, sigmap, ct, bit2cell, bit2loads);
	pool<SigBit> output_bits = collect_output_bits(module, sigmap);

	std::vector<Cell *> candidates;
	for (auto cell : module->cells())
		if (cell->type.in(ID($eq), ID($eqx)) && GetSize(cell->getPort(ID::Y)) == 1 &&
		    allowed_outputs.count(sigmap(cell->getPort(ID::Y)[0])))
			candidates.push_back(cell);

	int narrowed = 0;
	for (auto cell : candidates) {
		SigSpec a = sigmap(cell->getPort(ID::A));
		SigSpec b = sigmap(cell->getPort(ID::B));
		if (GetSize(a) != GetSize(b) || GetSize(a) < 2 || GetSize(a) > 16)
			continue;

		bool const_on_a = a.is_fully_const() && !b.is_fully_const();
		bool const_on_b = b.is_fully_const() && !a.is_fully_const();
		if (!const_on_a && !const_on_b)
			continue;

		SigSpec bus = const_on_a ? b : a;
		int code = (const_on_a ? a : b).as_int();
		int remove_bit = -1;
		for (int bit = GetSize(bus) - 1; bit >= 0; bit--) {
			bool value = (code >> bit) & 1;
			if (all_loads_masked(module, sigmap, ct, bit2cell, bit2loads,
			                     output_bits, cell, bus[bit], value)) {
				remove_bit = bit;
				break;
			}
		}
		if (remove_bit < 0)
			continue;

		SigSpec new_bus;
		int new_code = 0;
		int out_bit = 0;
		for (int bit = 0; bit < GetSize(bus); bit++) {
			if (bit == remove_bit)
				continue;
			new_bus.append(bus[bit]);
			if ((code >> bit) & 1)
				new_code |= 1 << out_bit;
			out_bit++;
		}

		int width = GetSize(new_bus);
		if (const_on_a) {
			cell->setPort(ID::A, Const(new_code, width));
			cell->setPort(ID::B, new_bus);
		} else {
			cell->setPort(ID::A, new_bus);
			cell->setPort(ID::B, Const(new_code, width));
		}
		cell->setParam(ID::A_WIDTH, width);
		cell->setParam(ID::B_WIDTH, width);
		cell->setParam(ID::Y_WIDTH, 1);
		narrowed++;
		if (verbose)
			log("  removed gated bus bit %d from equality %s (%d -> %d bits)\n",
			    remove_bit, log_id(cell->name), GetSize(bus), width);
	}

	if (narrowed)
		log("pmux_opt: narrowed %d gated equality decoder(s).\n", narrowed);
	return narrowed;
}

// Share the common literal prefix of active-high DFF enables.  Byte-sliced
// register updates often end up as several reduce_and cones such as
// {gate, clock_enable, update_condition_i}; rebuilding them as
// shared_gate & update_condition_i exposes the common logic to ABC.
int factor_common_enable_terms(Module *module, SigMap &sigmap, CellTypes &ct, bool verbose)
{
	bool has_active_high_dffe = false;
	for (auto cell : module->cells())
		if (cell->type == ID($dffe) && cell->hasPort(ID::EN) &&
		    cell->getParam(ID::EN_POLARITY).as_bool()) {
			has_active_high_dffe = true;
			break;
		}
	if (!has_active_high_dffe)
		return 0;

	dict<SigBit, Cell *> bit2cell;
	for (auto module_cell : module->cells())
		for (auto &conn : module_cell->connections())
			if (ct.cell_output(module_cell->type, conn.first))
				for (auto bit : sigmap(conn.second))
					bit2cell[bit] = module_cell;

	dict<SigBit, std::vector<Cell *>> groups;
	dict<Cell *, SigSpec> cell_terms;
	for (auto cell : module->cells()) {
		if (cell->type != ID($dffe) || !cell->hasPort(ID::EN) ||
		    !cell->getParam(ID::EN_POLARITY).as_bool())
			continue;
		SigSpec clk = sigmap(cell->getPort(ID::CLK));
		SigSpec en = sigmap(cell->getPort(ID::EN));
		if (GetSize(clk) != 1 || GetSize(en) != 1 || en[0].wire == nullptr)
			continue;
		auto it = bit2cell.find(en[0]);
		if (it == bit2cell.end() || !it->second->type.in(ID($reduce_and), ID($and)))
			continue;
		SigSpec terms = sigmap(it->second->getPort(ID::A));
		if (GetSize(terms) < 2)
			continue;
		groups[clk[0]].push_back(cell);
		cell_terms[cell] = terms;
	}

	int factored = 0;
	for (auto &group : groups) {
		auto &cells = group.second;
		if (cells.size() < 2)
			continue;

		std::vector<SigBit> common;
		for (auto bit : cell_terms[cells.front()]) {
			bool present = true;
			for (size_t i = 1; i < cells.size() && present; i++)
				if (!std::count(cell_terms[cells[i]].begin(), cell_terms[cells[i]].end(), bit))
					present = false;
			if (present && std::find(common.begin(), common.end(), bit) == common.end())
				common.push_back(bit);
		}
		if (common.empty())
			continue;

		SigBit shared = make_bit_and(module, common);
		for (auto cell : cells) {
			std::vector<SigBit> remainder;
			for (auto bit : cell_terms[cell])
				if (std::find(common.begin(), common.end(), bit) == common.end())
					remainder.push_back(bit);
			SigBit new_en = shared;
			if (!remainder.empty()) {
				remainder.push_back(shared);
				new_en = make_bit_and(module, remainder);
			}
			cell->setPort(ID::EN, new_en);
			factored++;
		}
		if (verbose)
			log("  factored %d common literal(s) across %d active-high DFF enable(s)\n",
			    GetSize(common), GetSize(cells));
	}

	if (factored)
		log("pmux_opt: refactored %d active-high DFF enable(s).\n", factored);
	return factored;
}

// Factor equality decoders for constants that differ in only one bit.
//
// For example, bus == 8'b11111111 and bus == 8'b11111110 share the upper
// seven comparisons.  Building that common prefix once lets abc9 share it
// between the two predicates instead of synthesizing two independent wide
// equality comparators.  This is deliberately conservative: only straight
// $eq cells with a fully constant operand are considered; $eqx is skipped.
void factor_adjacent_eq(Module *module, SigMap &sigmap, const pool<SigBit> &allowed_outputs,
                        bool verbose)
{
	struct EqInfo {
		Cell *cell;
		SigSpec bus;
		int code;
	};

	std::map<SigSpec, std::vector<EqInfo>> groups;
	for (auto cell : module->cells()) {
		if (cell->type != ID($eq))
			continue;
		if (!allowed_outputs.count(sigmap(cell->getPort(ID::Y)[0])))
			continue;

		SigSpec a = sigmap(cell->getPort(ID::A));
		SigSpec b = sigmap(cell->getPort(ID::B));
		if (GetSize(a) != GetSize(b) || GetSize(a) == 0 || GetSize(a) > 16)
			continue;

		SigSpec bus;
		Const code;
		if (a.is_fully_const() && !b.is_fully_const()) {
			code = a.as_const();
			bus = b;
		} else if (b.is_fully_const() && !a.is_fully_const()) {
			code = b.as_const();
			bus = a;
		} else {
			continue;
		}

		groups[bus].push_back({cell, bus, code.as_int()});
	}

	int merged = 0;
	for (auto &entry : groups) {
		auto &eqs = entry.second;
		if (eqs.size() < 2)
			continue;

		pool<IdString> rewritten;
		for (size_t i = 0; i < eqs.size(); i++) {
			if (rewritten.count(eqs[i].cell->name))
				continue;
			for (size_t j = i + 1; j < eqs.size(); j++) {
				if (rewritten.count(eqs[j].cell->name))
					continue;

				int diff = eqs[i].code ^ eqs[j].code;
				if (diff == 0 || (diff & (diff - 1)) != 0)
					continue;

				int diff_bit = 0;
				while (((diff >> diff_bit) & 1) == 0)
					diff_bit++;

				std::vector<SigBit> common;
				for (int bit = 0; bit < GetSize(eqs[i].bus); bit++) {
					if (bit == diff_bit)
						continue;
					SigBit s = sigmap(eqs[i].bus[bit]);
					if (((eqs[i].code >> bit) & 1) == 0)
						s = make_not(module, s);
					common.push_back(s);
				}
				SigBit prefix = make_bit_and(module, common);

				auto build_equal = [&](const EqInfo &eq) {
					SigBit s = sigmap(eq.bus[diff_bit]);
					if (((eq.code >> diff_bit) & 1) == 0)
						s = make_not(module, s);
					std::vector<SigBit> terms = {prefix, s};
					return make_bit_and(module, terms);
				};

				SigBit y1 = build_equal(eqs[i]);
				SigBit y2 = build_equal(eqs[j]);
				if (verbose)
					log("  factored adjacent equality codes %x / %x over %d-bit bus\n",
					    eqs[i].code, eqs[j].code, GetSize(eqs[i].bus));

				module->connect(eqs[i].cell->getPort(ID::Y), y1);
				module->connect(eqs[j].cell->getPort(ID::Y), y2);
				module->remove(eqs[i].cell);
				module->remove(eqs[j].cell);
				rewritten.insert(eqs[i].cell->name);
				rewritten.insert(eqs[j].cell->name);
				merged++;
				break;
			}
		}
	}

	if (merged)
		log("pmux_opt: factored %d adjacent equality pair(s).\n", merged);
}

// Sparse PMUX cells in register-update logic often carry data that is just a
// permutation of the decoded bus.  Their A input is undef because opt_dff has
// already moved the hold behaviour into the enable input.  For every selected
// code, try to prove each output bit is equal to one of the bus bits.  If all
// bits qualify, the whole PMUX collapses to wiring.  The unselected cases are
// deliberately don't-care (A is undef), so the replacement is a refinement of
// the original netlist rather than an assumption about unselected data.
int simplify_partial_pmux(Module *module, SigMap &sigmap, CellTypes &ct, bool verbose,
                          pool<SigBit> &eq_outputs)
{
	std::vector<Cell *> pmuxes;
	for (auto cell : module->cells())
		if (cell->type == ID($pmux))
			pmuxes.push_back(cell);
	if (pmuxes.empty())
		return 0;

	dict<SigBit, Cell *> bit2cell;
	for (auto cell : module->cells())
		for (auto &conn : cell->connections())
			if (ct.cell_output(cell->type, conn.first))
				for (auto bit : sigmap(conn.second))
					bit2cell[bit] = cell;

	int simplified = 0;
	for (auto cell : pmuxes) {
		if (!module->cells_.count(cell->name))
			continue;

		SigSpec a = sigmap(cell->getPort(ID::A));
		if (verbose)
			log("  partial candidate %s: A=%s undef=%d\n", log_id(cell->name),
			    log_signal(a), a.is_fully_undef());

		SigSpec b = sigmap(cell->getPort(ID::B));
		SigSpec s = sigmap(cell->getPort(ID::S));
		int width = GetSize(a);
		int nsel = GetSize(s);
		if (width == 0 || width > 16 || nsel < 2 || GetSize(b) != width * nsel)
			continue;
		if (!a.is_fully_undef() && width != 1)
			continue;

		std::vector<DecodeBit> dec(nsel);
		bool ok = true;
		for (int i = 0; i < nsel; i++)
			if (!analyze_select_bit(s[i], sigmap, bit2cell, module, dec[i])) {
				if (verbose)
					log("    select %d is not a constant decoder\n", i);
				ok = false;
				break;
			}
		if (!ok)
			continue;

		SigSpec bus = dec[0].bus;
		int bw = GetSize(bus);
		if (verbose) {
			log("    bus=%s codes:", log_signal(bus));
			for (int i = 0; i < nsel; i++)
				log(" %x", dec[i].code);
			log("\n");
		}
		if (bw < 2 || bw > 16)
			continue;
		for (int i = 0; i < nsel; i++)
			if (dec[i].bus != bus || dec[i].code < 0 || dec[i].code >= (1 << bw)) {
				ok = false;
				break;
			}
		if (!ok)
			continue;

		dict<SigBit, int> bus_pos;
		for (int i = 0; i < bw; i++)
			bus_pos[sigmap(bus[i])] = i;

		SigSpec replacement;
		std::vector<SigBit> known_selects;
		bool all_direct = true;
		for (int bit = 0; bit < width; bit++) {
			bool found = false;
			std::vector<int> candidates;
			if (a.is_fully_undef()) {
				for (int i = 0; i < nsel; i++) {
					SigBit leaf = sigmap(b.extract(i * width + bit, 1)[0]);
					if (leaf == State::Sx)
						continue;
					auto prefer = bus_pos.find(leaf);
					if (prefer != bus_pos.end()) {
						if (std::find(candidates.begin(), candidates.end(), prefer->second) == candidates.end())
							candidates.push_back(prefer->second);
						break;
					}
				}
				for (int bus_bit = 0; bus_bit < bw; bus_bit++)
					if (std::find(candidates.begin(), candidates.end(), bus_bit) == candidates.end())
						candidates.push_back(bus_bit);
			} else {
				for (int bus_bit = bw - 1; bus_bit >= 0; bus_bit--)
					candidates.push_back(bus_bit);
			}

			for (int bus_bit : candidates) {
				SigBit candidate = sigmap(bus[bus_bit]);
				if (candidate == State::S0 || candidate == State::S1)
					continue;

				bool matches = true;
				for (int i = 0; i < nsel; i++) {
					SigBit leaf = sigmap(b.extract(i * width + bit, 1)[0]);
					if (leaf == State::Sx)
						continue;
					int expected = (dec[i].code >> bus_bit) & 1;
					if ((leaf == State::S0 || leaf == State::S1) &&
					    leaf != SigBit(expected ? State::S1 : State::S0)) {
						matches = false;
						break;
					}
					if (leaf != State::S0 && leaf != State::S1) {
						auto it = bus_pos.find(leaf);
						if (it == bus_pos.end() ||
						    (((dec[i].code >> it->second) & 1) != expected)) {
							matches = false;
							break;
						}
					}
				}
				if (matches) {
					replacement.append(candidate);
					found = true;
					break;
				}
			}
			if (!found) {
				if (verbose) {
					log("    no direct bus bit for output bit %d; leaves:", bit);
					for (int i = 0; i < nsel; i++)
						log(" %s", log_signal(b.extract(i * width + bit, 1)));
					log("\n");
				}
				all_direct = false;
				break;
			}
		}
		if (!all_direct)
			continue;

		if (!a.is_fully_undef()) {
			for (int i = 0; i < nsel; i++)
				if (sigmap(b.extract(i * width, 1)[0]) != State::Sx)
					known_selects.push_back(s[i]);
			if (known_selects.empty())
				continue;
			replacement = module->Mux(NEW_ID, a, replacement, make_or(module, known_selects));
		}

		if (verbose)
			log("  collapsed %s: %d selected codes, %d-bit output rewritten from %d-bit bus\n",
			    log_id(cell->name), nsel, width, bw);
		module->connect(cell->getPort(ID::Y), replacement);
		for (auto bit : s)
			eq_outputs.insert(sigmap(bit));
		module->remove(cell);
		simplified++;
	}

	if (simplified)
		log("pmux_opt: collapsed %d sparse partial-update PMUX cell(s).\n", simplified);
	return simplified;
}

} // namespace

struct PmuxOptPass : public Pass {
	PmuxOptPass() : Pass("pmux_opt", "restructure PMUX cells to reduce logic cell count") {}

	void help() override
	{
		log("\n");
		log("    pmux_opt [options]\n");
		log("\n");
		log("Restructure PMUX cells into cheaper structures.\n");
		log("\n");
		log("    -v\n");
		log("        print details about matched cells\n");
		log("\n");
		log("    -fold_luts\n");
		log("        fold single-load NOT cells and constant-data MUX cells into LUT truth tables,\n");
		log("        move shared inverted DFF enables into synchronous self-load mode,\n");
		log("        and move shared disabled-state LUT inputs into DFF synchronous clear\n");
		log("\n");
	}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		bool verbose = false;
		bool fold_luts = false;
		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++) {
			if (args[argidx] == "-v") {
				verbose = true;
				continue;
			}
			if (args[argidx] == "-fold_luts") {
				fold_luts = true;
				continue;
			}
			break;
		}
		extra_args(args, argidx, design);

		log_header(design, "Executing PMUX_OPT pass.\n");

		CellTypes ct;
		ct.setup_internals();
		ct.setup_design(design);

		if (fold_luts) {
			ct.setup_stdcells();
			int64_t fold_start = PerformanceTimer::query();
			int n_folded = 0;
			int n_mux_folded = 0;
			int n_dff_enable = 0;
			int n_dff_clear = 0;
			int n_dff_zero_gate = 0;
			for (auto module : design->selected_modules())
				n_folded += fold_not_into_luts(module, ct, verbose);
			int64_t fold_not_done = PerformanceTimer::query();
			for (auto module : design->selected_modules())
				n_dff_enable += fold_inverted_enable_into_dff(module, ct, verbose);
			int64_t fold_enable_done = PerformanceTimer::query();
			for (auto module : design->selected_modules())
				n_dff_zero_gate += fold_zero_gate_into_dff_clear(module, ct, verbose);
			int64_t fold_zero_done = PerformanceTimer::query();
			for (auto module : design->selected_modules())
				n_dff_clear += fold_disabled_lut_into_dff(module, verbose);
			int64_t fold_clear_done = PerformanceTimer::query();
			for (auto module : design->selected_modules())
				n_mux_folded += fold_mux_into_luts(module, ct, verbose);
			int64_t fold_mux_done = PerformanceTimer::query();
			if (verbose)
				log("pmux_opt fold timing: not=%.3f enable=%.3f zero=%.3f clear=%.3f mux=%.3f ms\n",
				    (fold_not_done - fold_start) / 1e6,
				    (fold_enable_done - fold_not_done) / 1e6,
				    (fold_zero_done - fold_enable_done) / 1e6,
				    (fold_clear_done - fold_zero_done) / 1e6,
				    (fold_mux_done - fold_clear_done) / 1e6);
			log("pmux_opt: folded %d NOT cell(s) into LUTs.\n", n_folded);
			log("pmux_opt: moved %d inverted enable(s) into DFFs.\n", n_dff_enable);
			log("pmux_opt: moved %d shared zero-gate(s) into DFF synchronous clear.\n",
			    n_dff_zero_gate);
			log("pmux_opt: moved %d disabled-state LUT input(s) into DFF synchronous clear.\n",
			    n_dff_clear);
			log("pmux_opt: folded %d constant-data MUX cell(s) into LUTs.\n", n_mux_folded);
			return;
		}

		int n_pmux = 0, n_pairs = 0, n_skip = 0, n_rom = 0;
		int n_target_clear = 0;
		int n_sync_dff = 0;
		int n_shift_decode = 0;

		for (auto module : design->selected_modules()) {
			SigMap sigmap(module);

			int64_t module_start = PerformanceTimer::query();
			n_sync_dff += lower_sync_reset_dffs_to_target(module, verbose);
			int64_t after_sync = PerformanceTimer::query();
			n_target_clear += fold_zero_mux_into_target_dff_clear(module, verbose);
			int64_t after_zero_mux = PerformanceTimer::query();
			n_rom += lower_constant_roms_to_luts(module, sigmap, ct, verbose);
			int64_t after_rom = PerformanceTimer::query();
			n_shift_decode += lower_constant_power_shift_decoders(module, sigmap, verbose);
			int64_t after_shift = PerformanceTimer::query();

			dict<SigBit, Cell *> bit2cell;
				for (auto cell : module->cells())
					for (auto &conn : cell->connections())
						if (ct.cell_output(cell->type, conn.first))
							for (auto bit : sigmap(conn.second))
								bit2cell[bit] = cell;

			convert_priority_mux_chains(module, sigmap, ct, verbose);

			std::vector<Cell *> pmuxes;
			for (auto cell : module->cells())
				if (cell->type == ID($pmux))
					pmuxes.push_back(cell);
			n_pmux += pmuxes.size();

			pool<IdString> done;

			for (size_t ia = 0; ia < pmuxes.size(); ia++) {
				Cell *c1 = pmuxes[ia];
				if (done.count(c1->name) || !module->cells_.count(c1->name))
					continue;

				int width = GetSize(c1->getPort(ID::Y));
				int nsel = GetSize(c1->getPort(ID::S));
				if (width < 1 || nsel < 4 || GetSize(c1->getPort(ID::B)) != width * nsel)
					continue;

				SigSpec s1 = sigmap(c1->getPort(ID::S));
				SigSpec b1 = sigmap(c1->getPort(ID::B));

				std::vector<DecodeBit> dec(nsel);
				bool ok = true;
				for (int i = 0; i < nsel; i++)
					if (!analyze_select_bit(s1[i], sigmap, bit2cell, module, dec[i])) {
						ok = false;
						break;
					}
				if (!ok) {
					n_skip++;
					continue;
				}

				SigSpec bus = dec[0].bus;
				int bw = GetSize(bus);
				if (bw < 2 || bw > 8 || (1 << bw) != nsel) {
					n_skip++;
					continue;
				}

				std::vector<int> pos(nsel, -1);
				bool bad = false;
				for (int i = 0; i < nsel; i++) {
					int code = dec[i].code;
					if (code < 0 || code >= nsel || pos[code] != -1 || dec[i].bus != bus) {
						bad = true;
						break;
					}
					pos[code] = i;
				}
				if (bad) {
					n_skip++;
					continue;
				}

				Cell *partner = nullptr;
				for (size_t ib = ia + 1; ib < pmuxes.size(); ib++) {
					Cell *c2 = pmuxes[ib];
					if (done.count(c2->name) || !module->cells_.count(c2->name))
						continue;
					if (GetSize(c2->getPort(ID::Y)) != width)
						continue;
					if (GetSize(c2->getPort(ID::S)) != nsel)
						continue;
					if (sigmap(c2->getPort(ID::S)) != s1)
						continue;
					SigSpec b2 = sigmap(c2->getPort(ID::B));
					if (GetSize(b2) != width * nsel)
						continue;

					bool swapped = true;
					for (int k = 1; k < nsel / 2 && swapped; k++) {
						int e0 = pos[2 * k], e1 = pos[2 * k + 1];
						if (!slice_equal(b1, e0 * width, b2, e1 * width, width, sigmap) ||
						    !slice_equal(b1, e1 * width, b2, e0 * width, width, sigmap))
							swapped = false;
					}
					if (swapped)
						partner = c2;
					break;
				}
				if (partner == nullptr) {
					n_skip++;
					continue;
				}

				SigSpec b2 = sigmap(partner->getPort(ID::B));
				SigSpec s0 = s1[pos[0]];

				std::vector<SigSpec> ilv, qlv;
				for (int k = 0; k < nsel / 2; k++) {
					if (k == 0) {
						ilv.push_back(b2.extract(pos[1] * width, width));
						qlv.push_back(b1.extract(pos[1] * width, width));
					} else {
						ilv.push_back(b1.extract(pos[2 * k] * width, width));
						qlv.push_back(b1.extract(pos[2 * k + 1] * width, width));
					}
				}

				SigSpec isel = build_tree(module, bus.extract(1, bw - 1), ilv);
				SigSpec qsel = build_tree(module, bus.extract(1, bw - 1), qlv);
				SigSpec y1 = module->Mux(NEW_ID, isel, qsel, bus[0]);
				SigSpec y2 = module->Mux(NEW_ID, qsel, isel, bus[0]);
				y1 = module->Mux(NEW_ID, y1, b1.extract(pos[0] * width, width), s0);
				y2 = module->Mux(NEW_ID, y2, b2.extract(pos[0] * width, width), s0);

				if (verbose)
					log("  merged %s + %s: %d-way dual PMUX over %d-bit bus\n",
					    log_id(c1->name), log_id(partner->name), nsel, bw);

				module->connect(c1->getPort(ID::Y), y1);
				module->connect(partner->getPort(ID::Y), y2);
				module->remove(c1);
				module->remove(partner);
				done.insert(c1->name);
				done.insert(partner->name);
				n_pairs++;
			}

			SigMap post_map(module);
			pool<SigBit> partial_decoders;
			simplify_partial_pmux(module, post_map, ct, verbose, partial_decoders);
			if (!partial_decoders.empty()) {
				narrow_gated_eq_decoders(module, post_map, ct, partial_decoders, verbose);
				SigMap factor_map(module);
				pool<SigBit> factor_outputs;
				for (auto bit : partial_decoders)
					factor_outputs.insert(factor_map(bit));
				factor_adjacent_eq(module, factor_map, factor_outputs, verbose);
			}
			SigMap enable_map(module);
			factor_common_enable_terms(module, enable_map, ct, verbose);
			if (verbose)
				log("pmux_opt timing: sync=%.3f zero_mux=%.3f rom=%.3f shift=%.3f rest=%.3f ms\n",
				    (after_sync - module_start) / 1e6,
				    (after_zero_mux - after_sync) / 1e6,
				    (after_rom - after_zero_mux) / 1e6,
				    (after_shift - after_rom) / 1e6,
				    (PerformanceTimer::query() - after_shift) / 1e6);
		}

		if (n_sync_dff)
			log("pmux_opt: lowered %d synchronous-reset DFF bit(s) to target clear.\n",
			    n_sync_dff);
		if (n_target_clear)
			log("pmux_opt: moved %d zero-mux bit(s) into target DFF synchronous clear.\n",
			    n_target_clear);
		log("pmux_opt: %d $pmux cell(s), merged %d pair(s), skipped %d, lowered %d constant ROM(s).\n",
		    n_pmux, n_pairs, n_skip, n_rom);
		if (n_shift_decode)
			log("pmux_opt: lowered %d constant power-of-two shift decoder(s).\n", n_shift_decode);
	}
} PmuxOptPass;

PRIVATE_NAMESPACE_END
