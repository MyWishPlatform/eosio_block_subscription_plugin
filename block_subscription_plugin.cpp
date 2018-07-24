#include <eosio/block_subscription_plugin/block_subscription_plugin.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/block.hpp>
#include <boost/signals2/connection.hpp>
#include <fc/io/json.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

namespace eosio {
	static appbase::abstract_plugin& _block_subscription_plugin = app().register_plugin<block_subscription_plugin>();

	class block_subscription_plugin_impl {
	private:
		websocketpp::server<websocketpp::config::asio> server;
		std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> connections;
		fc::optional<abi_serializer> (*resolver)(const account_name&);

	public:
		fc::optional<boost::signals2::scoped_connection> accepted_block_connection;
		chain_plugin& chain_plugin_ref;

		block_subscription_plugin_impl() :
			resolver([](const account_name& name) {
				return fc::optional<abi_serializer>();
			}),
			chain_plugin_ref(app().get_plugin<chain_plugin>())
		{
			this->server.clear_access_channels(websocketpp::log::alevel::all);
			this->server.init_asio(&app().get_io_service());
			this->server.set_reuse_addr(true);
			this->server.set_open_handler([this](websocketpp::connection_hdl hdl) {
				this->connections.insert(hdl);
			});
			this->server.set_close_handler([this](websocketpp::connection_hdl hdl) {
				this->connections.erase(hdl);
			});
			this->server.listen(8886); // TODO: load port from config
			this->server.start_accept();
		}

		void on_block(const chain::signed_block_ptr& block) {
			fc::variant output;
			abi_serializer::to_variant(static_cast<const chain::signed_block&>(*block), output, this->resolver, this->chain_plugin_ref.get_abi_serializer_max_time());
			std::string json = fc::json::to_string(output);
			for (auto connection : this->connections) {
				this->server.send(connection, json, websocketpp::frame::opcode::text);
			}
		}
	};

	block_subscription_plugin::block_subscription_plugin() :
		my(new block_subscription_plugin_impl())
	{}

	block_subscription_plugin::~block_subscription_plugin(){}

	void block_subscription_plugin::set_program_options(options_description&, options_description& cfg) {}

	void block_subscription_plugin::plugin_initialize(const variables_map& options) {}

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
