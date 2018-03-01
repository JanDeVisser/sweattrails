#
# Copyright (c) 2014 Jan de Visser (jan@sweattrails.com)
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
# This module uses ideas and code from GoldenCheetah:
# Copyright (c) 2009 Greg Lonnon (greg.lonnon@gmail.com)
#               2011 Mark Liversedge (liversedge@gmail.com)

import json
import os.path
import sys

import jinja2

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtCore import pyqtSlot
from PyQt5.QtCore import qFatal
from PyQt5.QtCore import qWarning
from PyQt5.QtCore import QJsonDocument
from PyQt5.QtCore import QJsonParseError
from PyQt5.QtCore import QObject
from PyQt5.QtNetwork import QHostAddress
from PyQt5.QtWebChannel import QWebChannel
from PyQt5.QtWebChannel import QWebChannelAbstractTransport
from PyQt5.QtWebEngineWidgets import QWebEnginePage
from PyQt5.QtWebEngineWidgets import QWebEngineView
from PyQt5.QtWebSockets import QWebSocketServer
from PyQt5.QtWidgets import QSizePolicy

import gripe
import sweattrails.session

logger = gripe.get_logger(__name__)


class WebSocketTransport(QWebChannelAbstractTransport):
    def __init__(self, socket):
        super(WebSocketTransport, self).__init__(socket)
        self._socket = socket
        self._socket.textMessageReceived.connect(self.textMessageReceived)
        self._socket.disconnected.connect(self.deleteLater)

    def sendMessage(self, message):
        doc = QJsonDocument(message);
        self._socket.sendTextMessage(doc.toJson(QJsonDocument.Compact))

    def textMessageReceived(self, messageData):
        error = QJsonParseError()
        message = QJsonDocument.fromJson(bytes(messageData))
        if error.error:
            qWarning("Error parsing text message '%s' to JSON object: %s"  %
                     (messageData, error.errorString()))
            return
        elif not message.isObject():
            qWarning("Received JSON message that is not an object: %s", messageData)
            return
        print >> sys.stderr, "message:", message, "object:", message.object(), "type: ", type(message.object())
        self.messageReceived.emit(message.object(), self)


class WebSocketClientWrapper(QObject):
    clientConnected = pyqtSignal(WebSocketTransport)

    def __init__(self, server, parent = None):
        super(WebSocketClientWrapper, self).__init__(parent)
        self._server = server
        self._server.newConnection.connect(self.handleNewConnection)

    def handleNewConnection(self):
        self.clientConnected.emit(WebSocketTransport(self._server.nextPendingConnection()))


class WebChannelServer(object):
    def setupWebChannel(self, bridge=None, bridgename="bridge", port=12345):
        # setup the QWebSocketServer
        self._server = QWebSocketServer("Grumble webchannel", QWebSocketServer.NonSecureMode)
        if not self._server.listen(QHostAddress.LocalHost, port):
            qFatal("Failed to open web socket server.")
            return False

        # wrap WebSocket clients in QWebChannelAbstractTransport objects
        self._clientWrapper = WebSocketClientWrapper(self._server, self)

        # setup the channel
        self._channel = QWebChannel()
        self._clientWrapper.clientConnected.connect(self._channel.connectTo)

        # Publish the bridge object to the QWebChannel
        self._bridge = bridge
        if bridge is not None:
            self._channel.registerObject(bridgename, bridge);


class JsBridge(QObject):
    sendRoute = pyqtSignal(list)
    highlight = pyqtSignal(int, int)

    @pyqtSlot(str)
    def log(self, msg):
        logger.info("JS-Bridge: %s", msg)


class ConsoleLoggerWebPage(QWebEnginePage):
    def __init__(self, parent=None):
        super(ConsoleLoggerWebPage, self).__init__(parent)

    def javaScriptConsoleMessage(self, level, message, lineNumber, sourceID):
        logger.debug("JS-Console [%s] (%s/%d): %s", level, sourceID, lineNumber, message)


class IntervalMap(QWebEngineView, WebChannelServer):
    usewebchannel = False

    def __init__(self, parent):
        super(IntervalMap, self).__init__(parent)
        # assert interval.geodata, "IntervalMap only works with an interval with geodata"
        self.setContentsMargins(0, 0, 0, 0)
        self.setPage(ConsoleLoggerWebPage(self))
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setAcceptDrops(False)
        self.interval = None
        self._bridge = None

    def consoleMessage(self, level, message, lineNumber, sourceID):
        logger.debug("JS-Console [%s] (%s/%d): %s", level, sourceID, lineNumber, message)

    @pyqtSlot()
    def drawMap(self, interval):
        self.setVisible(True)
        must_init = self.interval is None
        self.interval = interval
        if must_init:
            if self.usewebchannel:
                self.setupWebChannel(JSBridge())
            self.loadFinished.connect(self.loadScript)
            self.env = jinja2.Environment(loader=jinja2.PackageLoader('sweattrails.qt', 'session'))
            template = self.env.get_template("maps.html")
            self.setHtml(template.render(usewebchannel=self.usewebchannel))
        else:
            self.sendRoute()

    @pyqtSlot(bool)
    def loadScript(self, ok):
        if ok:
            template = self.env.get_template("maps.js.j2")
            src = template.render(usewebchannel=self.usewebchannel)
            self.page().runJavaScript(src)
            self.sendRoute()
        else:
            qFatal("Could not load maps.html")

    def sendRoute(self):
        with gripe.db.Tx.begin():
            if self.interval and self.interval.waypoints():
                waypoints = [wp.to_dict() for wp in self.interval.waypoints()]
                if self.usewebchannel:
                    self._bridge.sendRoute.emit(waypoints)
                else:
                    self.page().runJavaScript("com.sweattrails.map.setRoute({0:s});".format(json.dumps(waypoints)))

    def drawSegment(self, ts, duration):
        if hasattr(ts, "total_seconds"):
            ts = int(ts.total_seconds())
        if hasattr(duration, "total_seconds"):
            duration = int(duration.total_seconds())
        if self.usewebchannel:
            self._bridge.highlight.emit(ts, duration)
        else:
            self.page().runJavaScript("com.sweattrails.map.highlight({0:d}, {1:d});".format(int(ts), int(duration)))

    def mark(self, ts):
        if hasattr(ts, "total_seconds"):
            ts = int(ts.total_seconds())
        if self.usewebchannel:
            self._bridge.mark.emit(ts)
        else:
            self.page().runJavaScript("com.sweattrails.map.mark({0:d});".format(int(ts)))
