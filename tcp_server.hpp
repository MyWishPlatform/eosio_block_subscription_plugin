#pragma once
#include <sstream>
#include "connection.hpp"

class tcp_server {
private:
	boost::asio::ip::tcp::acceptor acceptor;
	std::vector<connection*> connections;
	std::function<void(connection* const, std::stringstream)> message_handler;

	void do_accept();
	void do_session(connection* conn);

public:
	tcp_server(boost::asio::io_service& io_service, uint16_t port);
	~tcp_server();

	void on_message(std::function<void(connection* const, std::stringstream)> handler);
};
