#include <eosio/chain/webassembly/eos-vm-oc.hpp>
#include <eosio/chain/wasm_eosio_constraints.hpp>
#include <eosio/chain/wasm_eosio_injection.hpp>
#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/global_property_object.hpp>

#include <vector>
#include <iterator>

namespace eosio { namespace chain { namespace webassembly { namespace eosvmoc {

class eosvmoc_instantiated_module : public wasm_instantiated_module_interface {
   public:
      eosvmoc_instantiated_module(const digest_type& code_hash, const uint8_t& vm_version, eosvmoc_runtime& wr) :
         _code_hash(code_hash),
         _vm_version(vm_version),
         _eosvmoc_runtime(wr)
      {

      }

      ~eosvmoc_instantiated_module() {
         _eosvmoc_runtime.cc.free_code(_code_hash, _vm_version);
      }

      void apply(apply_context& context) override {
         const code_descriptor* const cd = _eosvmoc_runtime.cc.get_descriptor_for_code_sync(_code_hash, _vm_version);
         EOS_ASSERT(cd, wasm_execution_error, "EOS VM OC instantiation failed");

         uint64_t max_call_depth = eosio::chain::wasm_constraints::maximum_call_depth+1;
         uint64_t max_pages = eosio::chain::wasm_constraints::maximum_linear_memory/eosio::chain::wasm_constraints::wasm_page_size;
         if(context.control.is_builtin_activated(builtin_protocol_feature_t::configurable_wasm_limits)) {
            const wasm_config& config = context.control.get_global_properties().wasm_configuration;
            max_call_depth = config.max_call_depth;
            max_pages = config.max_pages;
         }
         _eosvmoc_runtime.exec.execute(*cd, _eosvmoc_runtime.mem, &context, max_call_depth, max_pages,
                                       [&context](void (*fn)(void*), void* data) {
                                          context.trx_context.transaction_timer.set_expiration_callback(fn, data);
                                       }, [&context]{ context.trx_context.checktime(); },
                                       context.get_receiver().to_uint64_t(), context.get_action().account.to_uint64_t(), context.get_action().name.to_uint64_t());
      }

      const digest_type              _code_hash;
      const uint8_t                  _vm_version;
      eosvmoc_runtime&               _eosvmoc_runtime;
};

eosvmoc_runtime::eosvmoc_runtime(const boost::filesystem::path data_dir, const eosvmoc::config& eosvmoc_config, const chainbase::database& db)
   : cc(data_dir, eosvmoc_config, [&db](const digest_type& id, uint8_t vm_version) -> std::string_view {
                                    auto * p = db.find<code_object,by_code_hash>(boost::make_tuple(id, 0, vm_version));
                                    if(p) return { p->code.data(), p->code.size() };
                                    else return {};
                                  }), exec(cc), mem(wasm_constraints::maximum_linear_memory/wasm_constraints::wasm_page_size, eosvmoc::get_intrinsic_map()) {
}

eosvmoc_runtime::~eosvmoc_runtime() {
}

std::unique_ptr<wasm_instantiated_module_interface> eosvmoc_runtime::instantiate_module(const char* code_bytes, size_t code_size, std::vector<uint8_t> initial_memory,
                                                                                        const digest_type& code_hash, const uint8_t& vm_type, const uint8_t& vm_version) {

   return std::make_unique<eosvmoc_instantiated_module>(code_hash, vm_type, *this);
}

//never called. EOS VM OC overrides eosio_exit to its own implementation
void eosvmoc_runtime::immediately_exit_currently_running_module() {}

}}}}
