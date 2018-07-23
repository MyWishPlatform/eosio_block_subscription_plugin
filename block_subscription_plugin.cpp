#include <eosio/block_subscription_plugin/block_subscription_plugin.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/block.hpp>
#include <boost/signals2/connection.hpp>
#include <fc/io/json.hpp>

namespace eosio {
	static appbase::abstract_plugin& _block_subscription_plugin = app().register_plugin<block_subscription_plugin>();

	class block_subscription_plugin_impl {
	public:
		fc::optional<boost::signals2::scoped_connection> accepted_block_connection;
		chain_plugin& chain_plugin_ref;

		block_subscription_plugin_impl(chain_plugin& chain_plugin_ref) :
			chain_plugin_ref(chain_plugin_ref)
		{}

		void on_block(const chain::signed_block_ptr& block) {
			fc::variant output;
			fc::to_variant(static_cast<const chain::signed_block&>(*block), output);
			ilog(fc::json::to_string(output));
		}
	};

	block_subscription_plugin::block_subscription_plugin() :
		my(new block_subscription_plugin_impl(
			app().get_plugin<chain_plugin>()
		))
	{}
	block_subscription_plugin::~block_subscription_plugin(){}

	void block_subscription_plugin::set_program_options(options_description&, options_description& cfg) {}

	void block_subscription_plugin::plugin_initialize(const variables_map& options) {
	}

	void block_subscription_plugin::plugin_startup() {
		ilog("starting block_subscription_plugin");
		my->accepted_block_connection.emplace(
			my->chain_plugin_ref.chain().accepted_block.connect([this](const auto& bsp) {
				my->on_block(bsp->block);
			})
		);
	}

	void block_subscription_plugin::plugin_shutdown() {
		my->accepted_block_connection.reset();
	}
}
