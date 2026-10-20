#include "kernel/yosys.h"
#include "kernel/register.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct PmuxHelloPass : public Pass {
	PmuxHelloPass() : Pass("pmux_hello", "smoke test for plugin building") {}
	void execute(std::vector<std::string> args, RTLIL::Design *design) override
	{
		log_header(design, "Executing PMUX_HELLO pass.\n");
		for (auto mod : design->modules()) {
			int n_mux = 0, n_pmux = 0;
			for (auto cell : mod->cells()) {
				if (cell->type == ID($_MUX_)) n_mux++;
				if (cell->type == ID($pmux)) n_pmux++;
			}
			log("  module %s: %d cells (%d x $_MUX_, %d x $pmux)\n",
			    log_id(mod->name), GetSize(mod->cells()), n_mux, n_pmux);
		}
	}
} PmuxHelloPass;

PRIVATE_NAMESPACE_END
