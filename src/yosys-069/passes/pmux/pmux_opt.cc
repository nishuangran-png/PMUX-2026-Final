/*
 * pmux_opt — PMUX restructuring for the 2026 FPGA competition (topic 2).
 *
 * v2: PMUX restructuring for the official Cyclone IV flow.
 *
 * Implemented structural rewrites:
 *   1. dual-$pmux sharing;
 *   2. priority $mux-chain folding into a balanced mux tree or one $pmux;
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

class ConstantRomSynthesizer
{
	Module *module;
	SigSpec address;
	int input_count;
	std::vector<int> order;
	std::map<LutFunction, LutNode> memo;

	int choose_split(uint32_t support) const
	{
		for (int input : order)
			if (support & (1u << input))
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
		node.sig = module->addWire(NEW_ID);
		node.cell = module->addLut(NEW_ID, inputs, node.sig, std::move(table));
		node.cell->set_bool_attribute(ID(pmux_deferred_lut));
		node.width = GetSize(inputs);
		return node;
	}

	LutNode merge_children(const LutNode &low, const LutNode &high, int split);

public:
	ConstantRomSynthesizer(Module *module, SigSpec address, int input_count, std::vector<int> order)
	    : module(module), address(std::move(address)), input_count(input_count), order(std::move(order))
	{
	}

	LutNode synthesize(const std::vector<uint8_t> &table);
	LutNode synthesize_gated(const std::vector<uint8_t> &table, SigBit gate, State base);
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
		lut.set(index, (select ? high_value : low_value) ? State::S1 : State::S0);
	}
	return add_lut(inputs, std::move(lut));
}

LutNode ConstantRomSynthesizer::synthesize(const std::vector<uint8_t> &table)
{
	uint32_t support = function_support(table, input_count);
	if (!support)
		return make_constant(table[0] ? State::S1 : State::S0);

	LutFunction key = {support, compress_function(table, support)};
	auto found = memo.find(key);
	if (found != memo.end())
		return found->second;

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
		int split = choose_split(support);
		LutNode low = synthesize(cofactor(table, split, false));
		LutNode high = synthesize(cofactor(table, split, true));
		node = merge_children(low, high, split);
	}
	memo[key] = node;
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
		int split = choose_split(support);
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
			lut.set(index, (enable ? (select ? high_value : low_value) : base == State::S1)
			                   ? State::S1 : State::S0);
		}
		return add_lut(inputs, std::move(lut));
	} else {
		data = synthesize(table);
	}

	if (data.is_const) {
		State enabled = data.value;
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
		bool enable = index & 2;
		lut.set(index, (enable ? data_value : base == State::S1) ? State::S1 : State::S0);
	}
	SigSpec inputs;
	inputs.append(data.sig);
	inputs.append(gate);
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
		std::vector<int> lut_order = choose_lut_order(output_functions, addr_bits);
		int estimated_luts = estimate_rom_lut_cost(output_functions, addr_bits, lut_order);
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
			ConstantRomSynthesizer synthesizer(module, port_addr, addr_bits, lut_order);

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

				SigSpec lut_output = module->addWire(NEW_ID, width);
				SigBit select = sigmap(cell->getPort(ID::S))[0];
				for (int bit = 0; bit < width; bit++) {
					LutNode node = synthesizer.synthesize_gated(output_functions[indices[bit]],
					                                                 select, a_data[bit].data);
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
				else
					module->connect(port.data[bit], node.sig);
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

SigBit make_and(Module *module, const std::vector<SigBit> &inputs)
{
	if (inputs.empty())
		return State::S1;
	if (inputs.size() == 1)
		return inputs.front();
	SigSpec sig(inputs);
	for (auto cell : module->cells())
		if (cell->type.in(ID($reduce_and), ID($and), ID($logic_and)) &&
		    GetSize(cell->getPort(ID::Y)) == 1) {
			SigSpec cell_inputs = cell->getPort(ID::A);
			if (cell->hasPort(ID::B))
				cell_inputs.append(cell->getPort(ID::B));
			if (same_multiset(cell_inputs, sig))
				return cell->getPort(ID::Y)[0];
		}

	SigBit y = module->addWire(NEW_ID);
	module->addReduceAnd(NEW_ID, sig, y);
	return y;
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

// Turn a priority chain of scalar-select $mux cells into a lower-cost form.
// Four-level chains become two priority pairs plus a group mux; longer chains
// use one $pmux with masked priority selects.  This targets priority decoders
// that proc leaves as a long mux chain, while deliberately ignoring short or
// vector-select chains where the rewrite is unlikely to pay off.
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

		SigSpec out = outer->getPort(ID::Y);
		if (chain.size() == 4) {
			// A four-level priority chain maps more cheaply as two priority
			// pairs followed by a group mux.  The equivalent $pmux form needs
			// a large masked-select network before ABC and raises peak memory.
			SigBit s0 = selects[0][0], s1 = selects[1][0];
			SigBit s2 = selects[2][0], s3 = selects[3][0];
			SigBit upper_active = make_or(module, {s0, s1});
			SigBit lower_active = make_or(module, {s2, s3});
			SigSpec upper = module->Mux(NEW_ID, chain[1]->getPort(ID::B),
			                             chain[0]->getPort(ID::B), s0);
			SigSpec lower = module->Mux(NEW_ID, chain[3]->getPort(ID::B),
			                             chain[2]->getPort(ID::B), s2);
			SigSpec lower_or_base = module->Mux(NEW_ID, base, lower, lower_active);
			module->connect(out, module->Mux(NEW_ID, lower_or_base, upper, upper_active));
		} else {
			SigSpec pmux_b, pmux_s;
			std::vector<SigBit> inverted_prior;
			for (size_t i = 0; i < chain.size(); i++) {
				SigBit sel = selects[i][0];
				if (i == 0) {
					pmux_s.append(sel);
				} else {
					inverted_prior.push_back(make_not(module, selects[i - 1][0]));
					std::vector<SigBit> terms = inverted_prior;
					terms.push_back(sel);
					pmux_s.append(make_and(module, terms));
				}
				pmux_b.append(chain[i]->getPort(ID::B));
			}

			Cell *pmux = module->addPmux(NEW_ID, base, pmux_b, pmux_s, out);
			pmux->add_strpool_attribute(ID::src, outer->get_strpool_attribute(ID::src));
		}

		for (auto mux : chain) {
			done.insert(mux->name);
			module->remove(mux);
		}
		converted++;
		if (verbose)
			log("  converted %d-level priority $mux chain at %s into %s\n",
			    GetSize(chain), log_signal(out),
			    chain.size() == 4 ? "a balanced mux tree" : "one $pmux");
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
		log("        fold single-load NOT cells and constant-data MUX cells into LUT truth tables\n");
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
			int n_folded = 0;
			int n_mux_folded = 0;
			for (auto module : design->selected_modules())
				n_folded += fold_not_into_luts(module, ct, verbose);
			for (auto module : design->selected_modules())
				n_mux_folded += fold_mux_into_luts(module, ct, verbose);
			log("pmux_opt: folded %d NOT cell(s) into LUTs.\n", n_folded);
			log("pmux_opt: folded %d constant-data MUX cell(s) into LUTs.\n", n_mux_folded);
			return;
		}

		int n_pmux = 0, n_pairs = 0, n_skip = 0, n_rom = 0;

		for (auto module : design->selected_modules()) {
			SigMap sigmap(module);

			n_rom += lower_constant_roms_to_luts(module, sigmap, ct, verbose);

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
		}

		log("pmux_opt: %d $pmux cell(s), merged %d pair(s), skipped %d, lowered %d constant ROM(s).\n",
		    n_pmux, n_pairs, n_skip, n_rom);
	}
} PmuxOptPass;

PRIVATE_NAMESPACE_END
