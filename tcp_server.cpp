#include "tcp_server.hpp"
#include <fc/log/logger.hpp>

void tcp_server::do_accept() {
	connection* conn = new connection(this->acceptor.get_io_service());
	this->acceptor.async_accept(conn->socket, [this, conn](boost::system::error_code ec) {
		ilog("client subscribed to blocks");
		this->connections.push_back(conn);
		this->do_session(conn);
		this->do_accept();
	});
}

void tcp_server::do_session(connection* conn) {
	char* buffer = new char[32];
	conn->socket.async_receive(boost::asio::buffer(buffer, 32), 0, [this, conn, buffer](boost::system::error_code err, size_t bytes) {
		if (err == boost::asio::error::eof || err == boost::asio::error::connection_reset) {
			conn->enabled = false;
			ilog("client unsubscribed from blocks");
			delete[] buffer;
		} else {
			this->message_handler(conn, std::stringstream(std::string(buffer, bytes)));
			delete[] buffer;
			do_session(conn);
		}
	});
}

tcp_server::tcp_server(boost::asio::io_service& io_service, uint16_t port) :
	acceptor(io_service)
{
	this->acceptor.open(boost::asio::ip::tcp::v4());
	this->acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
	this->acceptor.bind({{}, port});
	this->acceptor.listen();
	this->do_accept();
}

tcp_server::~tcp_server() {
	for (connection* conn : this->connections) {
		delete conn;
	}
	this->acceptor.close();
}

void tcp_server::on_message(std::function<void(connection* const, std::stringstream)> handler) {
	this->message_handler = handler;
}
