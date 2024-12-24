/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef DVD_DIVIDE_ASIO_UTILS_H_
#define DVD_DIVIDE_ASIO_UTILS_H_

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/deadline_timer.hpp>

namespace Divide
{
    using deadline_timer = boost::asio::basic_deadline_timer<boost::posix_time::ptime, boost::asio::time_traits<boost::posix_time::ptime>, boost::asio::io_context::executor_type>;

    using tcp_socket   = boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;
    using tcp_acceptor = boost::asio::basic_socket_acceptor<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;
    using tcp_resolver = boost::asio::ip::basic_resolver<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;

    using udp_socket   = boost::asio::basic_datagram_socket<boost::asio::ip::udp, boost::asio::io_context::executor_type>;
    using udp_resolver = boost::asio::ip::basic_resolver<boost::asio::ip::udp, boost::asio::io_context::executor_type>;

    static constexpr char LocalHostAddress[] = "127.0.0.1";
    static constexpr U16 NetworkingPort = 3443u;

    [[nodiscard]] inline bool IsLocalHostAddress(const std::string_view address) noexcept
    {
        return address == "localhost" ||
               address == LocalHostAddress;
    }
} // namespace Divide

#endif //DVD_DIVIDE_ASIO_UTILS_H_
