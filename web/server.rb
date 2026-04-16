#!/usr/bin/env ruby
# frozen_string_literal: true

# Development server with COOP/COEP headers for SharedArrayBuffer support.
# Based on ktock/qemu-wasm examples (GPLv2).
# SPDX-License-Identifier: GPL-2.0
#
# Usage: ruby server.rb [port]
# No gems required - uses Ruby's built-in WEBrick.

require "webrick"

port = (ARGV[0] || 8088).to_i
doc_root = __dir__

server = WEBrick::HTTPServer.new(
  Port: port,
  DocumentRoot: doc_root,
  Logger: WEBrick::Log.new($stderr, WEBrick::Log::INFO),
  MimeTypes: WEBrick::HTTPUtils::DefaultMimeTypes.merge(
    "wasm" => "application/wasm",
    "js"   => "application/javascript"
  )
)

# Inject COOP/COEP headers on all responses
default_servlet = WEBrick::HTTPServlet::FileHandler.new(server, doc_root)
server.mount_proc("/") do |req, res|
  default_servlet.service(req, res)
  res["Cross-Origin-Opener-Policy"] = "same-origin"
  res["Cross-Origin-Embedder-Policy"] = "require-corp"
end

trap("INT") { server.shutdown }

puts "Ruby on Bare Metal dev server"
puts "  http://localhost:#{port}/"
puts "  COOP/COEP headers enabled for SharedArrayBuffer"
puts "  Press Ctrl+C to stop"
server.start
