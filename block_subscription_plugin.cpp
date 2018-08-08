#include <eosio/block_subscription_plugin/block_subscription_plugin.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/block.hpp>
#include <boost/signals2/connection.hpp>
#include <fc/io/json.hpp>
#include "tcp_server.hpp"

#define CHUNK_SIZE 500

namespace eosio {
	static appbase::abstract_plugin& _block_subscription_plugin = app().register_plugin<block_subscription_plugin>();

	class block_subscription_plugin_impl {
	private:
		struct client_t {
			boost::asio::ip::tcp::socket* const socket;
			int32_t last_block;
			std::string addr;
		};

		chain_plugin& chain_plugin_ref;
		std::function<fc::optional<abi_serializer>(const account_name&)> resolver;
		std::vector<client_t*> clients;
		tcp_server server;
		std::mutex mutex;
		fc::optional<boost::signals2::scoped_connection> accepted_block_connection;

		std::string block_to_json(const chain::signed_block& block) const {
			fc::variant output;
			abi_serializer::to_variant(block, output, this->resolver, this->chain_plugin_ref.get_abi_serializer_max_time());
			return fc::json::to_string(fc::mutable_variant_object(output.get_object())
				("id", block.id())
				("block_num", block.block_num())
			);
		}

		void on_block() {
			this->mutex.lock();
			try {
				std::for_each(this->clients.begin(), this->clients.end(), [this](client_t* client) {
					int32_t from_block = client->last_block+1;
					int32_t to_block = this->chain_plugin_ref.chain().last_irreversible_block_num();
					ilog("got irreversible: " + std::to_string(to_block));
					if (to_block - from_block >= CHUNK_SIZE) to_block = from_block + CHUNK_SIZE;
					for (int32_t i = from_block; i <= to_block; i++) {
						this->server.send(client->socket, this->block_to_json(*this->chain_plugin_ref.chain().fetch_block_by_number(i)));
					}
					client->last_block = std::max(client->last_block, to_block);
				});
			} catch (...) {}
			this->mutex.unlock();
		}

	public:
		block_subscription_plugin_impl() :
			chain_plugin_ref(app().get_plugin<chain_plugin>()),
			resolver([this](const account_name& name) -> fc::optional<abi_serializer> {
				const chain::account_object* account = this->chain_plugin_ref.chain().db().find<chain::account_object, chain::by_name>(name);
				auto time = this->chain_plugin_ref.get_abi_serializer_max_time();
				if (account != nullptr) {
					abi_def abi;
					if (abi_serializer::to_abi(account->abi, abi)) {
						return abi_serializer(abi, time);
					}
				}
				return fc::optional<abi_serializer>();
			}),
			server(app().get_io_service(), 56732) // TODO: load port from config
		{
			this->server.on_message([this](boost::asio::ip::tcp::socket* const socket, std::stringstream data) {
				char msg;
				data >> msg;
				switch (msg) {
					case 's': {
						for (client_t* client : this->clients) {
							if (client->socket == socket) return;
						}
						int32_t last_block = this->chain_plugin_ref.chain().last_irreversible_block_num();
						int32_t from_block;
						data >> from_block;
						if (from_block == 0) {
							from_block = last_block;
						}
						client_t* client = new client_t{
							.socket = socket,
							.last_block = from_block,
							.addr = socket->remote_endpoint().address().to_string()
						};
						this->mutex.lock();
						this->clients.push_back(client);
						this->mutex.unlock();
						ilog("client '" + client->addr + "' subscribed to blocks");
						break;
					}
				}
			});
			this->server.on_disconnect([this](boost::asio::ip::tcp::socket* const socket) {
				this->mutex.lock();
				this->clients.erase(std::remove_if(this->clients.begin(), this->clients.end(), [socket](client_t* client) {
					if (client->socket == socket) {
						ilog("client '" + client->addr + "' unsubscribed from blocks");
						delete client;
						return true;
					}
					return false;
				}), this->clients.end());
				this->mutex.unlock();
			});
		}

		void init() {
			this->accepted_block_connection.emplace(
				this->chain_plugin_ref.chain().accepted_block.connect([this](const auto& bsp) {
					this->on_block();
				})
			);
		}

		void destroy() {
			this->accepted_block_connection.reset();
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
		my->init();
	}

	void block_subscription_plugin::plugin_shutdown() {
		my->destroy();
	}
}
