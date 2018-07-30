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
			boost::asio::ip::tcp::socket* const socket;
			uint32_t last_block;
		};

		std::function<fc::optional<abi_serializer>(const account_name&)> resolver;
		std::set<client_t*> clients;
		tcp_server server;

		std::string block_to_json(const chain::signed_block& block) const {
			fc::variant output;
			abi_serializer::to_variant(block, output, this->resolver, this->chain_plugin_ref.get_abi_serializer_max_time());
			return fc::json::to_string(fc::mutable_variant_object(output.get_object())
				("id", block.id())
				("block_num", block.block_num())
			);
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
			this->server.on_message([this](boost::asio::ip::tcp::socket* const socket, std::stringstream data) {
				char msg;
				data >> msg;
				switch (msg) {
					case 's': {
						for (client_t* client : this->clients) {
							if (client->socket == socket) return;
						}
						uint32_t last_block = this->chain_plugin_ref.chain().fork_db_head_block_num();
						uint32_t from_block;
						data >> from_block;
						if (last_block - from_block > 10000) return;
						client_t* client = new client_t{
							.socket = socket,
							.last_block = last_block
						};
						for (uint32_t i = from_block; i <= last_block; i++) {
         					this->server.send(client->socket, this->block_to_json(*this->chain_plugin_ref.chain().fetch_block_by_number(i)));
						}
						this->clients.insert(client);
						ilog("client '" + socket->remote_endpoint().address().to_string() + "' subscribed to blocks");
						break;
					}
				}
			});
			this->server.on_disconnect([this](boost::asio::ip::tcp::socket* const socket) {
				for (auto it = this->clients.begin(); it != this->clients.end(); it++) {
					if ((*it)->socket == socket) {
						this->clients.erase(it);
						delete *it;
						ilog("client '" + socket->remote_endpoint().address().to_string() + "' unsubscribed from blocks");
						break;
					}
				}
			});
		}

		void on_block(chain::signed_block& block) {
			std::for_each(this->clients.begin(), this->clients.end(), [this, block](client_t* client) {
				for (uint32_t i = client->last_block+1; i < block.block_num(); i++) {
					this->server.send(client->socket, this->block_to_json(*this->chain_plugin_ref.chain().fetch_block_by_number(i)));
				}
				this->server.send(client->socket, this->block_to_json(block));
				client->last_block = block.block_num();
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
				my->on_block(*bsp->block);
			})
		);
	}

	void block_subscription_plugin::plugin_shutdown() {
		my->accepted_block_connection.reset();
	}
}
