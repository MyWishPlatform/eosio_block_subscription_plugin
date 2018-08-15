#include "tcp_server.hpp"
#include <fc/log/logger.hpp>

void tcp_server::do_accept() {
	boost::asio::ip::tcp::socket* socket = new boost::asio::ip::tcp::socket(this->acceptor.get_io_service());
	this->acceptor.async_accept(*socket, [this, socket](boost::system::error_code ec) {
		this->do_session(socket);
		this->do_accept();
	});
}

void tcp_server::do_session(boost::asio::ip::tcp::socket* const socket) {
	char* buffer = new char[32];
	socket->async_receive(boost::asio::buffer(buffer, 32), 0, [this, socket, buffer](boost::system::error_code err, size_t bytes) {
		if (err == boost::asio::error::eof || err == boost::asio::error::connection_reset) {
			delete[] buffer;
			this->disconnect_handler(socket);
			delete socket;
		} else {
			this->message_handler(socket, std::stringstream(std::string(buffer, bytes)));
			delete[] buffer;
			do_session(socket);
		}
	});
}

void tcp_server::send(boost::asio::ip::tcp::socket* const socket, std::string string) {
	int size = string.size();
	boost::asio::write(*socket, boost::asio::buffer(static_cast<char*>(static_cast<void*>(&size)), 4));
	boost::asio::write(*socket, boost::asio::buffer(string));
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
	this->acceptor.close();
}

void tcp_server::on_message(std::function<void(boost::asio::ip::tcp::socket* const, std::stringstream)> handler) {
	this->message_handler = handler;
}

void tcp_server::on_disconnect(std::function<void(boost::asio::ip::tcp::socket* const)> handler) {
	this->disconnect_handler = handler;
}
