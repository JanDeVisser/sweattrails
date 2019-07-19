#
#   Copyright (c) 2019 Jan de Visser (jan@sweattrails.com)
#
#   This program is free software; you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the Free
#   Software Foundation; either version 2 of the License, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful, but WITHOUT
#   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#   more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program; if not, write to the Free Software Foundation, Inc., 51
#   Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#

from PyQt5.QtGui import QPixmap

from PyQt5.QtWidgets import QApplication
from PyQt5.QtWidgets import QAction
from PyQt5.QtWidgets import QLabel
from PyQt5.QtWidgets import QMainWindow
from PyQt5.QtWidgets import QMessageBox
from PyQt5.QtWidgets import QProgressBar
from PyQt5.QtWidgets import QSplashScreen
from PyQt5.QtWidgets import QTabWidget
from PyQt5.QtWidgets import QVBoxLayout
from PyQt5.QtWidgets import QWidget

import gripe
import gripe.db
import grumpy.bg

import bucks.schema
import bucks.tximport
import bucks.app.wizard

from bucks.app.account import AccountTab
from bucks.app.category import CategoryTab
from bucks.app.contact import ContactTab
from bucks.app.institution import InstitutionTab
from bucks.app.project import ProjectTab
from bucks.app.transaction import TransactionTab
from bucks.datamodel.account import Account


class SplashScreen(QSplashScreen):
    def __init__(self):
        super(SplashScreen, self).__init__(QPixmap("image/splash.png"))


class MainWindow(QMainWindow):
    def __init__(self, app):
        super(MainWindow, self).__init__()
        self._app = app
        self.table = None
        file_menu = self.menuBar().addMenu(self.tr("&File"))
        file_menu.addAction(
            QAction("E&xit", self, shortcut="Ctrl+Q", statusTip="Exit", triggered=self.close))
        window = QWidget()
        layout = QVBoxLayout()
        self.tabs = QTabWidget()
        self.tabs.addTab(TransactionTab(self), "Transactions")
        # self.tabs.addTab(InstitutionTab(self), "Institutions")
        # self.tabs.addTab(AccountTab(self), "Accounts")
        self.tabs.addTab(CategoryTab(self), "Categories")
        # self.tabs.addTab(ProjectTab(self), "Projects")
        # self.tabs.addTab(ContactTab(self), "Contacts")
        layout.addWidget(self.tabs)
        window.setLayout(layout)
        self.message_label = QLabel()
        self.message_label.setMinimumWidth(200)
        self.statusBar().addPermanentWidget(self.message_label)
        self.progressbar = QProgressBar()
        self.progressbar.setMinimumWidth(100)
        self.progressbar.setMinimum(0)
        self.progressbar.setMaximum(100)
        self.statusBar().addPermanentWidget(self.progressbar)
        self.setCentralWidget(window)

    def app(self):
        return self._app

    def status_message(self, msg, *args):
        self.message_label.setText(str(msg).format(*args))

    def error_message(self, msg, e):
        if e:
            msg = str(e) if not msg else "%s: %s" % (msg, str(e))
        if not msg:
            msg = "Unknown error"
        QMessageBox.error(self, "Error", msg)

    def progress_init(self, msg, *args):
        self.progressbar.setValue(0)
        self.status_message(msg, *args)

    def progress(self, percentage):
        self.progressbar.setValue(percentage)

    def progress_done(self):
        self.progressbar.reset()


class Bucks(QApplication):
    def __init__(self, argv):
        super(Bucks, self).__init__(argv)
        self.main_window = None
        self.splash = SplashScreen()
        self.importer = bucks.tximport.Importer()

    def start(self, cmdline):
        self.splash.show()
        self.processEvents()
        with gripe.db.Tx.begin():
            ok = True
            if cmdline.clear:
                gripe.db.Tx.reset_schema(True)
            if Account.query().get() is None:
                if cmdline.schema:
                    bucks.schema.SchemaImporter.import_file(cmdline.schema)
                    self.processEvents()
                if not cmdline.schema or Account.query().get() is None:
                    wiz = bucks.app.wizard.FirstUse()
                    ok = wiz.exec_()
            if cmdline.imp:
                acc, file_name = cmdline.imp.split(':', 2)
                account = Account.by("acc_name", acc)
                assert account
                self.importer.execute(account, file_name)
            if ok:
                self.main_window = MainWindow(self)
        self.processEvents()
        t = grumpy.bg.bg.BackgroundThread.get_thread()
        t.statusMessage.connect(self.status_message)
        t.progressInit.connect(self.progress_init)
        t.progressUpdate.connect(self.progress)
        t.progressEnd.connect(self.progress_done)
        t.jobStarted.connect(self.job_started)
        t.jobFinished.connect(self.job_finished)
        t.jobError.connect(self.job_error)
        t.add_plugin(bucks.tximport.ScanInbox)
        t.start()
        self.processEvents()
        self.splash.finish(self.main_window)
        self.splash = None
        if self.main_window is not None:
            self.main_window.show()

    def status_message(self, msg, *args):
        self.main_window.status_message(msg, *args)

    def progress_init(self, msg, *args):
        self.main_window.progress_init(msg, *args)

    def progress(self, percentage):
        self.main_window.progress(percentage)

    def progress_done(self):
        self.main_window.progress_done()

    def job_started(self, job):
        self.status_message("{0} started", job)

    def job_finished(self, job):
        self.status_message("{0} finished", job)

    def job_error(self, job, msg, ex):
        args = [str(job), str(ex) if not msg else "%s: %s" % (msg, str(ex))]
        self.status_message("Error executing {0}: {1}", *args)
