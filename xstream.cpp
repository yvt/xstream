#include <iostream>
#include <boost/asio.hpp>
#include <string>
#include <memory>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using local_stream = asio::local::stream_protocol;
using boost::system::error_code;


namespace detail
{
	template <class InStream, class OutStream, class Callback>
	struct AsyncPipeState: public std::enable_shared_from_this<AsyncPipeState<InStream, OutStream, Callback>>
	{
		using State = AsyncPipeState;
		std::vector<char> buffer;
		InStream& inStream;
		OutStream& outStream;
		Callback callback;
		std::uint64_t count = 0;
		
		AsyncPipeState(InStream& inStream, OutStream& outStream, std::size_t bufferSize, const Callback& callback):
		inStream(inStream),
		outStream(outStream),
		callback(callback)
		{
			buffer.resize(bufferSize);
		}
		~AsyncPipeState() = default;
		
		void run()
		{
			inStream.async_read_some(asio::buffer(buffer.data(), buffer.size()),
									 ReadOperation(this->shared_from_this()));
		}
		
		struct Operation
		{
			static constexpr bool IsAsyncPipeStateOperation = true;
			std::shared_ptr<State> state;
			Operation(const std::shared_ptr<State>& state):
			state(state) {}
		};
		
		struct ReadOperation: public Operation
		{
			ReadOperation(const std::shared_ptr<State>& state):
			Operation(state) {}
			
			void operator ()
			(const boost::system::error_code& error, std::size_t count) const
			{
				auto& s = *this->state;
				
				if (error) {
					s.callback(error, s.count);
					return;
				} else if (count == 0) {
					s.callback(boost::system::error_code(), s.count);
					return;
				}
				s.count += count;
				
				async_write(s.outStream, asio::buffer(s.buffer.data(), count), WriteOperation(this->state));
			}
		};
		
		struct WriteOperation: public Operation
		{
			WriteOperation(const std::shared_ptr<State>& state):
			Operation(state) {}
			
			void operator ()
			(const boost::system::error_code& error, std::size_t count) const
			{
				auto& s = *this->state;
				
				if (count == 0 || error) {
					s.callback(error, s.count);
					return;
				}
				
				s.run();
			}
		};
		
	};
	
	/* won't work for some reason...
	template <typename Function, class InStream, class OutStream, class Callback>
	inline void asio_handler_invoke
	(Function function,
	 typename AsyncPipeState<InStream, OutStream, Callback>::Operation *thisHandler)
	{
		boost_asio_handler_invoke_helpers::invoke
		(function, thisHandler->state->callback);
	}
	*/
	
	template <typename Function, typename Operation>
	inline typename
	std::enable_if<Operation::IsAsyncPipeStateOperation>::type
	asio_handler_invoke
	(Function function, Operation *thisHandler)
	{
		boost_asio_handler_invoke_helpers::invoke
		(function, thisHandler->state->callback);
	}
	
}

template <class InStream, class OutStream, class Callback>
void startAsyncPipe(InStream& inStream, OutStream& outStream, std::size_t bufferSize, const Callback& callback)
{
	using State = detail::AsyncPipeState<InStream, OutStream, Callback>;
	
    auto state = std::make_shared<State>(inStream, outStream, bufferSize, callback);
    state->run();
}

template <class T1, class T2>
class XStreamConnection :
public std::enable_shared_from_this<XStreamConnection<T1, T2>>
{
	T1 ep1;
	T2 ep2;

	void errored()
	{
		error_code error;
		ep1.close(error);
		ep2.close(error);
	}

public:
	using Ptr = std::shared_ptr<XStreamConnection>;

	XStreamConnection(asio::io_service &io) :
	ep1(io), ep2(io)
	{ }

	T1& stream1() { return ep1; }
	T2& stream2() { return ep2; }

	void start()
	{
		auto self = this->shared_from_this();
		startAsyncPipe(ep1, ep2, 4096, [self, this] (const error_code &error, std::uint64_t) {
			errored();
		});
		startAsyncPipe(ep2, ep1, 4096, [self, this] (const error_code &error, std::uint64_t) {
			errored();
		});
	}
};

class XStreamCore :
public std::enable_shared_from_this<XStreamCore>
{
	asio::io_service io;
	std::string socketPath;
	int tcpPort;

	using Connection = XStreamConnection<tcp::socket, local_stream::socket>;
	Connection::Ptr awaitingConnection;
	tcp::acceptor acceptor;

	void accept()
	{
		auto self = shared_from_this();
		awaitingConnection = std::make_shared<Connection>(io);
		acceptor.async_accept(awaitingConnection->stream1(),
			[this, self] (const error_code &error) {
				if (error) {
					std::cerr << "accept: " << error << std::endl;
				} else {
					Connection::Ptr thisConnecion;
					std::swap(thisConnecion, awaitingConnection);
					accept();

					error_code localSocketError;
					thisConnecion->stream2().connect(socketPath, localSocketError);
					if (localSocketError) {
						std::cerr << "connecting local socket: " << localSocketError << std::endl;
						thisConnecion->stream1().close(localSocketError);
						return;
					}
					thisConnecion->start();
				}
			});
	}

public:
	XStreamCore(const std::string& socketPath, int port) :
	socketPath(socketPath),
	tcpPort(port),
	acceptor(io, tcp::endpoint(tcp::v4(), port))
	{

	}

	void run()
	{
		accept();
		io.run();
	}
};

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::cerr << "Usage: xstream SOCKET PORT" << std::endl;
		return 1;
	}

	auto core = std::make_shared<XStreamCore>(argv[1], std::stoi(argv[2]));
	core->run();
}

