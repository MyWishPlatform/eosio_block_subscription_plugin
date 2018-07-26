#pragma once
#include <boost/asio.hpp>

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
