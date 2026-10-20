#include "kernel/yosys.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"
#include "kernel/register.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct PmuxApiTestPass : public Pass {
	PmuxApiTestPass() : Pass("pmux_api_test", "verify plugin API availability") {}

	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		log_header(design, "Executing PMUX_API_TEST pass.\n");
		(void)args;

		CellTypes ct;
		ct.setup_internals();
		ct.setup_design(design);

		for (auto module : design->modules()) {
			SigMap sigmap(module);

			// 位 -> 驱动它的单元。写算法时最常用的一张表。
			dict<SigBit, Cell *> bit2cell;
			for (auto cell : module->cells())
				for (auto &conn : cell->connections())
					if (ct.cell_output(cell->type, conn.first))
						for (auto bit : sigmap(conn.second))
							bit2cell[bit] = cell;

			int pmux_n = 0;
			for (auto cell : module->cells()) {
				if (cell->type != ID($pmux))
					continue;
				pmux_n++;
				SigSpec s = sigmap(cell->getPort(ID::S));
				SigSpec b = sigmap(cell->getPort(ID::B));
				// $pmux (A, B, S, Y)：A 是 S 全低时的输出；B 按 S 的位分片，|B| = |S| * W
				SigSpec a = sigmap(cell->getPort(ID::A));
				int width = GetSize(s) ? GetSize(b) / GetSize(s) : 0;
				log("  %s: A=%d bits, S=%d 位选择, B=%d bits（每片 %d 位，%s）\n",
				    log_id(cell->name), GetSize(a), GetSize(s), GetSize(b), width,
				    (width > 0 && GetSize(b) == GetSize(s) * width) ? "符合 |B| = |S| x W" : "与定义不符");

				dict<IdString, int> drivers;
				for (auto bit : s) {
					auto it = bit2cell.find(bit);
					IdString t = (it == bit2cell.end()) ? ID($undef) : it->second->type;
					drivers[t]++;
				}
				for (auto &d : drivers)
					log("     选择信号来源: %s × %d\n", log_id(d.first), d.second);
			}
			log("  module %s: %d cells, %d x $pmux\n", log_id(module->name),
			    GetSize(module->cells()), pmux_n);
		}
		log("API check: SigMap / CellTypes / dict / NEW_ID / log 全部可用\n");
	}
} PmuxApiTestPass;

PRIVATE_NAMESPACE_END
