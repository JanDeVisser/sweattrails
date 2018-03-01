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

import exceptions
import traceback

import gripe
import grit.upload
import grizzle
import grudge
import grumble
import grumble.model
import sweattrails.device
import sweattrails.device.exceptions
import sweattrails.session

logger = gripe.get_logger(__name__)


@grudge.OnStarted(grudge.Invoke())
@grudge.OnAdd("done", grudge.Stop())
@grudge.Process()
class UploadActivity(grumble.Model):
    uploadedFile = grumble.ReferenceProperty(grit.upload.UploadedFile)
    athlete = grumble.ReferenceProperty(grizzle.User)
    filename = grumble.property.StringProperty()
    converted = grumble.BooleanProperty(default=False)
    error = grumble.TextProperty()

    done = grudge.Status()

    def check(self):
        logger.debug("UploadActivity(%s).check", self.filename)
        uploaded = self.uploadedFile
        user = uploaded.get_user()
        if not user.has_role("athlete"):
            logger.debug(
                "UploadActivity(%s).check: User %s cannot upload activities because they don't have the 'athlete' role",
                self.filename, user.uid())
            self.error = "User %s cannot upload activities because they don't not have the 'athlete' role" % user.uid()
            self.put()
            return False
        q = UploadActivity.query('"filename" =',   uploaded.filename,
                                 '"athlete" =',    user,
                                 '"converted" = ', True)
        already_uploaded = q.get()
        if already_uploaded:
            logger.debug("UploadActivity(%s).check: User '%s' already uploaded file", self.filename, user.uid())
            self.error = "User '%s' already uploaded file '%s'" % (user.uid(), uploaded.filename)
            self.put()
            return False
        self.filename = uploaded.filename
        self.athlete = user
        self.put()
        logger.debug("UploadActivity(%s).check OK", self.filename)
        return True

    def import_file(self):
        logger.debug("UploadActivity(%s).import_file", self.filename)
        uploaded = self.uploadedFile
        try:
            parser = sweattrails.device.get_parser(uploaded.filename)
            parser.set_athlete(self.athlete)
            parser.parse(uploaded.content.adapted)
            logger.debug("UploadActivity(%s).import_file: file parsed", self.filename)
            self.converted = True
        except exceptions.Exception as e:
            logger.exception("Exception parsing file %s", self.filename)
            self.error = traceback.format_exc()
        self.put()

    def cleanup(self):
        logger.debug("UploadActivity(%s).cleanup", self.filename)
        grumble.model.delete(self.uploadedFile)
        self.uploadedFile = None
        self.put()

    def invoke(self):
        logger.debug("UploadActivity(%s).invoke", self.filename)
        if self.check():
            self.import_file()
        self.cleanup()
