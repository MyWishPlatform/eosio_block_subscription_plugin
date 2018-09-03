#include "tcp_server.hpp"
#include <fc/log/logger.hpp>

#define BUFFER_SIZE 32

void tcp_server::do_accept() {
	boost::asio::ip::tcp::socket* socket = new boost::asio::ip::tcp::socket(this->acceptor.get_io_service());
	this->acceptor.async_accept(*socket, [this, socket](boost::system::error_code ec) {
		this->do_session(socket);
		this->do_accept();
	});
}

void tcp_server::process_input(boost::asio::ip::tcp::socket* const socket, char** input, int* input_len) {
	bool done = true;
	for (int i = 0; i < *input_len; i++) {
		if ((*input)[i] == '\n') {
			done = false;
			std::string input_str(*input, i);
			this->message_handler(socket, input_str, std::stringstream(input_str));
			char* new_input = new char[*input_len - i - 1];
			std::memcpy(new_input, (*input)+i + 1, *input_len - i - 1);
			*input_len = *input_len - i - 1;
			delete[] *input;
			*input = new_input;
			break;
		}
	}
	if (!done) this->process_input(socket, input, input_len);
}

void tcp_server::do_session(boost::asio::ip::tcp::socket* const socket, char* input, int input_len) {
	char* buffer = new char[BUFFER_SIZE];
	socket->async_receive(boost::asio::buffer(buffer, BUFFER_SIZE), 0, [this, socket, buffer, input, input_len](boost::system::error_code err, size_t bytes) {
		int _input_len = input_len;
		char* _input = input;
		if (err == boost::asio::error::eof || err == boost::asio::error::connection_reset) {
			delete[] buffer;
			this->disconnect_handler(socket);
			delete socket;
		} else {
			if (_input_len == 0) {
				_input_len = bytes;
				_input = new char[bytes];
				memcpy(_input, buffer, bytes);
			} else {
				char* new_input = new char[_input_len + bytes];
				std::memcpy(new_input, _input, _input_len);
				std::memcpy(new_input + _input_len, buffer, bytes);
				_input_len += bytes;
				delete[] _input;
				_input = new_input;
			}
			process_input(socket, &_input, &_input_len);
			delete[] buffer;
			do_session(socket, _input, _input_len);
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

void tcp_server::on_message(std::function<void(boost::asio::ip::tcp::socket* const, std::string, std::stringstream)> handler) {
	this->message_handler = handler;
}

void tcp_server::on_disconnect(std::function<void(boost::asio::ip::tcp::socket* const)> handler) {
	this->disconnect_handler = handler;
}
