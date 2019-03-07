#
# Copyright (c) 2017 Jan de Visser (jan@sweattrails.com)
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

import http.server
import urllib.parse

import gripe
import grumpy.bg.bg

logger = gripe.get_logger(__name__)


class WebServerHandler(http.server.BaseHTTPRequestHandler):
    config = gripe.Config.app.sweattrails.background.webserver \
        if "webserver" in gripe.Config.app.sweattrails.background \
        else {}
    handlers = config.handlers if "handlers" in config else {}

    def __getattr__(self, name):
        if name.startswith("do_"):
            return self.do_subhandler
        else:
            raise AttributeError("Attribute " + name + " not found in WebServerHandler")

    def do_subhandler(self):
        parsed_path = urllib.parse.urlparse(self.path)
        subhandler = None
        for h in self.handlers:
            p = h["path"]
            if parsed_path.path.startswith(p):
                subhandler = h["handler"]
                break
        if subhandler:
            subhandler = gripe.resolve(subhandler)(self)
            if subhandler:
                try:
                    subhandler = getattr(subhandler, "do_" + self.command)
                except AttributeError:
                    subhandler = getattr(subhandler, "do")
                subhandler()
        if subhandler is None:
            self.send_response(404)


class WebServer(grumpy.bg.bg.ThreadPlugin):
    def run(self):
        config = gripe.Config.app.sweattrails.background.webserver \
            if "webserver" in gripe.Config.app.sweattrails.background \
            else {}
        interface = config.get("interface", "127.0.0.1")
        port = config.get("port", 8080)
        self.server = http.server.HTTPServer((interface, port), WebServerHandler)
        logger.debug('Starting server at http://%s:%s', interface, port)
        self.server.serve_forever()

    def quit(self):
        self.server.shutdown()
