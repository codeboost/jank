#include "nrepl_server.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <cerrno>

namespace nrepl_server
{
  // client
  class client::impl
  {
  public:
    impl()
      : socket_fd_(-1)
      , connected_(false)
    {
    }

    explicit impl(int socket_fd)
      : socket_fd_(socket_fd)
      , connected_(true)
    {
    }

    ~impl()
    {
      if(socket_fd_ >= 0)
      {
        ::close(socket_fd_);
      }
    }

    bool is_connected() const
    {
      return connected_;
    }

    std::string read_some()
    {
      if(!connected_)
      {
        return "";
      }

      ssize_t length = ::recv(socket_fd_, rx_buf_, rx_capacity, 0);

      if(length <= 0)
      {
        // Connection closed or error
        connected_ = false;
        return "";
      }

      return { rx_buf_, static_cast<std::size_t>(length) };
    }

    void write_some(std::string const &message)
    {
      if(!connected_)
      {
        return;
      }

      ssize_t sent = ::send(socket_fd_, message.data(), message.size(), 0);

      if(sent < 0)
      {
        connected_ = false;
      }
    }

  private:
    int socket_fd_;
    bool connected_;

    static constexpr std::size_t rx_capacity{ 1024ul * 1024ul }; // 1MiB
    char rx_buf_[rx_capacity]{};
  };

  // client
  client::client(std::unique_ptr<client::impl> impl)
    : impl_(std::move(impl))
  {
  }

  bool client::is_connected()
  {
    return impl_->is_connected();
  }

  std::string client::read_some()
  {
    auto data = impl_->read_some();
    return data;
  }

  void client::write_some(std::string const &data)
  {
    impl_->write_some(data);
  }

  // server
  class nrepl_server::impl
  {
  public:
    impl(int port)
      : server_fd_(-1)
    {
      // Create socket
      server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
      if(server_fd_ < 0)
      {
        throw std::runtime_error("Failed to create socket");
      }

      // Set socket options to reuse address
      int opt = 1;
      if(::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
      {
        ::close(server_fd_);
        throw std::runtime_error("Failed to set socket options");
      }

      // Bind to loopback address
      struct sockaddr_in address;
      std::memset(&address, 0, sizeof(address));
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = inet_addr("127.0.0.1");
      address.sin_port = htons(static_cast<uint16_t>(port));

      if(::bind(server_fd_, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0)
      {
        ::close(server_fd_);
        throw std::runtime_error("Failed to bind socket");
      }

      // Listen for connections
      if(::listen(server_fd_, SOMAXCONN) < 0)
      {
        ::close(server_fd_);
        throw std::runtime_error("Failed to listen on socket");
      }
    }

    ~impl()
    {
      if(server_fd_ >= 0)
      {
        ::close(server_fd_);
      }
    }

    std::unique_ptr<client::impl> accept()
    {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);

      int client_fd = ::accept(server_fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

      if(client_fd < 0)
      {
        throw std::runtime_error("Failed to accept client connection");
      }

      return std::make_unique<client::impl>(client_fd);
    }

  private:
    int server_fd_;
  };

  nrepl_server::nrepl_server(int port)
    : impl_(std::make_unique<nrepl_server::impl>(port))
  {
  }

  nrepl_server::~nrepl_server() = default;

  client *nrepl_server::accept()
  {
    auto impl = impl_->accept();

    // TODO: This leaks memory but jank complains about not being able to delete
    // an opaque type if we return unique_ptr<client>.
    return new client(std::move(impl));
  }
}
