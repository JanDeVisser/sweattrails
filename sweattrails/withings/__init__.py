#
# Copyright (c) 2015 Jan de Visser (jan@sweattrails.com)
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

import datetime
import httplib
import json

import gripe.db
import grumble.model
import grumble.property
import sweattrails.qt.async.job
import sweattrails.userprofile

logger = gripe.get_logger(__name__)

withings_user_id = "2497930"
withings_public_key = "82435cd4a1ca8d8b"
withings_host = "wbsapi.withings.net"
withings_url = "/measure?action=getmeas&userid=%s&publickey=%s&category=1"


class WithingsMeasurement(grumble.model.Model):
    timestamp = grumble.property.DateTimeProperty()
    type = grumble.property.IntegerProperty()
    value = grumble.property.FloatProperty()


class WithingsJob(sweattrails.qt.async.job.Job):
    def __init__(self):
        super(WithingsJob, self).__init__()

    def __str__(self):
        return "Downloading Withings data"

    def handle(self):
        part = self.user.get_part("WeightMgmt")
        if not part:
            self.error("downloading Withings data", "No WeightMgmt part found.")
            return
        auth = sweattrails.userprofile.WithingsAuth.query(parent=part).get()
        if auth:
            user_id = auth.user_id
            public_key = auth.public_key
        else:
            user_id = withings_user_id
            public_key = withings_public_key
            # self.error("downloading Withings data", Exception("No WithingsAuth data found."))
            # return
        conn = httplib.HTTPConnection(withings_host)
        conn.request("GET", withings_url % (user_id,  public_key))
        response = conn.getresponse()
        if response.status == 200:
            if self._parse_results(part, response):
                pass
        else:
            logger.error("Error downloading Withings data: %s", response.status)
            self.error("downloading Withing data", response.status)
        
    def _parse_results(self, part, response):
        logger.debug("Parsing downloaded Withings data")
        results = json.load(response)
        logger.debug("parsed json data")
        if results["status"] != 0:
            logger.error("Error downloading Withing data. Withings reports error: %s",
                         results["status"])
            self.error("downloading Withing data. Withings reports error: %s",
                       results["status"])
            return False
        
        logger.debug("Download result OK. %s measurements",  len(results["body"]["measuregrps"]))
        for measuregrp in results["body"]["measuregrps"]:
            ts = datetime.datetime.fromtimestamp(measuregrp["date"])
            wms = {wm for wm in WithingsMeasurement.query("timestamp = ", ts, parent=part)}
            for measure in measuregrp["measures"]:
                if not filter(lambda wm: wm.timestamp == ts and wm.type == measure["type"], wms):
                    wm = WithingsMeasurement(parent=part, timestamp=ts)
                    wm.type = measure["type"]
                    wm.value = measure["value"] * pow(10, measure["unit"])
                    wm.put()
                    wms.add(wm)
            self.convert(wms)
        return True

    def convert(self,  wms):
        for wm in wms:
            part = wm.parent()
            if wm.type in [1, 6]:
                h = sweattrails.userprofile.WeightHistory.query("snapshotdate = ", wm.timestamp, parent=part)
                if not h:
                    h = sweattrails.userprofile.WeightHistory(snapshotdate=wm.timestamp, parent=part)
                if wm.type == 1:
                    h.weight = wm.value
                else:
                    h.bfPercentage = wm.value
                h.put()
            elif wm.type in [9, 10, 11]:
                h = sweattrails.userprofile.CardioVascularHistory.query("snapshotdate = ", wm.timestamp, parent=part)
                if not h:
                    h = sweattrails.userprofile.CardioVascularHistory(snapshotdate=wm.timestamp, parent=part)
                if wm.type == 9:
                    h.bpLow = wm.value
                elif wm.type == 10:
                    h.bpHigh = wm.value
                else:
                    h.resting_hr = wm.value
                h.put()
