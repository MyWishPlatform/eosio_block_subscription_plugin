#include <eosio/block_subscription_plugin/block_subscription_plugin.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>
#include <eosio/chain/plugin_interface.hpp>
#include <eosio/chain/block.hpp>
#include <boost/signals2/connection.hpp>
#include <boost/asio.hpp>
#include <fc/io/json.hpp>

namespace eosio {
	static appbase::abstract_plugin& _block_subscription_plugin = app().register_plugin<block_subscription_plugin>();

	class tcp_server {
	private:
		struct connection {
			bool enabled;
			boost::asio::ip::tcp::socket socket;

			connection(boost::asio::io_service& io_service) :
				enabled(true),
				socket(io_service)
			{}

			void send(std::string string) {
				boost::asio::write(this->socket, boost::asio::buffer(string));
			}
		};

		boost::asio::ip::tcp::acceptor acceptor;
		std::vector<connection*> connections;

		void start_accept() {
			connection* conn = new connection(this->acceptor.get_io_service());
			this->acceptor.async_accept(conn->socket, [this, conn](boost::system::error_code ec) {
				ilog("client subscribed to blocks");
				this->connections.push_back(conn);
				char* buffer = new char[8];
				conn->socket.async_receive(boost::asio::buffer(buffer, 8), 0, [conn, buffer](boost::system::error_code err, size_t) {
					if (err == boost::asio::error::eof || err == boost::asio::error::connection_reset) {
						conn->enabled = false;
						delete[] buffer;
						ilog("client unsubscribed from blocks");
					}
				});
				this->start_accept();
			});
		}

	public:
		tcp_server(uint16_t port) :
			acceptor(app().get_io_service())
		{
			this->acceptor.open(boost::asio::ip::tcp::v4());
			this->acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
			this->acceptor.bind({{}, port});
			this->acceptor.listen();
			this->start_accept();
		}

		~tcp_server() {
			for (connection* conn : this->connections) {
				delete conn;
			}
			this->acceptor.close();
		}

		void broadcast(std::string string) {
			auto it = this->connections.end();
			std::for_each(this->connections.rbegin(), this->connections.rend(), [this, &it, string](connection* conn) {
				it--;
				if (conn->enabled) {
					conn->send(string);
				} else {
					this->connections.erase(it);
				}
			});
		}
	};

	class block_subscription_plugin_impl {
	private:
		fc::optional<abi_serializer> (*resolver)(const account_name&);
		tcp_server server;

	public:
		fc::optional<boost::signals2::scoped_connection> accepted_block_connection;
		chain_plugin& chain_plugin_ref;

		block_subscription_plugin_impl() :
			resolver([](const account_name& name) {
				return fc::optional<abi_serializer>();
			}),
			server(56732), // TODO: load from config
			chain_plugin_ref(app().get_plugin<chain_plugin>())
		{
		}

		void on_block(const chain::signed_block_ptr& block) {
			fc::variant output;
			abi_serializer::to_variant(static_cast<const chain::signed_block&>(*block), output, this->resolver, this->chain_plugin_ref.get_abi_serializer_max_time());
			this->server.broadcast(fc::json::to_string(output));
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
