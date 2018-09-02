#pragma once
#include <boost/asio.hpp>
#include <sstream>

class tcp_server {
private:
	boost::asio::ip::tcp::acceptor acceptor;
	std::function<void(boost::asio::ip::tcp::socket* const, std::string, std::stringstream)> message_handler;
	std::function<void(boost::asio::ip::tcp::socket* const)> disconnect_handler;

	void process_input(char** input, int* input_len);
	void do_accept();
	void do_session(boost::asio::ip::tcp::socket* socket, char* input = nullptr, int input_len = 0);

public:
	tcp_server(boost::asio::io_service& io_service, uint16_t port);
	~tcp_server();

	void on_message(std::function<void(boost::asio::ip::tcp::socket* const, std::string, std::stringstream)> handler);
	void on_disconnect(std::function<void(boost::asio::ip::tcp::socket* const)> handler);
	void send(boost::asio::ip::tcp::socket* const socket, std::string data);
};
