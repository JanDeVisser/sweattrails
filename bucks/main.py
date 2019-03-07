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

import argparse
import sys

from PyQt5.QtWidgets import QApplication

import gripe.db
import grumpy.bg

import bucks.datamodel
import bucks.gui
import bucks.schema
import bucks.tximport
import bucks.wizard


class Bucks(QApplication):
    def __init__(self, argv):
        super(Bucks, self).__init__(argv)
        self.main_window = None
        self.splash = bucks.gui.SplashScreen()
        self.importer = bucks.tximport.Importer()

    def start(self, cmdline):
        self.splash.show()
        self.processEvents()
        t = grumpy.bg.bg.BackgroundThread.get_thread()
        t.statusMessage.connect(self.status_message)
        t.progressInit.connect(self.progress_init)
        t.progressUpdate.connect(self.progress)
        t.progressEnd.connect(self.progress_done)
        t.jobStarted.connect(self.job_started)
        t.jobFinished.connect(self.job_finished)
        t.jobError.connect(self.job_error)
        t.start()
        with gripe.db.Tx.begin():
            ok = True
            if cmdline.clear:
                gripe.db.Tx.reset_schema(True)
            if bucks.datamodel.Account.query().get() is None:
                if cmdline.schema:
                    bucks.schema.SchemaImporter.import_file(cmdline.schema)
                else:
                    wizard = bucks.wizard.FirstUse()
                    ok = wizard.exec_()
            if ok:
                self.main_window = bucks.gui.MainWindow(self)
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


parser = argparse.ArgumentParser()
parser.add_argument("-c", "--clear", action="store_true", help="Erase all data")
parser.add_argument("-s", "--schema", type=str, help="Use the given file as the initial schema")

cmdline = parser.parse_args()

app = Bucks(sys.argv)
app.start(cmdline)

app.exec_()
sys.exit()
