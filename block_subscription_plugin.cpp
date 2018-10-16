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
		struct client_irreversible_t {
			boost::asio::ip::tcp::socket* const socket;
			int32_t last_block;
			std::string addr;
		};

		struct client_accepted_t {
			boost::asio::ip::tcp::socket* const socket;
			std::string addr;
		};

		chain_plugin& chain_plugin_ref;
		std::function<fc::optional<abi_serializer>(const account_name&)> resolver;
		std::vector<client_irreversible_t*> clients_irreversible;
		std::vector<client_accepted_t*> clients_accepted;
		tcp_server server;
		boost::asio::deadline_timer timer;
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

		void on_irreversible_block() {
			this->mutex.lock();
			try {
				std::for_each(this->clients_irreversible.begin(), this->clients_irreversible.end(), [this](client_irreversible_t* client) {
					int32_t from_block = client->last_block+1;
					int32_t to_block = this->chain_plugin_ref.chain().last_irreversible_block_num();
					if (to_block - from_block >= CHUNK_SIZE) to_block = from_block + CHUNK_SIZE;
					client->last_block = std::max(client->last_block, to_block);
					if (to_block >= from_block) ilog("Sending #" + std::to_string(from_block) + " - #" + std::to_string(to_block) + " to client '" + client->addr + "'; client's last_block now is #" + std::to_string(client->last_block) + "'");
					for (int32_t i = from_block; i <= to_block; i++) {
						this->server.send(client->socket, this->block_to_json(*this->chain_plugin_ref.chain().fetch_block_by_number(i)));
					}
				});
			} catch(const std::exception& e) {
				ilog(e.what());
			}
			this->mutex.unlock();
		}

		void on_accepted_block(const chain::signed_block& block) {
			this->mutex.lock();
			try {
				std::for_each(this->clients_accepted.begin(), this->clients_accepted.end(), [this, block](client_accepted_t* client) {
					this->server.send(client->socket, this->block_to_json(block));
				});
			} catch (...) {}
			this->mutex.unlock();
		}

	public:
		block_subscription_plugin_impl(uint16_t port) :
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
			server(app().get_io_service(), port),
			timer(app().get_io_service(), boost::posix_time::seconds(1))
		{
			this->server.on_message([this](boost::asio::ip::tcp::socket* const socket, std::string string, std::stringstream data) {
				ilog(string);
				char msg;
				data >> msg;
				switch (msg) {
					case 'n': {
						for (client_accepted_t* client : this->clients_accepted) {
							if (client->socket == socket) return;
						}
						client_accepted_t* client = new client_accepted_t{
							.socket = socket,
							.addr = socket->remote_endpoint().address().to_string()
						};
						this->mutex.lock();
						this->clients_accepted.push_back(client);
						this->mutex.unlock();
						ilog("client '" + client->addr + "' subscribed to accepted blocks");
						break;
					}
					case 's': {
						for (client_irreversible_t* client : this->clients_irreversible) {
							if (client->socket == socket) return;
						}
						int32_t last_block = this->chain_plugin_ref.chain().last_irreversible_block_num();
						int32_t from_block;
						data >> from_block;
						if (from_block == 0) {
							from_block = last_block;
						}
						client_irreversible_t* client = new client_irreversible_t{
							.socket = socket,
							.last_block = from_block,
							.addr = socket->remote_endpoint().address().to_string()
						};
						this->mutex.lock();
						this->clients_irreversible.push_back(client);
						this->mutex.unlock();
						ilog("client '" + client->addr + "' subscribed to irreversible blocks from block '" + std::to_string(from_block) + "'");
						break;
					}
				}
			});
			this->server.on_disconnect([this](boost::asio::ip::tcp::socket* const socket) {
				this->mutex.lock();
				this->clients_irreversible.erase(std::remove_if(this->clients_irreversible.begin(), this->clients_irreversible.end(), [socket](client_irreversible_t* client) {
					if (client->socket == socket) {
						ilog("client '" + client->addr + "' unsubscribed from blocks");
						delete client;
						return true;
					}
					return false;
				}), this->clients_irreversible.end());
				this->clients_accepted.erase(std::remove_if(this->clients_accepted.begin(), this->clients_accepted.end(), [socket](client_accepted_t* client) {
					if (client->socket == socket) {
						ilog("client '" + client->addr + "' unsubscribed from blocks");
						delete client;
						return true;
					}
					return false;
				}), this->clients_accepted.end());
				this->mutex.unlock();
			});
			this->accepted_block_connection.emplace(
				this->chain_plugin_ref.chain().accepted_block.connect([this](const auto& bsp) {
					this->on_accepted_block(*bsp->block);
				})
			);
			this->send_irreversible();
		}

		void send_irreversible() {
			this->timer.expires_from_now(boost::posix_time::seconds(1));
			this->timer.async_wait([&](auto err) {
				this->on_irreversible_block();
				this->send_irreversible();
			});
		}

		~block_subscription_plugin_impl() {
			this->accepted_block_connection.reset();
		}
	};

	block_subscription_plugin::block_subscription_plugin() {}

	block_subscription_plugin::~block_subscription_plugin() {}

	void block_subscription_plugin::set_program_options(options_description&, options_description& cfg) {
		cfg.add_options()
			("block-subscription-port", bpo::value<uint16_t>()->default_value(56732),
			"Port to listen to");
	}

	void block_subscription_plugin::plugin_initialize(const variables_map& options) {
		uint16_t port = options["block-subscription-port"].as<uint16_t>();
		ilog("starting block_subscription_plugin at port " + std::to_string(port));
		this->my = new block_subscription_plugin_impl(port);
	}

	void block_subscription_plugin::plugin_startup() {}

	void block_subscription_plugin::plugin_shutdown() {
		delete this->my;
	}
}
