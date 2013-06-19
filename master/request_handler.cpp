//
// request_handler.cpp
// ~~~~~~~~~~~~~~~~~~~
//
// Modified by Pierre David from the Boost "http server2"
// example by Christopher M. Kohlhoff
//
// Copyright (c) 2003-2008 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "request_handler.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <boost/lexical_cast.hpp>
#include "http/mime_types.hpp"
#include "http/reply.hpp"
#include "http/request.hpp"
#include "http/server.hpp"

#include "conf.h"
#include "option.h"
#include "resource.h"
#include "slave.h"
#include "sos.h"
#include "master.h"

extern conf cf ;
extern master master ;

request_handler::request_handler(const std::string& doc_root)
  : doc_root_(doc_root)
{
}

void request_handler::handle_request(const http::server2::request& req, http::server2::reply& rep)
{
  std::string request_path;

  // analyzes "%hh" and "+" in req.uri to get a usable path
  if (!url_decode(req.uri, request_path))
  {
    rep = http::server2::reply::stock_reply(http::server2::reply::bad_request);
    return;
  }

#if 0
{
  std::cout << "req.method=" << req.method << "\n" ;
  std::cout << "req.uri=" << req.uri
  		<< " => request_path=" << request_path << "\n" ;
  for (auto &h : req.headers)
  {
    std::cout << "header <" << h.name << "," << h.value << ">\n" ;
  }
  std::cout << "args=" << req.rawargs << "\n" ;
  for (auto &h : req.postargs)
  {
    std::cout << "arg= <" << h.name << "," << h.value << ">\n" ;
  }
}
#endif

  // Request path must be absolute and not contain "..".
  if (request_path.empty() || request_path[0] != '/'
      || request_path.find("..") != std::string::npos)
  {
    rep = http::server2::reply::stock_reply(http::server2::reply::bad_request);
    return;
  }

  // Request path must not end with a slash (i.e. is a directory)
  if (request_path[request_path.size() - 1] == '/')
  {
    rep = http::server2::reply::stock_reply(http::server2::reply::bad_request);
    return;
  }

  master.handle_http (request_path, req, rep) ;
}

bool request_handler::url_decode(const std::string& in, std::string& out)
{
  out.clear();
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size(); ++i)
  {
    if (in[i] == '%')
    {
      if (i + 3 <= in.size())
      {
        int value;
        std::istringstream is(in.substr(i + 1, 2));
        if (is >> std::hex >> value)
        {
          out += static_cast<char>(value);
          i += 2;
        }
        else
        {
          return false;
        }
      }
      else
      {
        return false;
      }
    }
    else if (in[i] == '+')
    {
      out += ' ';
    }
    else
    {
      out += in[i];
    }
  }
  return true;
}