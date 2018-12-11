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

import BaseHTTPServer
import threading
import stravalib
import urlparse

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtCore import pyqtSlot
from PyQt5.QtCore import qFatal
from PyQt5.QtCore import qWarning
from PyQt5.QtCore import QJsonDocument
from PyQt5.QtCore import QJsonParseError
from PyQt5.QtCore import QObject
from PyQt5.QtCore import QThread
from PyQt5.QtCore import QUrl

from PyQt5.QtNetwork import QHostAddress

from PyQt5.QtWebChannel import QWebChannel
from PyQt5.QtWebChannel import QWebChannelAbstractTransport

from PyQt5.QtWebEngineWidgets import QWebEnginePage
from PyQt5.QtWebEngineWidgets import QWebEngineView
from PyQt5.QtWebSockets import QWebSocketServer

from PyQt5.QtWidgets import QAction
from PyQt5.QtWidgets import QApplication
from PyQt5.QtWidgets import QCheckBox
from PyQt5.QtWidgets import QDialog
from PyQt5.QtWidgets import QDialogButtonBox
from PyQt5.QtWidgets import QFileDialog
from PyQt5.QtWidgets import QFormLayout
from PyQt5.QtWidgets import QLabel
from PyQt5.QtWidgets import QLineEdit
from PyQt5.QtWidgets import QMainWindow
from PyQt5.QtWidgets import QMessageBox
from PyQt5.QtWidgets import QProgressBar
from PyQt5.QtWidgets import QSizePolicy
from PyQt5.QtWidgets import QTabWidget
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import gripe

logger = gripe.get_logger(__name__)


class ConsoleLoggerWebPage(QWebEnginePage):
    def __init__(self, parent=None):
        super(ConsoleLoggerWebPage, self).__init__(parent)

    def javaScriptConsoleMessage(self, level, message, lineNumber, sourceID):
        logger.debug("JS-Console [%s] (%s/%d): %s", level, sourceID, lineNumber, message)


class AuthWindow(QWebEngineView):
    def __init__(self, url):
        super(AuthWindow, self).__init__()
        self.setContentsMargins(0, 0, 0, 0)
        self.setPage(ConsoleLoggerWebPage(self))
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setAcceptDrops(False)
        self.url = QUrl(url)

    def loadPage(self):
        self.loadFinished.connect(Strava.intance().pageLoaded)
        self.setUrl(self.url)
        self.setVisible(True)


class Handler(object):
    def __init__(self, handler):
        self.handler = handler

    def do_GET(self):
        parsed_path = urlparse.urlparse(self.handler.path)
        query = urlparse.parse_qs(parsed_path.query)
        code = query["code"][0] if "code" in query else None
        if code is not None:
            Strava.instance().codeReceived.emit(code)
            self.send_response(200)
        else:
            Strava.instance().authenticationFailed()
            self.send_response(401)


class Strava(QObject):
    STRAVA_CLIENT_ID = 20069
    STRAVA_CLIENT_SECRET = "c4ad3f07cffa11c849e9cff08ff87604abeb8699"
    REDIRECT_INTERFACE = "127.0.0.1"
    REDIRECT_PORT = 8080
    REDIRECT_URL = 'http://{0}:{1:d}/strava/auth'.format(REDIRECT_INTERFACE, REDIRECT_PORT)

    singleton = None
    codeReceived = pyqtSignal(str)
    authenticated = pyqtSignal()
    authenticationFailed = pyqtSignal()

    def __init__(self):
        assert self.__class__.singleton is None
        super(Strava, self).__init__()
        if "strava" not in gripe.Config:
            gripe.Config.strava = {}
        self.config = gripe.Config.strava
        self.client = stravalib.Client(self.config.get("access_code", None))
        self.codeReceived.connect(self.code_received)

    def auth(self):
        if self.config.access_token is None:
            url = self.client.authorization_url(client_id=self.STRAVA_CLIENT_ID,
                                                redirect_uri=self.REDIRECT_URL)
            self.window = AuthWindow(url)
            self.window.loadPage()
        else:
            self.athlete = self.client.get_athlete()

    def pageLoaded(self, ok):
        if not ok:
            assert False, "Could not load Strava Auth page"

    def code_received(self, code):
        self.config["access_token"] = self.client.exchange_code_for_token(
            client_id=self.STRAVA_CLIENT_ID,
            client_secret=self.STRAVA_CLIENT_SECRET,
            code=code)
        self.config = gripe.Config.set("strava", self.config)
        self.window.deleteLater()
        logger.debug("Strava access token is '%s'", self.config.access_token)
        self.athlete = self.client.get_athlete()
        self.authenticated.emit()

    @classmethod
    def instance(cls):
        if cls.singleton is None:
            cls.singleton = Strava()
        return cls.singleton


def authenticate(**kwargs):
    strava = Strava.instance()
    strava.auth()
    return 0
