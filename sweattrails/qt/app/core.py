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

from PyQt5.QtCore import QCoreApplication
from PyQt5.QtCore import QObject
from PyQt5.QtCore import pyqtSignal

import gripe
import gripe.db
import grizzle
import grumble.property
import sweattrails.qt.bg.bg
import sweattrails.qt.bg.job
import sweattrails.qt.imports
import sweattrails.qt.mainwindow
import sweattrails.qt.session.details
import sweattrails.session
import sweattrails.withings

logger = gripe.get_logger(__name__)


class NotAuthenticatedException(gripe.AuthException):
    def __str__(self):
        return "Not authenticated"


class SweatTrailsCore(object):
    refresh = pyqtSignal(QObject, name="refresh")

    def init_config(self, args):
        save = False
        self.user = self.user_id = None
        if "qtapp" not in gripe.Config:
            gripe.Config.qtapp = {}
        self.config = gripe.Config.qtapp
        if "settings" not in self.config:
            self.config["settings"] = {}
            save = True
        if save:
            self.config = gripe.Config.set("qtapp", self.config)
        save = False
        if args.user and args.password:
            if QCoreApplication.instance().has_users():
                self.authenticate(uid=args.user,
                                  password=args.password,
                                  savecredentials=args.savecredentials)
            else:
                self.add_user(args.user, args.password, args.user, args.savecredentials)
        elif "user" in self.config.settings:
                user_settings = self.config.settings.user
                uid = user_settings.user_id if "user_id" in user_settings else None
                password = user_settings.password if "password" in user_settings else None
                logger.debug("Auto-login uid %s", uid)
                if not uid or not self.authenticate(uid=uid, password=password, savecredentials=False):
                    del self.config.settings["user"]
                    save = True
        else:
            self.authenticate(uid=args.user, password=args.password, savecredentials=args.savecredentials)
        if save:
            self.config = gripe.Config.set("qtapp", self.config)

    def start(self, args):
        self.init_config(args)
        t = sweattrails.qt.bg.bg.BackgroundThread.get_thread()
        t.statusMessage.connect(self.status_message)
        t.progressInit.connect(self.progress_init)
        t.progressUpdate.connect(self.progress)
        t.progressEnd.connect(self.progress_done)
        t.jobStarted.connect(self.job_started)
        t.jobFinished.connect(self.job_finished)
        t.jobError.connect(self.job_error)
        t.start()

    def status_message(self, msg, *args):
        print(msg.format(*args))

    def job_started(self, job):
        self.status_message("{0} started", job)

    def job_finished(self, job):
        self.status_message("{0} finished", job)

    def job_error(self, job, msg, ex):
        args = [str(job), str(ex) if not msg else "%s: %s" % (msg, str(ex))]
        self.status_message("Error executing {0}: {1}", *args)

    def user_manager(self):
        if not hasattr(self, "_user_manager"):
            self._user_manager = grizzle.UserManager()
        return self._user_manager

    def has_users(self):
        mgr = self.user_manager()
        return mgr.has_users()

    def authenticate(self, **kwargs):
        logger.debug("Authenticating")
        self.user = None
        self.user_id = None
        um = self.user_manager()
        with gripe.db.Tx.begin():
            self.user = um.authenticate(**kwargs)
            if self.user is not None:
                self.user_id = self.user.id
                self.refresh.emit(None)
        return self.user is not None

    # FIXME When creating a new user from within the app, should not confirm.
    # probably best to just add a new method for that.
    def add_user(self, uid, password, display_name, savecreds):
        um = self.user_manager()
        with gripe.db.Tx.begin():
            user = um.add(uid, password=password, display_name=display_name)
            user.confirm()
        return self.authenticate(uid, password, savecreds)

    def is_authenticated(self):
        return self.user is not None

    def import_files(self, *filenames):
        t = sweattrails.qt.bg.bg.BackgroundThread.get_thread()
        for f in filenames:
            job = sweattrails.qt.imports.ImportFile(f)
            job.jobFinished.connect(self._refresh)
            t.addjob(job)

    def _refresh(self, job):
        self.refresh.emit(job)

    def download(self):
        job = sweattrails.qt.imports.DownloadJob(self.getDownloadManager())
        job.jobFinished.connect(self._refresh)
        job.jobError.connect(self.status_message)
        job.submit()

    def withings(self):
        job = sweattrails.withings.WithingsJob()
        job.jobFinished.connect(self._refresh)
        job.submit()

    def reanalyze(self):
        with gripe.db.Tx.begin():
            q = sweattrails.session.Session.query(athlete=QCoreApplication.instance().user)
            for session in q:
                job = sweattrails.qt.session.details.ReanalyzeJob(session)
                job.submit()
