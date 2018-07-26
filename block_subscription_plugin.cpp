#include <eosio/block_subscription_plugin/block_subscription_plugin.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/block.hpp>
#include <boost/signals2/connection.hpp>
#include <fc/io/json.hpp>
#include "tcp_server.hpp"

namespace eosio {
	static appbase::abstract_plugin& _block_subscription_plugin = app().register_plugin<block_subscription_plugin>();

	class block_subscription_plugin_impl {
	private:
		struct client_t {
			connection* const conn;
			uint32_t last_block;
		};

		std::function<fc::optional<abi_serializer>(const account_name&)> resolver;
		std::vector<client_t*> clients;
		tcp_server server;

		std::string block_to_json(chain::signed_block& block) {
			fc::variant output;
			abi_serializer::to_variant(block, output, this->resolver, this->chain_plugin_ref.get_abi_serializer_max_time());
			return fc::json::to_string(output);
		}

	public:
		fc::optional<boost::signals2::scoped_connection> accepted_block_connection;
		chain_plugin& chain_plugin_ref;

		block_subscription_plugin_impl() :
			resolver([](const account_name& name) {
				return fc::optional<abi_serializer>();
			}),
			server(app().get_io_service(), 56732), // TODO: load port from config
			chain_plugin_ref(app().get_plugin<chain_plugin>())
		{
			server.on_message([this](connection* const conn, std::stringstream data) {
				char msg;
				data >> msg;
				switch (msg) {
					case 's': {
						for (client_t* client : this->clients) {
							if (client->conn == conn) return;
						}
						uint32_t last_block = this->chain_plugin_ref.chain().fork_db_head_block_num();
						client_t* client = new client_t{
							.conn = conn,
							.last_block = last_block
						};
						uint32_t from_block;
						data >> from_block;
						for (uint32_t i = from_block; i < last_block; i++) {
         					client->conn->send(this->block_to_json(*this->chain_plugin_ref.chain().fetch_block_by_number(i)));
						}
						this->clients.push_back(client);
						break;
					}
				}
			});
		}

		void on_block(uint32_t block_num) {
			auto it = this->clients.end();
			std::for_each(this->clients.rbegin(), this->clients.rend(), [this, &it, block_num](client_t* client) {
				it--;
				if (!client->conn->enabled) {
					delete *it;
					this->clients.erase(it);
					return;
				}
				for (uint32_t i = client->last_block; i < block_num; i++) {
					client->conn->send(this->block_to_json(*this->chain_plugin_ref.chain().fetch_block_by_number(i+1)));
				}
				client->last_block = block_num;
			});
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
				my->on_block(bsp->block->block_num());
			})
		);
	}

	void block_subscription_plugin::plugin_shutdown() {
		my->accepted_block_connection.reset();
	}
}
